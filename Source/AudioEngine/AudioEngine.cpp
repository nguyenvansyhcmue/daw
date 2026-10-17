#include "AudioEngine.h"
#include "GainUtilityProcessor.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int livePluginLatencyBudgetSamples = 64;
constexpr uint64_t recordingDeviceRecoveryGraceMs = 1000;

int preferredLiveBufferSize(const juce::Array<int>& availableSizes, int currentSize)
{
    // The production live policy is 64 samples when the device supports it.
    // Do not auto-promote to 32: stability under a crowded live session is
    // more valuable than a small theoretical latency reduction.
    if (currentSize == 64 && availableSizes.contains(currentSize))
        return currentSize;
    for (const auto preferred : { 64, 128, 256 })
        if (availableSizes.contains(preferred))
            return preferred;

    auto smallest = currentSize;
    for (const auto size : availableSizes)
        if (smallest <= 0 || size < smallest)
            smallest = size;
    return smallest;
}

juce::File getAudioDeviceStateFile()
{
    auto settingsFolder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile("StudioForge");
    settingsFolder.createDirectory();
    return settingsFolder.getChildFile("audio-device.xml");
}

float peakForChannel(const juce::AudioBuffer<float>& buffer, int channel, int numSamples) noexcept
{
    if (channel >= buffer.getNumChannels()) return 0.0f;
    auto peak = 0.0f;
    const auto* samples = buffer.getReadPointer(channel);
    for (int sample = 0; sample < numSamples; ++sample)
        peak = juce::jmax(peak, std::abs(samples[sample]));
    return peak;
}

std::unique_ptr<juce::AudioFormat> createOfflineFormat(const juce::File& destination)
{
    const auto extension = destination.getFileExtension().toLowerCase();
    if (extension == ".wav") return std::make_unique<juce::WavAudioFormat>();
    if (extension == ".aif" || extension == ".aiff") return std::make_unique<juce::AiffAudioFormat>();
    if (extension == ".flac") return std::make_unique<juce::FlacAudioFormat>();
    return {};
}

// Backing buffers are deliberately over-allocated during device setup so a
// CoreAudio renegotiation never allocates in the realtime callback. An insert
// must nevertheless see exactly the callback span, never that capacity. A
// 64-frame callback passed as 4096 frames advances plug-in delay lines and
// oscillators at the wrong rate and exposes stale tail samples as noise.
void processEffectBlock(AudioEffectProcessor& processor,
                        juce::AudioBuffer<float>& backingBuffer,
                        int numSamples) noexcept
{
    const auto channels = juce::jmin(2, backingBuffer.getNumChannels());
    if (channels <= 0 || numSamples <= 0 || numSamples > backingBuffer.getNumSamples()) return;
    juce::AudioBuffer<float> callbackBlock(backingBuffer.getArrayOfWritePointers(), channels, numSamples);
    processor.processBlock(callbackBlock);
}

void processEffectBlock(AudioEffectProcessor& processor,
                        juce::AudioBuffer<float>& backingBuffer,
                        juce::MidiBuffer& midi,
                        int numSamples) noexcept
{
    const auto channels = juce::jmin(2, backingBuffer.getNumChannels());
    if (channels <= 0 || numSamples <= 0 || numSamples > backingBuffer.getNumSamples()) return;
    juce::AudioBuffer<float> callbackBlock(backingBuffer.getArrayOfWritePointers(), channels, numSamples);
    processor.processBlock(callbackBlock, midi);
}
}

// This is a deliberately fixed-size, single-producer/single-consumer bridge.
// The main device callback is its only writer and the virtual device callback
// is its only reader.  Keeping the storage here (rather than in AudioBuffer)
// makes both callback paths allocation-free and independent of their block
// sizes/clock cadence.
class AudioEngine::StreamOutputBridge final : public juce::AudioIODeviceCallback
{
public:
    static constexpr uint64_t capacityFrames = 1u << 16;
    static constexpr uint64_t frameMask = capacityFrames - 1;

    explicit StreamOutputBridge(AudioEngine& engineIn) : engine(engineIn) {}

    void write(const juce::AudioBuffer<float>& source, int numSamples) noexcept
    {
        if (numSamples <= 0 || source.getNumChannels() == 0) return;

        const auto read = readFrame.load(std::memory_order_acquire);
        auto write = writeFrame.load(std::memory_order_relaxed);
        const auto available = capacityFrames - (write - read);
        const auto count = juce::jmin<uint64_t>(static_cast<uint64_t>(numSamples), available);
        const auto* left = source.getReadPointer(0);
        const auto* right = source.getReadPointer(source.getNumChannels() > 1 ? 1 : 0);
        for (uint64_t sample = 0; sample < count; ++sample)
        {
            const auto destination = ((write + sample) & frameMask) * 2;
            frames[static_cast<size_t>(destination)] = left[sample];
            frames[static_cast<size_t>(destination + 1)] = right[sample];
        }
        write += count;
        writeFrame.store(write, std::memory_order_release);
    }

    void audioDeviceAboutToStart(juce::AudioIODevice*) override
    {
        started.store(true, std::memory_order_release);
    }

    void audioDeviceStopped() override
    {
        started.store(false, std::memory_order_release);
        // This also covers a virtual device disappearing while selected.  The
        // message thread can retain the selection, but UI state immediately
        // becomes "Not configured" until a successful reopen.
        engine.streamOutputConfigured.store(false, std::memory_order_release);
    }

    void audioDeviceIOCallbackWithContext(const float* const*, int,
                                          float* const* output, int outputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        if (outputChannels <= 0 || numSamples <= 0) return;

        auto read = readFrame.load(std::memory_order_relaxed);
        const auto write = writeFrame.load(std::memory_order_acquire);
        const auto count = juce::jmin<uint64_t>(static_cast<uint64_t>(numSamples), write - read);
        const auto muted = engine.streamMuted.load(std::memory_order_acquire);
        auto peak = 0.0f;
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float left = 0.0f, right = 0.0f;
            if (static_cast<uint64_t>(sample) < count)
            {
                const auto source = ((read + static_cast<uint64_t>(sample)) & frameMask) * 2;
                left = frames[static_cast<size_t>(source)];
                right = frames[static_cast<size_t>(source + 1)];
            }
            if (muted) left = right = 0.0f;
            if (output[0] != nullptr) output[0][sample] = left;
            if (outputChannels > 1 && output[1] != nullptr) output[1][sample] = right;
            for (int channel = 2; channel < outputChannels; ++channel)
                if (output[channel] != nullptr) output[channel][sample] = 0.0f;
            peak = juce::jmax(peak, juce::jmax(std::abs(left), std::abs(right)));
        }
        read += count;
        readFrame.store(read, std::memory_order_release);
        engine.streamPeak.store(peak, std::memory_order_relaxed);
    }

    bool hasStarted() const noexcept { return started.load(std::memory_order_acquire); }

private:
    AudioEngine& engine;
    alignas(64) std::array<float, capacityFrames * 2> frames {};
    alignas(64) std::atomic<uint64_t> readFrame { 0 };
    alignas(64) std::atomic<uint64_t> writeFrame { 0 };
    std::atomic<bool> started { false };
};

AudioEngine::AudioEngine(TrackDataModel* model)
    : dataModel(model)
{
    for (auto& callbackChannel : callbackInputByPhysicalChannel)
        callbackChannel.store(-1, std::memory_order_relaxed);
    for (auto& callbackChannel : callbackOutputByPhysicalChannel)
        callbackChannel.store(-1, std::memory_order_relaxed);
    for (auto& mapping : masterMidiCc)
        mapping.store(-1, std::memory_order_relaxed);
    graph.addNode(std::make_unique<GainNode>(1.0f));
    publishMasterFxRack(std::make_unique<TrackDataModel::FxRackSnapshot>());
    if (dataModel != nullptr) recordingSession = std::make_unique<RecordingSession>(*dataModel);
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

void AudioEngine::initialise()
{
    const auto stateFile = getAudioDeviceStateFile();
    const auto savedState = stateFile.existsAsFile() ? juce::parseXML(stateFile) : nullptr;
    if (savedState != nullptr)
    {
        // JUCE serialises these as audioInput/OutputDeviceName in its
        // DEVICESETUP XML (not the C++ AudioDeviceSetup member names).
        preferredInputDeviceName = savedState->getStringAttribute("audioInputDeviceName");
        preferredOutputDeviceName = savedState->getStringAttribute("audioOutputDeviceName");
    }
    deviceManager.initialise(2, 2, savedState.get(), true);
    if (savedState != nullptr)
        if (auto* type = deviceManager.getCurrentDeviceTypeObject(); type != nullptr)
            preferredDeviceWasUnavailable = (! preferredInputDeviceName.isEmpty()
                                              && ! type->getDeviceNames(true).contains(preferredInputDeviceName))
                                          || (! preferredOutputDeviceName.isEmpty()
                                              && ! type->getDeviceNames(false).contains(preferredOutputDeviceName));
    if (savedState == nullptr)
        enableAllAvailableInputChannels();
    deviceManager.addChangeListener(this);
    deviceManager.addAudioCallback(this);
    deviceManager.addMidiInputDeviceCallback({}, this);
    refreshMidiInputDevices();
    if (savedState != nullptr)
    {
        setMidiOutputDevice(savedState->getStringAttribute("midiOutputDeviceIdentifier"));
        softwareMonitoringEnabled.store(savedState->getBoolAttribute("softwareMonitoringEnabled", true), std::memory_order_release);
        focusedInputMonitoringEnabled.store(savedState->getBoolAttribute("focusedInputMonitoringEnabled", false), std::memory_order_release);
        liveLowLatencyMonitoringPreferred.store(savedState->getBoolAttribute("liveLowLatencyMonitoringPreferred", true), std::memory_order_release);
        studioPluginDelayCompensationEnabled.store(savedState->getBoolAttribute("studioPluginDelayCompensationEnabled", true), std::memory_order_release);
    }
    setMasterGain(1.0f);
    if (juce::SystemStats::getEnvironmentVariable("STUDIOFORGE_AUDIO_TRACE", {}).isNotEmpty())
        startAudioTrace();
    // Monitoring is deliberately opt-in. Opening a project must never route a
    // microphone to speakers before the performer explicitly enables LIVE.
    // Recording capture and input meters remain available while LIVE is off.
    setLivePerformanceEnabled(false);
    // Preserve the interface configuration selected by the user/Core Audio at
    // launch.  Forcing a 64-sample "live" buffer here used to tear down and
    // reopen the device session even in Studio mode.  Mature DAWs only change
    // this setting after an explicit Live/Low-Latency request, never as an
    // invisible side effect of opening a project.
}

void AudioEngine::shutdown()
{
    if (recordingSession != nullptr) recordingSession->stop();
    closeStreamOutputDevice();
    deviceManager.removeChangeListener(this);
    deviceManager.removeMidiInputDeviceCallback({}, this);
    for (const auto& identifier : managedMidiInputIdentifiers)
        deviceManager.setMidiInputDeviceEnabled(identifier, false);
    managedMidiInputIdentifiers.clear();
    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
    setMidiOutputDevice({});
    processingBuffer.clear();
}

void AudioEngine::setMasterGain(float gain) noexcept
{
    masterGain.store(juce::jlimit(0.0f, AudioGainRange::maximumChannelGain, gain));
    graph.setMasterGain(masterGain.load());
}

float AudioEngine::getMasterGain() const noexcept
{
    return masterGain.load();
}

AudioEngine::MasterOutputState AudioEngine::getMasterOutputState() const noexcept
{
    return { getMasterGain(), getMasterPan(), getMasterWidth(), isMasterMono(), isMasterDim(), isMasterMuted() };
}

void AudioEngine::beginMasterMidiLearn(MasterMidiTarget target) noexcept
{
    pendingMasterMidiLearn.store(static_cast<int>(target), std::memory_order_release);
}

int AudioEngine::getMasterMidiCc(MasterMidiTarget target) const noexcept
{
    return masterMidiCc[static_cast<size_t>(target)].load(std::memory_order_acquire);
}

void AudioEngine::setMasterOutputState(const MasterOutputState& state) noexcept
{
    setMasterGain(state.gain);
    setMasterPan(state.pan);
    setMasterWidth(state.width);
    setMasterMono(state.mono);
    setMasterDim(state.dim);
    setMasterMuted(state.muted);
}

void AudioEngine::setMasterPan(float pan) noexcept { masterPan.store(juce::jlimit(-1.0f, 1.0f, pan), std::memory_order_relaxed); }
float AudioEngine::getMasterPan() const noexcept { return masterPan.load(std::memory_order_relaxed); }
void AudioEngine::setMasterWidth(float width) noexcept { masterWidth.store(juce::jlimit(0.0f, 2.0f, width), std::memory_order_relaxed); }
float AudioEngine::getMasterWidth() const noexcept { return masterWidth.load(std::memory_order_relaxed); }
void AudioEngine::setMasterDim(bool enabled) noexcept { masterDim.store(enabled, std::memory_order_relaxed); }
bool AudioEngine::isMasterDim() const noexcept { return masterDim.load(std::memory_order_relaxed); }
void AudioEngine::setMasterMono(bool enabled) noexcept { masterMono.store(enabled, std::memory_order_relaxed); }
bool AudioEngine::isMasterMono() const noexcept { return masterMono.load(std::memory_order_relaxed); }
void AudioEngine::setMasterMuted(bool enabled) noexcept { masterMuted.store(enabled, std::memory_order_relaxed); }
bool AudioEngine::isMasterMuted() const noexcept { return masterMuted.load(std::memory_order_relaxed); }

void AudioEngine::setTempo(double newTempo) noexcept
{
    if (dataModel != nullptr)
        dataModel->setBpm(newTempo);
}

void AudioEngine::saveAudioDeviceState() const
{
    const auto state = deviceManager.createStateXml();
    if (state != nullptr)
    {
        state->setAttribute("midiOutputDeviceIdentifier", midiOutputDeviceIdentifier);
        state->setAttribute("softwareMonitoringEnabled", isSoftwareMonitoringEnabled());
        state->setAttribute("focusedInputMonitoringEnabled", isFocusedInputMonitoringEnabled());
        state->setAttribute("liveLowLatencyMonitoringPreferred", liveLowLatencyMonitoringPreferred.load(std::memory_order_acquire));
        state->setAttribute("studioPluginDelayCompensationEnabled", isStudioPluginDelayCompensationEnabled());
        state->writeTo(getAudioDeviceStateFile());
    }
}

juce::Array<juce::MidiDeviceInfo> AudioEngine::getAvailableMidiOutputDevices() const
{
    return juce::MidiOutput::getAvailableDevices();
}

juce::String AudioEngine::getMidiOutputDeviceIdentifier() const
{
    return midiOutputDeviceIdentifier;
}

juce::Result AudioEngine::setMidiOutputDevice(const juce::String& identifier)
{
    auto replacement = identifier.isEmpty() ? std::unique_ptr<juce::MidiOutput>() : juce::MidiOutput::openDevice(identifier);
    if (! identifier.isEmpty() && replacement == nullptr)
        return juce::Result::fail("Cannot open the selected MIDI output device");

    auto previous = std::move(midiOutput);
    midiOutput = std::move(replacement);
    midiOutputDeviceIdentifier = identifier;
    publishedMidiOutput.store(midiOutput.get(), std::memory_order_release);
    while (midiOutputCallbackUsers.load(std::memory_order_acquire) != 0)
        juce::Thread::sleep(1);
    return juce::Result::ok();
}

void AudioEngine::setMetronomeEnabled(bool enabled) noexcept
{
    metronomeEnabled.store(enabled, std::memory_order_release);
}

bool AudioEngine::isMetronomeEnabled() const noexcept
{
    return metronomeEnabled.load(std::memory_order_acquire);
}

void AudioEngine::setCountInMetronomeActive(bool active) noexcept
{
    countInMetronomeActive.store(active, std::memory_order_release);
}

void AudioEngine::setLivePerformanceEnabled(bool enabled) noexcept
{
    // Compatibility bridge for callers outside the workspace controller.
    workspaceMode.store(enabled ? WorkspaceMode::stage : WorkspaceMode::studio, std::memory_order_release);
    // LIVE is intended for performers first. Keep normal mixing intact outside
    // this mode, but avoid reported look-ahead/linear-phase delays while live.
    liveLowLatencyProtectionEnabled.store(enabled && liveLowLatencyMonitoringPreferred.load(std::memory_order_acquire), std::memory_order_release);
}

bool AudioEngine::isLivePerformanceEnabled() const noexcept
{
    return getWorkspaceMode() != WorkspaceMode::studio;
}

void AudioEngine::setSoftwareMonitoringEnabled(bool enabled) noexcept
{
    softwareMonitoringEnabled.store(enabled, std::memory_order_release);
}

bool AudioEngine::isSoftwareMonitoringEnabled() const noexcept
{
    return softwareMonitoringEnabled.load(std::memory_order_acquire);
}

void AudioEngine::setFocusedInputMonitoringEnabled(bool enabled) noexcept
{
    focusedInputMonitoringEnabled.store(enabled, std::memory_order_release);
}

bool AudioEngine::isFocusedInputMonitoringEnabled() const noexcept
{
    return focusedInputMonitoringEnabled.load(std::memory_order_acquire);
}

void AudioEngine::setLiveLowLatencyProtectionEnabled(bool enabled) noexcept
{
    liveLowLatencyMonitoringPreferred.store(enabled, std::memory_order_release);
    liveLowLatencyProtectionEnabled.store(isLivePerformanceEnabled() && enabled, std::memory_order_release);
}

void AudioEngine::setStudioPluginDelayCompensationEnabled(bool enabled) noexcept
{
    studioPluginDelayCompensationEnabled.store(enabled, std::memory_order_release);
}

bool AudioEngine::isStudioPluginDelayCompensationEnabled() const noexcept
{
    return studioPluginDelayCompensationEnabled.load(std::memory_order_acquire);
}

WorkspaceMode AudioEngine::getWorkspaceMode() const noexcept
{
    return workspaceMode.load(std::memory_order_acquire);
}

juce::Result AudioEngine::validateWorkspaceProfile(const WorkspaceProfile& profile) const
{
    if (profile.mode == WorkspaceMode::studio) return juce::Result::ok();
    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return juce::Result::fail("No active audio device for live workspace");
    return juce::Result::ok();
}

juce::Result AudioEngine::applyWorkspaceProfile(const WorkspaceProfile& profile)
{
    if (const auto validation = validateWorkspaceProfile(profile); validation.failed())
        return validation;
    // Modes are usable presets, not merely a different layout. A track which
    // is already explicitly In/Auto is brought live with a route guaranteed
    // to exist on the active interface. Tracks configured Off stay Off.
    // Master is the universal fallback: a two-output interface such as a
    // Scarlett Solo cannot satisfy a 3/4 CUE route and must never become
    // silently inaudible just because the user selected STAGE or STREAM.
    if (dataModel != nullptr)
    {
        // A monitored vocal must enter its track before inserts so Auto-Tune,
        // EQ, compression, sends and reverb all remain active while performing.
        // Dedicated cue hardware can still be fed from a normal post-FX bus;
        // the raw cue shortcut intentionally is not selected here.
        const auto destination = MonitorDestination::master;
        // A user deliberately entering a live workspace has already chosen
        // to monitor. Do not silently revoke that choice merely because an
        // aggregate/CoreAudio device happens to contain "speaker" in its
        // display name. Feedback mitigation belongs to gain and hardware
        // setup, not to an invisible mute in the signal path.
        dataModel->activateConfiguredMonitorRoutes(destination);
        restoreLiveVocalMonitorRoutes();
    }
    const auto previousMode = getWorkspaceMode();
    if (previousMode == WorkspaceMode::studio && profile.mode != WorkspaceMode::studio)
        if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr)
            studioBufferSizeBeforeLive = device->getCurrentBufferSizeSamples();

    liveLowLatencyProtectionEnabled.store(profile.protectsLivePluginLatency()
                                              && liveLowLatencyMonitoringPreferred.load(std::memory_order_acquire),
                                          std::memory_order_release);
    if (profile.mode == WorkspaceMode::studio && studioBufferSizeBeforeLive > 0)
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        if (setup.bufferSize != studioBufferSizeBeforeLive)
        {
            setup.bufferSize = studioBufferSizeBeforeLive;
            if (const auto error = deviceManager.setAudioDeviceSetup(setup, true); error.isNotEmpty())
                return juce::Result::fail(error);
        }
    }
    else if (profile.latency == LatencyPolicy::preferLowLatency)
    {
        if (const auto result = optimiseDeviceForLivePerformance(); result.failed()) return result;
    }
    workspaceMode.store(profile.mode, std::memory_order_release);
    return juce::Result::ok();
}

void AudioEngine::setPanicMute(bool enabled) noexcept { panicMuteEnabled.store(enabled, std::memory_order_release); }
bool AudioEngine::isPanicMuteEnabled() const noexcept { return panicMuteEnabled.load(std::memory_order_acquire); }
void AudioEngine::startAudioTrace()
{
    // Tracing can be enabled after the primary device's about-to-start callback
    // has already happened during application startup.
    if (const auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr)
        diagnosticTracer.setCoreAudioInputDevice(device->getName());
    diagnosticTracer.start();
    recordDeviceSessionEvent(1);
}

void AudioEngine::selectPluginTraceTarget(size_t trackIndex, size_t slot) noexcept
{
    pluginTraceTrackIndex.store(trackIndex < TrackDataModel::maxTracks ? static_cast<int>(trackIndex) : -1,
                                std::memory_order_release);
    pluginTraceSlot.store(slot < TrackDataModel::maxFxSlots ? static_cast<int>(slot) : -1,
                          std::memory_order_release);
    pluginTracePresent.store(0, std::memory_order_release);
    pluginTraceProcessed.store(0, std::memory_order_release);
    pluginTraceSkippedForLatency.store(0, std::memory_order_release);
}

void AudioEngine::clearPluginTraceTarget() noexcept
{
    pluginTraceTrackIndex.store(-1, std::memory_order_release);
    pluginTraceSlot.store(-1, std::memory_order_release);
}

void AudioEngine::recordDeviceSessionEvent(uint8_t kind) noexcept
{
    if (! diagnosticTracer.isEnabled())
        return;

    auto* device = deviceManager.getCurrentAudioDevice();
    const auto inputMask = device != nullptr ? device->getActiveInputChannels() : juce::BigInteger();
    const auto outputMask = device != nullptr ? device->getActiveOutputChannels() : juce::BigInteger();
    AudioDiagnosticTracer::Event event {};
    event.ms = static_cast<uint64_t>(juce::Time::getMillisecondCounterHiRes());
    event.kind = kind;
    event.deviceBufferSize = static_cast<uint32_t>(juce::jmax(0, device != nullptr ? device->getCurrentBufferSizeSamples() : 0));
    event.sampleRate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
    event.deviceHash = device != nullptr ? static_cast<uint32_t>(device->getName().hashCode()) : 0;
    event.inputChannels = static_cast<uint8_t>(juce::jlimit(0, 255, inputMask.countNumberOfSetBits()));
    event.outputChannels = static_cast<uint8_t>(juce::jlimit(0, 255, outputMask.countNumberOfSetBits()));
    for (int bit = 0; bit < 64; ++bit)
    {
        if (inputMask[bit]) event.inputMask |= (uint64_t { 1 } << bit);
        if (outputMask[bit]) event.outputMask |= (uint64_t { 1 } << bit);
    }
    diagnosticTracer.record(event);
}

void AudioEngine::recoverPreferredAudioDeviceAfterHotPlug()
{
    // Device-manager notifications are delivered on the message thread.  Do
    // not run this from audioDeviceIOCallback: opening a CoreAudio device can
    // block and causes the driver to stop/start the callback itself.
    if (recoveringPreferredAudioDevice.exchange(true, std::memory_order_acq_rel))
        return;

    auto* type = deviceManager.getCurrentDeviceTypeObject();
    if (type == nullptr)
    {
        recoveringPreferredAudioDevice.store(false, std::memory_order_release);
        return;
    }

    const auto inputNames = type->getDeviceNames(true);
    const auto outputNames = type->getDeviceNames(false);
    const auto inputReady = preferredInputDeviceName.isEmpty()
        || inputNames.contains(preferredInputDeviceName);
    const auto outputReady = preferredOutputDeviceName.isEmpty()
        || outputNames.contains(preferredOutputDeviceName);

    auto setup = deviceManager.getAudioDeviceSetup();

    // Wait until every explicitly selected side of the interface is present.
    // This avoids opening half of a USB interface while CoreAudio is still
    // enumerating it, which previously left a vocal route with zero inputs.
    if (! inputReady || ! outputReady)
    {
        preferredDeviceWasUnavailable = true;
        recoveringPreferredAudioDevice.store(false, std::memory_order_release);
        return;
    }

    // A selector action also produces a device-manager notification. Until
    // this distinction existed, choosing a USB interface was immediately
    // overwritten by the MacBook names loaded at launch. Only reclaim a
    // preference after we observed that exact saved interface disappear.
    if (! preferredDeviceWasUnavailable)
    {
        const auto inputChanged = setup.inputDeviceName.isNotEmpty()
                               && setup.inputDeviceName != preferredInputDeviceName;
        const auto outputChanged = setup.outputDeviceName.isNotEmpty()
                                && setup.outputDeviceName != preferredOutputDeviceName;
        if (inputChanged || outputChanged)
        {
            preferredInputDeviceName = setup.inputDeviceName;
            preferredOutputDeviceName = setup.outputDeviceName;
            saveAudioDeviceState();
        }
        recoveringPreferredAudioDevice.store(false, std::memory_order_release);
        return;
    }

    auto changed = false;
    if (preferredInputDeviceName.isNotEmpty()
        && setup.inputDeviceName != preferredInputDeviceName)
    {
        setup.inputDeviceName = preferredInputDeviceName;
        changed = true;
    }
    if (preferredOutputDeviceName.isNotEmpty()
        && setup.outputDeviceName != preferredOutputDeviceName)
    {
        setup.outputDeviceName = preferredOutputDeviceName;
        changed = true;
    }

    if (! changed)
    {
        recoveringPreferredAudioDevice.store(false, std::memory_order_release);
        return;
    }

    // Let JUCE choose the interface defaults when its channel count differs
    // from the fallback device. audioDeviceAboutToStart then rebuilds the
    // physical maps, prepares effects, and restores the live vocal monitor.
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;
    const auto error = deviceManager.setAudioDeviceSetup(setup, true);
    if (error.isEmpty())
        preferredDeviceWasUnavailable = false;
    recoveringPreferredAudioDevice.store(false, std::memory_order_release);
}

juce::Result AudioEngine::ensureCurrentStereoOutputPair()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return juce::Result::fail("No active audio output device");

    const auto outputCount = device->getOutputChannelNames().size();
    if (outputCount < 2)
        return juce::Result::fail("The active audio device has no stereo output pair");

    auto setup = deviceManager.getAudioDeviceSetup();
    auto requestedOutputs = setup.outputChannels;
    requestedOutputs.setBit(0);
    requestedOutputs.setBit(1);
    if (setup.outputChannels == requestedOutputs)
        return juce::Result::ok();

    // StudioForge has one physical Master bus. Choosing an interface must
    // therefore never leave CoreAudio with a stale/empty output mask from the
    // previously selected device; map Master to the interface's main 1/2.
    setup.outputChannels = requestedOutputs;
    setup.useDefaultOutputChannels = false;
    if (const auto error = deviceManager.setAudioDeviceSetup(setup, true); error.isNotEmpty())
        return juce::Result::fail(error);
    return juce::Result::ok();
}

juce::Result AudioEngine::ensureCurrentCueOutputPair()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return juce::Result::fail("No active audio output device");
    if (device->getOutputChannelNames().size() < 4)
        return juce::Result::fail("Cue requires a device with physical outputs 3/4");

    auto setup = deviceManager.getAudioDeviceSetup();
    auto requestedOutputs = setup.outputChannels;
    requestedOutputs.setBit(0);
    requestedOutputs.setBit(1);
    requestedOutputs.setBit(2);
    requestedOutputs.setBit(3);
    if (setup.outputChannels == requestedOutputs)
        return juce::Result::ok();

    setup.outputChannels = requestedOutputs;
    setup.useDefaultOutputChannels = false;
    if (const auto error = deviceManager.setAudioDeviceSetup(setup, true); error.isNotEmpty())
        return juce::Result::fail(error);
    return juce::Result::ok();
}

void AudioEngine::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // This is delivered on JUCE's message thread.  A device manager change is
    // a session boundary even when Core Audio doesn't issue an obvious stop;
    // keep it in the same trace as raw callback samples.
    recordDeviceSessionEvent(4);
    refreshMidiInputDevices();
    recoverPreferredAudioDeviceAfterHotPlug();
    // Do this after preference recovery, because it may reopen a fallback
    // device as the selected USB interface and must inspect that final device.
    ensureCurrentStereoOutputPair();
}

void AudioEngine::stopAudioTrace() { diagnosticTracer.stop(); }
void AudioEngine::markAudioTraceIssue() noexcept { diagnosticTracer.markIssue(); }
void AudioEngine::setStreamMuted(bool muted) noexcept { streamMuted.store(muted, std::memory_order_release); }
bool AudioEngine::isStreamMuted() const noexcept { return streamMuted.load(std::memory_order_acquire); }
float AudioEngine::getStreamPeak() const noexcept { return streamPeak.load(std::memory_order_relaxed); }

juce::StringArray AudioEngine::getAvailableAudioOutputDevices() const
{
    juce::StringArray names;
    if (auto* type = deviceManager.getCurrentDeviceTypeObject(); type != nullptr)
        names = type->getDeviceNames(false);
    return names;
}

juce::Result AudioEngine::selectStreamOutputDevice(const juce::String& deviceName)
{
    // A selection becomes connected only after the second device has started
    // its callback.  This avoids showing a false-positive status for an
    // unplugged/permission-denied virtual device.
    closeStreamOutputDevice();
    if (deviceName.isEmpty())
    {
        streamOutputDeviceName.clear();
        return juce::Result::ok();
    }
    if (! getAvailableAudioOutputDevices().contains(deviceName))
        return juce::Result::fail("Selected stream output device is unavailable");
    streamOutputDeviceName = deviceName;
    return openStreamOutputDevice(deviceName);
}

juce::String AudioEngine::getStreamOutputDevice() const { return streamOutputDeviceName; }
bool AudioEngine::isStreamOutputConfigured() const noexcept { return streamOutputConfigured.load(std::memory_order_acquire); }

juce::Result AudioEngine::validateMonitorRoute(size_t trackIndex, MonitorDestination destination,
                                               bool allowSpeakerRisk) const
{
    if (dataModel == nullptr || trackIndex >= dataModel->getTrackCount())
        return juce::Result::fail("Monitor track is unavailable");
    if (destination == MonitorDestination::off)
        return juce::Result::ok();
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return juce::Result::fail("No active audio output device");
    if (destination == MonitorDestination::cue)
    {
        const auto setup = deviceManager.getAudioDeviceSetup();
        if (device->getOutputChannelNames().size() < 4
            || ! setup.outputChannels[2] || ! setup.outputChannels[3])
            return juce::Result::fail("Cue requires enabled physical output channels 3/4");
    }
    if (destination == MonitorDestination::streamOnly)
    {
        if (! isStreamOutputConfigured())
            return juce::Result::fail("Stream Output is not connected");
        // A single CoreAudio device used as both the main input path and the
        // virtual stream sink is an immediate digital-loop risk. Aggregate
        // topology needs platform IDs to refine this further, so it remains
        // conservatively blocked here.
        if (device->getName() == streamOutputDeviceName)
            return juce::Result::fail("Potential feedback loop: input device is the Stream Output device");
    }
    juce::ignoreUnused(allowSpeakerRisk);
    return juce::Result::ok();
}

juce::Result AudioEngine::enableTrackMonitor(size_t trackIndex, MonitorDestination destination,
                                             bool allowSpeakerRisk)
{
    // Configure the primary device before validating the Cue route. The
    // device callback rebuilds its compact physical-to-callback map during
    // this reconfiguration; validation intentionally uses the requested
    // physical mask so a harmless callback restart cannot reject the route.
    if (destination == MonitorDestination::cue)
        if (const auto cue = ensureCurrentCueOutputPair(); cue.failed())
            return cue;
    if (const auto validation = validateMonitorRoute(trackIndex, destination, allowSpeakerRisk); validation.failed())
        return validation;
    dataModel->setTrackMonitorDestination(trackIndex, destination);
    dataModel->setTrackMonitorPermission(trackIndex, destination != MonitorDestination::off);
    return juce::Result::ok();
}

void AudioEngine::disableTrackMonitor(size_t trackIndex) noexcept
{
    if (dataModel != nullptr)
        dataModel->setTrackMonitorPermission(trackIndex, false);
}

juce::Result AudioEngine::openStreamOutputDevice(const juce::String& deviceName)
{
    auto* type = deviceManager.getCurrentDeviceTypeObject();
    auto* mainDevice = deviceManager.getCurrentAudioDevice();
    if (type == nullptr || mainDevice == nullptr)
        return juce::Result::fail("No active audio device type for stream output");

    std::unique_ptr<juce::AudioIODevice> device(type->createDevice(deviceName, {}));
    if (device == nullptr)
        return juce::Result::fail("Cannot create the selected stream output device");

    juce::BigInteger outputChannels;
    const auto channelCount = device->getOutputChannelNames().size();
    if (channelCount <= 0)
        return juce::Result::fail("Selected stream output device has no output channels");
    outputChannels.setRange(0, juce::jmin(2, channelCount), true);

    auto sampleRate = mainDevice->getCurrentSampleRate();
    const auto sampleRates = device->getAvailableSampleRates();
    if (! sampleRates.isEmpty() && ! sampleRates.contains(sampleRate))
        sampleRate = sampleRates[0];
    auto bufferSize = mainDevice->getCurrentBufferSizeSamples();
    const auto bufferSizes = device->getAvailableBufferSizes();
    if (! bufferSizes.isEmpty() && ! bufferSizes.contains(bufferSize))
        bufferSize = bufferSizes[0];

    if (const auto error = device->open({}, outputChannels, sampleRate, bufferSize); error.isNotEmpty())
        return juce::Result::fail("Cannot open stream output device: " + error);

    auto bridge = std::make_unique<StreamOutputBridge>(*this);
    device->start(bridge.get());
    if (! bridge->hasStarted())
    {
        device->close();
        return juce::Result::fail("Stream output device did not start its audio callback");
    }

    streamOutputBridge = std::move(bridge);
    streamOutputDevice = std::move(device);
    publishedStreamOutputBridge.store(streamOutputBridge.get(), std::memory_order_release);
    streamOutputConfigured.store(true, std::memory_order_release);
    return juce::Result::ok();
}

void AudioEngine::closeStreamOutputDevice()
{
    streamOutputConfigured.store(false, std::memory_order_release);
    publishedStreamOutputBridge.store(nullptr, std::memory_order_release);
    if (streamOutputDevice != nullptr)
    {
        streamOutputDevice->stop();
        streamOutputDevice->close();
    }
    streamOutputDevice.reset();
    if (streamOutputBridge != nullptr)
        retiredStreamOutputBridges.push_back(std::move(streamOutputBridge));
    streamPeak.store(0.0f, std::memory_order_relaxed);
}

void AudioEngine::publishStreamOutput(const juce::AudioBuffer<float>& source, int numSamples) noexcept
{
    if (auto* bridge = publishedStreamOutputBridge.load(std::memory_order_acquire); bridge != nullptr)
        bridge->write(source, numSamples);
}

bool AudioEngine::isLiveLowLatencyProtectionEnabled() const noexcept
{
    return liveLowLatencyProtectionEnabled.load(std::memory_order_acquire);
}

void AudioEngine::setFocusedMonitorTrack(TrackId track) noexcept
{
    focusedMonitorTrackId.store(track.value, std::memory_order_release);
}

juce::Result AudioEngine::optimiseDeviceForLivePerformance()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return juce::Result::fail("No active audio device");

    const auto targetBufferSize = preferredLiveBufferSize(device->getAvailableBufferSizes(),
                                                           device->getCurrentBufferSizeSamples());
    if (targetBufferSize <= 0 || targetBufferSize == device->getCurrentBufferSizeSamples())
        return juce::Result::ok();

    auto setup = deviceManager.getAudioDeviceSetup();
    setup.bufferSize = targetBufferSize;
    if (const auto error = deviceManager.setAudioDeviceSetup(setup, true); error.isNotEmpty())
        return juce::Result::fail(error);

    prepareActiveEffects();
    return juce::Result::ok();
}

int AudioEngine::getRecommendedLiveBufferSize() noexcept
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device != nullptr ? preferredLiveBufferSize(device->getAvailableBufferSizes(),
                                                        device->getCurrentBufferSizeSamples()) : 0;
}

bool AudioEngine::needsLiveBufferOptimisation() noexcept
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr) return false;
    const auto recommended = getRecommendedLiveBufferSize();
    return recommended > 0 && recommended < device->getCurrentBufferSizeSamples();
}

juce::Result AudioEngine::startRecording(size_t trackIndex, const juce::File& destination,
                                         double timelineStartSample)
{
    return startRecordings({ { trackIndex, destination } }, timelineStartSample);
}

juce::Result AudioEngine::startRecordings(const std::vector<AudioRecordingTarget>& targets,
                                          double timelineStartSample)
{
    if (const auto validation = validateRecordingTargets(targets); validation.failed())
        return validation;

    auto* device = deviceManager.getCurrentAudioDevice();
    const auto sampleRate = device != nullptr ? device->getCurrentSampleRate() : dataModel->getSampleRate();
    const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : processingBuffer.getNumSamples();

    std::vector<RecordingSession::TrackTarget> recordingTargets;
    recordingTargets.reserve(targets.size());
    for (const auto& target : targets)
    {
        const auto physicalInputChannel = dataModel->getTrack(target.trackIndex).inputChannel.load(std::memory_order_relaxed);
        const auto inputChannelCount = dataModel->getTrack(target.trackIndex).inputChannelCount.load(std::memory_order_relaxed);
        const auto callbackInputChannel = activeCallbackInputCount.load(std::memory_order_acquire) > 0
            ? callbackInputForPhysicalChannel(physicalInputChannel) : physicalInputChannel;
        recordingTargets.push_back({ target.trackIndex, target.destination, callbackInputChannel, inputChannelCount });
    }

    const auto startSample = timelineStartSample >= 0.0 ? timelineStartSample : dataModel->getPlayheadPosition();
    const auto result = recordingSession->startMany(recordingTargets, startSample, sampleRate, blockSize);
    if (result.wasOk())
    {
        recordingDeviceStopMs.store(0, std::memory_order_release);
        for (const auto& target : targets)
            dataModel->setTrackArmed(target.trackIndex, true);
    }
    return result;
}

juce::Result AudioEngine::validateRecordingTargets(const std::vector<AudioRecordingTarget>& targets) const
{
    if (recordingSession == nullptr || dataModel == nullptr)
        return juce::Result::fail("Audio engine has no project model");
    if (targets.empty())
        return juce::Result::fail("Select at least one audio track to record");

    const auto* device = deviceManager.getCurrentAudioDevice();
    // Direct callback hosts (offline validation and automated tests) do not
    // register their device with AudioDeviceManager. Their prepared realtime
    // buffers still describe a valid input configuration.
    const auto hasPhysicalInputMap = activeCallbackInputCount.load(std::memory_order_acquire) > 0;
    const auto inputChannels = hasPhysicalInputMap ? activeCallbackInputCount.load(std::memory_order_acquire)
                                                   : processingBuffer.getNumChannels();
    if (inputChannels <= 0 || processingBuffer.getNumSamples() <= 0)
        return juce::Result::fail("Enable at least one audio input before recording");

    for (const auto& target : targets)
    {
        if (target.trackIndex >= dataModel->getTrackCount()
            || dataModel->getTrack(target.trackIndex).type != TrackType::audio)
            return juce::Result::fail("Recording requires audio tracks");

        const auto& track = dataModel->getTrack(target.trackIndex);
        const auto inputChannel = track.inputChannel.load(std::memory_order_relaxed);
        const auto inputChannelCount = track.inputChannelCount.load(std::memory_order_relaxed);
        if (inputChannel < 0 || inputChannelCount < 1 || inputChannelCount > 2
            || (hasPhysicalInputMap && ! isPhysicalInputRouteAvailable(inputChannel, inputChannelCount))
            || (! hasPhysicalInputMap && inputChannel + inputChannelCount > inputChannels))
            return juce::Result::fail("Track input is not enabled on the selected audio device");
    }
    return juce::Result::ok();
}

juce::Result AudioEngine::stopRecording()
{
    if (recordingSession == nullptr) return juce::Result::ok();
    clearRecordingCaptureRange();
    const auto result = recordingSession->stop();
    if (dataModel != nullptr)
        for (size_t index = 0; index < dataModel->getTrackCount(); ++index) dataModel->setTrackArmed(index, false);
    return result;
}
bool AudioEngine::isRecording() const noexcept { return recordingSession != nullptr && recordingSession->isRecording(); }

bool AudioEngine::consumeRecordingInterruptedByDeviceDisconnect() noexcept
{
    auto stoppedAt = recordingDeviceStopMs.load(std::memory_order_acquire);
    if (stoppedAt == 0)
        return false;

    const auto now = static_cast<uint64_t>(juce::Time::getMillisecondCounterHiRes());
    if (now - stoppedAt < recordingDeviceRecoveryGraceMs)
        return false;

    return recordingDeviceStopMs.compare_exchange_strong(stoppedAt, 0, std::memory_order_acq_rel);
}

juce::Result AudioEngine::startMidiRecording(size_t trackIndex)
{
    if (dataModel == nullptr || trackIndex >= dataModel->getTrackCount())
        return juce::Result::fail("MIDI recording track is invalid");
    const auto& track = dataModel->getTrack(trackIndex);
    if (track.type != TrackType::instrument && track.type != TrackType::externalMidi)
        return juce::Result::fail("MIDI recording requires an instrument or external MIDI track");
    if (midiRecordingActive.load(std::memory_order_acquire))
        return juce::Result::fail("MIDI recording is already active");

    recordedMidiFifo.reset();
    droppedMidiRecordingEvents.store(0, std::memory_order_relaxed);
    midiRecordingTrack = track.id;
    midiRecordingStartSample = dataModel->getPlayheadPosition();
    midiRecordingActive.store(true, std::memory_order_release);
    dataModel->setTrackArmed(trackIndex, true);
    return juce::Result::ok();
}

MidiClipId AudioEngine::stopMidiRecording()
{
    if (! midiRecordingActive.exchange(false, std::memory_order_acq_rel) || dataModel == nullptr)
        return {};

    while (midiRecordingCallbackUsers.load(std::memory_order_acquire) != 0)
        juce::Thread::sleep(1);

    std::vector<RecordedMidiEvent> events;
    events.reserve(static_cast<size_t>(recordedMidiFifo.getNumReady()));
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    recordedMidiFifo.prepareToRead(recordedMidiCapacity, start1, size1, start2, size2);
    const auto copyRange = [this, &events] (int start, int count)
    {
        for (int offset = 0; offset < count; ++offset)
            events.push_back(recordedMidiEvents[static_cast<size_t>(start + offset)]);
    };
    copyRange(start1, size1);
    copyRange(start2, size2);
    recordedMidiFifo.finishedRead(size1 + size2);
    if (events.empty())
        return {};

    struct OpenNote { int pitch; int channel; float velocity; double start; };
    std::vector<OpenNote> openNotes;
    std::vector<MidiNoteEvent> notes;
    const auto stopSample = dataModel->getPlayheadPosition();
    for (const auto& event : events)
    {
        if (event.noteOn)
        {
            openNotes.push_back({ event.pitch, event.channel, event.velocity, event.samplePosition });
            continue;
        }

        const auto match = std::find_if(openNotes.rbegin(), openNotes.rend(), [&event] (const OpenNote& open)
        {
            return open.pitch == event.pitch && open.channel == event.channel;
        });
        if (match == openNotes.rend())
            continue;
        notes.push_back({ {}, match->pitch, match->velocity,
                          match->start - midiRecordingStartSample,
                          event.samplePosition - match->start, match->channel });
        openNotes.erase(std::next(match).base());
    }
    for (const auto& open : openNotes)
        notes.push_back({ {}, open.pitch, open.velocity, open.start - midiRecordingStartSample,
                          stopSample - open.start, open.channel });

    return dataModel->addMidiRecording(midiRecordingTrack, midiRecordingStartSample, std::move(notes));
}

bool AudioEngine::isMidiRecording() const noexcept
{
    return midiRecordingActive.load(std::memory_order_acquire);
}

void AudioEngine::setRecordingCaptureRange(double startSample, double endSample) noexcept
{
    const auto start = juce::jmax(0.0, juce::jmin(startSample, endSample));
    const auto end = juce::jmax(0.0, juce::jmax(startSample, endSample));
    recordingCaptureStartSample.store(start, std::memory_order_release);
    recordingCaptureEndSample.store(end, std::memory_order_release);
    recordingCaptureRangeActive.store(end > start, std::memory_order_release);
}

void AudioEngine::clearRecordingCaptureRange() noexcept
{
    recordingCaptureRangeActive.store(false, std::memory_order_release);
}

juce::Result AudioEngine::renderOfflineWav(const juce::File& destination, double sampleRate,
                                           int blockSize, bool useCycle)
{
    OfflineRenderOptions options;
    options.sampleRate = sampleRate;
    options.blockSize = blockSize;
    options.useCycle = useCycle;
    return renderOfflineAudio(destination.withFileExtension(".wav"), options);
}

juce::Result AudioEngine::renderOfflineStems(const juce::File& directory,
                                             const OfflineRenderOptions& options)
{
    if (directory == juce::File {} || (! directory.exists() && ! directory.createDirectory()))
        return juce::Result::fail("Cannot create stem export directory");

    const auto state = dataModel != nullptr ? dataModel->createProjectState() : ProjectState {};
    if (state.tracks.empty())
        return juce::Result::fail("Project has no tracks to export");

    struct StagedStem
    {
        juce::File temporaryFile;
        juce::File destinationFile;
    };

    std::vector<StagedStem> stagedStems;
    std::vector<std::unique_ptr<juce::AudioFormatWriter>> writers(state.tracks.size());
    const auto discardStagedStems = [&stagedStems]
    {
        for (const auto& stem : stagedStems)
            stem.temporaryFile.deleteFile();
    };
    auto selectedCount = 0;
    for (size_t index = 0; index < state.tracks.size(); ++index)
    {
        if (options.trackExportFilter != nullptr && ! options.trackExportFilter(index))
            continue;
        auto name = state.tracks[index].name.replaceCharacters("/\\:*?\"<>|", "_________").trim();
        if (name.isEmpty()) name = "Track " + juce::String(static_cast<int>(index + 1));
        const auto destinationFile = directory.getNonexistentChildFile(name, ".wav", false);
        const auto temporaryFile = destinationFile.getSiblingFile(
            "." + destinationFile.getFileNameWithoutExtension()
                + ".studioforge-stem.tmp" + destinationFile.getFileExtension());
        temporaryFile.deleteFile();
        auto stream = temporaryFile.createOutputStream();
        if (stream == nullptr)
        {
            discardStagedStems();
            return juce::Result::fail("Cannot create stem file: " + destinationFile.getFileName());
        }
        auto* raw = stream.release();
        juce::WavAudioFormat format;
        writers[index].reset(format.createWriterFor(raw, options.sampleRate, 2,
                                                    juce::jlimit(16, 32, options.bitDepth), {}, 0));
        if (writers[index] == nullptr)
        {
            delete raw;
            temporaryFile.deleteFile();
            discardStagedStems();
            return juce::Result::fail("Cannot initialise stem writer: " + destinationFile.getFileName());
        }
        stagedStems.push_back({ temporaryFile, destinationFile });
        ++selectedCount;
    }
    if (selectedCount == 0)
        return juce::Result::fail("No tracks are selected for stem export");

    const auto temporaryMix = directory.getChildFile(".studioforge-stem-render.tmp.wav");
    auto stemOptions = options;
    stemOptions.trackBlockCallback = [&writers, filter = options.trackExportFilter]
        (size_t index, const juce::AudioBuffer<float>& buffer, int count)
    {
        if (index >= writers.size())
            return;
        if (filter != nullptr && ! filter(index))
            return;
        writers[index]->writeFromAudioSampleBuffer(buffer, 0, count);
    };
    const auto result = renderOfflineAudio(temporaryMix, stemOptions);
    // Close every stem before promotion: the files are intentionally invisible
    // until the complete render succeeds.
    writers.clear();
    temporaryMix.deleteFile();
    if (result.failed())
    {
        discardStagedStems();
        return result;
    }

    for (const auto& stem : stagedStems)
    {
        if (! stem.temporaryFile.replaceFileIn(stem.destinationFile))
        {
            discardStagedStems();
            return juce::Result::fail("Cannot finalise stem file: " + stem.destinationFile.getFileName());
        }
    }

    return juce::Result::ok();
}

juce::Result AudioEngine::renderOfflineAudio(const juce::File& destination,
                                             const OfflineRenderOptions& options)
{
    if (dataModel == nullptr || options.sampleRate <= 0.0 || options.blockSize <= 0)
        return juce::Result::fail("Offline render configuration is invalid");
    if (isRecording()) return juce::Result::fail("Stop recording before offline render");
    auto format = createOfflineFormat(destination);
    if (format == nullptr)
        return juce::Result::fail("Unsupported bounce format. Choose WAV, AIFF, or FLAC.");
    const auto bitDepth = juce::jlimit(16, 32, options.bitDepth);
    const auto projectState = dataModel->createProjectState();
    double endSample = 0.0;
    for (const auto& track : projectState.tracks)
        for (const auto& clip : track.clips)
            endSample = juce::jmax(endSample, clip.startSample + clip.durationSamples);
    for (const auto& clip : projectState.midiClips)
        for (const auto& note : clip.notes)
            endSample = juce::jmax(endSample, clip.startSample + note.startSample + note.durationSamples);
    if (options.useCycle && dataModel->isCycleActive()) endSample = dataModel->getCycleEndSample();
    if (endSample <= 0.0) return juce::Result::fail("Project has no audio to render");

    // Render beside the destination and promote only a complete file. This
    // preserves an existing bounce if the disk fills up or a writer fails.
    const auto temporaryDestination = destination.getSiblingFile(
        "." + destination.getFileNameWithoutExtension() + ".studioforge-render.tmp" + destination.getFileExtension());
    temporaryDestination.deleteFile();
    auto stream = temporaryDestination.createOutputStream();
    if (stream == nullptr) return juce::Result::fail("Cannot create offline render file");
    auto* rawStream = stream.release();
    std::unique_ptr<juce::AudioFormatWriter> writer(format->createWriterFor(rawStream, options.sampleRate, 2, bitDepth, {}, 0));
    if (writer == nullptr)
    {
        delete rawStream;
        temporaryDestination.deleteFile();
        return juce::Result::fail("Cannot create offline WAV writer");
    }

    const auto previousRate = dataModel->getSampleRate();
    const auto previousPlayhead = dataModel->getPlayheadPosition();
    const auto wasPlaying = dataModel->isPlaying();
    const auto cycleWasActive = dataModel->isCycleActive();
    const auto cycleStart = dataModel->getCycleStartSample();
    const auto cycleEnd = dataModel->getCycleEndSample();
    const auto previousProjectRevision = dataModel->getProjectRevision();
    const auto restoreTransport = [this, previousRate, previousPlayhead, wasPlaying,
                                   cycleWasActive, cycleStart, cycleEnd, previousProjectRevision]
    {
        dataModel->setSampleRate(previousRate);
        auto* device = deviceManager.getCurrentAudioDevice();
        const auto restoreRate = previousRate > 0.0 ? previousRate
            : (device != nullptr ? device->getCurrentSampleRate() : 44100.0);
        const auto restoreBlock = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
        for (size_t trackIndex = 0; trackIndex < dataModel->getTrackCount(); ++trackIndex)
            prepareRack(dataModel->getFxRackSnapshot(trackIndex), restoreRate, restoreBlock);
        for (const auto& bus : dataModel->getBuses())
            prepareRack(dataModel->getBusFxRackSnapshot(bus.id), restoreRate, restoreBlock);
        prepareRack(getMasterFxRackSnapshot(), restoreRate, restoreBlock);
        dataModel->setPlayheadPosition(previousPlayhead);
        dataModel->setPlaying(wasPlaying);
        if (cycleWasActive) dataModel->setCycle(cycleStart, cycleEnd); else dataModel->clearCycle();
        dataModel->restoreProjectRevision(previousProjectRevision);
    };
    dataModel->setSampleRate(options.sampleRate);
    // An offline render may use a different format from the current audio
    // device. Prepare every insert for this exact render before the first
    // callback, including plug-ins instantiated while no device is open.
    for (size_t trackIndex = 0; trackIndex < dataModel->getTrackCount(); ++trackIndex)
        prepareRack(dataModel->getFxRackSnapshot(trackIndex), options.sampleRate, options.blockSize);
    for (const auto& bus : dataModel->getBuses())
        prepareRack(dataModel->getBusFxRackSnapshot(bus.id), options.sampleRate, options.blockSize);
    prepareRack(getMasterFxRackSnapshot(), options.sampleRate, options.blockSize);
    if (! options.useCycle) dataModel->clearCycle();
    dataModel->setPlayheadPosition(options.useCycle && cycleWasActive ? cycleStart : 0.0);
    dataModel->setPlaying(true);
    processingBuffer.setSize(2, options.blockSize, false, true, true);
    directPathBuffer.setSize(2, options.blockSize, false, true, true);
    directPathDelayBuffer.setSize(2, trackLatencyCompensationCapacity, false, true, true);
    directPathDelayBuffer.clear();
    directPathDelayWrite = 0;
    liveKeyReferenceBuffer.setSize(2, options.blockSize, false, true, true);
    for (auto& buffer : trackBuffers) buffer.setSize(2, options.blockSize, false, true, true);
    for (size_t index = 0; index < trackLatencyDelayBuffers.size(); ++index)
    {
        trackLatencyDelayBuffers[index].setSize(2, trackLatencyCompensationCapacity, false, true, true);
        trackLatencyDelayBuffers[index].clear();
        trackLatencyDelayWrite[index] = 0;
    }
    for (auto& buffer : busBuffers) buffer.setSize(2, options.blockSize, false, true, true);
    for (size_t index = 0; index < busLatencyDelayBuffers.size(); ++index)
    {
        busLatencyDelayBuffers[index].setSize(2, trackLatencyCompensationCapacity, false, true, true);
        busLatencyDelayBuffers[index].clear();
        busLatencyDelayWrite[index] = 0;
    }
    for (auto& midi : trackMidiBuffers) midi.ensureSize(MidiEventBuffer::capacity * 4);
    juce::AudioBuffer<float> renderBuffer(2, options.blockSize);
    float* outputs[] { renderBuffer.getWritePointer(0), renderBuffer.getWritePointer(1) };
    const auto totalSamples = static_cast<int64_t>(std::ceil(endSample));
    int64_t rendered = 0;
    auto writeFailed = false;
    while (rendered < totalSamples)
    {
        const auto count = static_cast<int>(juce::jmin<int64_t>(options.blockSize, totalSamples - rendered));
        audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, count, {});
        if (options.trackBlockCallback != nullptr)
            for (size_t trackIndex = 0; trackIndex < projectState.tracks.size(); ++trackIndex)
                options.trackBlockCallback(trackIndex, trackBuffers[trackIndex], count);
        if (! writer->writeFromAudioSampleBuffer(renderBuffer, 0, count))
        {
            writeFailed = true;
            break;
        }
        rendered += count;
    }
    // Close the writer before replacement so all format buffers are flushed.
    writer.reset();
    restoreTransport();
    if (writeFailed)
    {
        temporaryDestination.deleteFile();
        return juce::Result::fail("Offline audio write failed");
    }
    if (! temporaryDestination.replaceFileIn(destination))
    {
        temporaryDestination.deleteFile();
        return juce::Result::fail("Cannot finalise offline render file");
    }
    return juce::Result::ok();
}

double AudioEngine::getTempo() const noexcept
{
    return dataModel != nullptr ? dataModel->getBpm() : 120.0;
}

void AudioEngine::setPlaybackState(bool shouldPlay) noexcept
{
    if (shouldPlay)
        restartAudioDeviceIfCallbackStalled();
    if (dataModel != nullptr)
        dataModel->setPlaying(shouldPlay);
}

void AudioEngine::restartAudioDeviceIfCallbackStalled()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;

    const auto now = static_cast<uint64_t>(juce::Time::getMillisecondCounterHiRes());
    const auto last = lastAudioCallbackMs.load(std::memory_order_acquire);
    // A healthy CoreAudio stream calls us continuously (normally every 1–11
    // ms). A generous window prevents a restart during normal UI work while
    // still recovering the dead-but-selected device state exposed by trace.
    if (last != 0 && now - last < 750)
        return;
    if (restartingStalledAudioDevice.exchange(true, std::memory_order_acq_rel))
        return;

    deviceManager.closeAudioDevice();
    deviceManager.restartLastAudioDevice();
    restartingStalledAudioDevice.store(false, std::memory_order_release);
}

bool AudioEngine::isPlaying() const noexcept
{
    return dataModel != nullptr && dataModel->isPlaying();
}

juce::AudioDeviceManager& AudioEngine::getAudioDeviceManager() noexcept
{
    return deviceManager;
}

juce::Array<int> AudioEngine::getActiveAudioInputChannels() const
{
    juce::Array<int> channels;
    const auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return channels;

    const auto active = device->getActiveInputChannels();
    for (int channel = 0; channel <= active.getHighestBit(); ++channel)
        if (active[channel])
            channels.add(channel);
    return channels;
}

void AudioEngine::refreshMidiInputDevices()
{
    // LIVE defaults to accepting every connected controller. The empty
    // callback identifier receives events from all enabled inputs; this owns
    // enabling a controller that appears after StudioForge has launched.
    const auto devices = juce::MidiInput::getAvailableDevices();
    juce::StringArray presentIdentifiers;
    for (const auto& device : devices)
    {
        presentIdentifiers.add(device.identifier);
        if (! deviceManager.isMidiInputDeviceEnabled(device.identifier))
            deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
    }

    for (const auto& identifier : managedMidiInputIdentifiers)
        if (! presentIdentifiers.contains(identifier))
            deviceManager.setMidiInputDeviceEnabled(identifier, false);
    managedMidiInputIdentifiers = std::move(presentIdentifiers);
}

juce::Result AudioEngine::enableAllAvailableInputChannels()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return juce::Result::fail("No active audio device");

    const auto inputCount = device->getInputChannelNames().size();
    if (inputCount <= 0)
        return juce::Result::fail("The active audio device has no inputs");

    auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.inputChannels.countNumberOfSetBits() == inputCount)
        return juce::Result::ok();
    setup.inputChannels.clear();
    for (int channel = 0; channel < inputCount; ++channel)
        setup.inputChannels.setBit(channel);
    if (const auto error = deviceManager.setAudioDeviceSetup(setup, true); error.isNotEmpty())
        return juce::Result::fail(error);
    return juce::Result::ok();
}

juce::Result AudioEngine::selectStereoOutputPair(int firstPhysicalOutputChannel)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return juce::Result::fail("No active audio device");

    const auto outputCount = device->getOutputChannelNames().size();
    if (firstPhysicalOutputChannel < 0 || firstPhysicalOutputChannel + 1 >= outputCount)
        return juce::Result::fail("The selected stereo output pair is unavailable");

    auto setup = deviceManager.getAudioDeviceSetup();
    juce::BigInteger requestedOutputs;
    requestedOutputs.setBit(firstPhysicalOutputChannel);
    requestedOutputs.setBit(firstPhysicalOutputChannel + 1);
    if (setup.outputChannels == requestedOutputs)
        return juce::Result::ok();

    setup.outputChannels = requestedOutputs;
    if (const auto error = deviceManager.setAudioDeviceSetup(setup, true); error.isNotEmpty())
        return juce::Result::fail(error);
    return juce::Result::ok();
}

float AudioEngine::getTrackPeak(size_t trackIndex) const noexcept
{
    if ((! isPlaying() && ! isLivePerformanceEnabled()) || trackIndex >= trackPeaks.size())
        return 0.0f;
    return trackPeaks[trackIndex].load(std::memory_order_relaxed);
}

float AudioEngine::getTrackInputPeak(size_t trackIndex) const noexcept
{
    return trackIndex < trackInputPeaks.size()
        ? trackInputPeaks[trackIndex].load(std::memory_order_relaxed) : 0.0f;
}

float AudioEngine::getBusPeak(size_t busIndex) const noexcept
{
    if (! isPlaying() || busIndex >= busPeaks.size())
        return 0.0f;
    return busPeaks[busIndex].load(std::memory_order_relaxed);
}

float AudioEngine::getMasterPeak() const noexcept
{
    return isPlaying() ? masterPeak.load(std::memory_order_relaxed) : 0.0f;
}

float AudioEngine::getMasterLeftPeak() const noexcept
{
    return isPlaying() ? masterLeftPeak.load(std::memory_order_relaxed) : 0.0f;
}

float AudioEngine::getMasterRightPeak() const noexcept
{
    return isPlaying() ? masterRightPeak.load(std::memory_order_relaxed) : 0.0f;
}

AudioEngine::RealtimeDiagnostics AudioEngine::getRealtimeDiagnostics() const noexcept
{
    return { activeSampleRate.load(std::memory_order_relaxed),
             activeBufferSize.load(std::memory_order_relaxed),
             activeInputLatencySamples.load(std::memory_order_relaxed),
             activeOutputLatencySamples.load(std::memory_order_relaxed),
             getEstimatedLivePluginLatencySamples(),
             getLiveProtectedPluginCount(),
             callbackLoad.load(std::memory_order_relaxed),
             callbackOverloadCount.load(std::memory_order_relaxed),
             droppedMidiInputEvents.load(std::memory_order_relaxed),
             droppedMidiRecordingEvents.load(std::memory_order_relaxed) };
}

int AudioEngine::getEstimatedLivePluginLatencySamples() const noexcept
{
    auto longestTrackRack = 0;
    if (dataModel != nullptr)
        for (size_t trackIndex = 0; trackIndex < dataModel->getTrackCount(); ++trackIndex)
            longestTrackRack = juce::jmax(longestTrackRack, rackLatencySamples(dataModel->getFxRackSnapshot(trackIndex)));

    return longestTrackRack + rackLatencySamples(getMasterFxRackSnapshot());
}

int AudioEngine::getLiveProtectedPluginCount() const noexcept
{
    if (! isLiveLowLatencyProtectionEnabled())
        return 0;

    auto protectedCount = protectedPluginCount(getMasterFxRackSnapshot());
    if (dataModel == nullptr)
        return protectedCount;

    for (size_t trackIndex = 0; trackIndex < dataModel->getTrackCount(); ++trackIndex)
        protectedCount += protectedPluginCount(dataModel->getFxRackSnapshot(trackIndex));
    for (const auto& bus : dataModel->getBuses())
        protectedCount += protectedPluginCount(dataModel->getBusFxRackSnapshot(bus.id));
    return protectedCount;
}

LiveKeyDetector::Result AudioEngine::getLiveKeyResult() const noexcept
{
    return liveKeyDetector.getLatestResult();
}

void AudioEngine::setLiveKeyReferenceTrack(TrackId track) noexcept
{
    liveKeyReferenceTrackId.store(track.value, std::memory_order_release);
}

TrackId AudioEngine::getLiveKeyReferenceTrack() const noexcept
{
    return { liveKeyReferenceTrackId.load(std::memory_order_acquire) };
}

int AudioEngine::applyDetectedKeyToPitchCorrection()
{
    const auto key = getLiveKeyResult();
    if (key.revision == 0 || key.confidence < 0.45f)
        return 0;

    return applyKeyToPitchCorrection(key.root, key.minor);
}

int AudioEngine::applyKeyToPitchCorrection(int rootNote, bool minor)
{
    auto applied = 0;
    const auto applyToRack = [&] (const TrackDataModel::FxRackSnapshot* rack)
    {
        if (rack == nullptr) return;
        for (const auto& processor : rack->processors)
            if (processor != nullptr && processor->applyDetectedKey(rootNote, minor))
                ++applied;
    };
    if (dataModel != nullptr)
        for (size_t index = 0; index < dataModel->getTrackCount(); ++index)
            applyToRack(dataModel->getFxRackSnapshot(index));
    applyToRack(getMasterFxRackSnapshot());
    return applied;
}

void AudioEngine::setFxProcessor(size_t trackIndex, size_t slot,
                                 std::shared_ptr<AudioEffectProcessor> processor)
{
    if (dataModel == nullptr || trackIndex >= TrackDataModel::maxTracks || slot >= TrackDataModel::maxFxSlots)
        return;

    auto* device = deviceManager.getCurrentAudioDevice();
    const auto sampleRate = device != nullptr ? device->getCurrentSampleRate()
                                              : dataModel->getSampleRate();
    const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
    if (processor != nullptr)
        processor->prepareToPlay(sampleRate, blockSize, 2);
    dataModel->setFxProcessor(trackIndex, slot, std::move(processor));
}

void AudioEngine::setFxBypassed(size_t trackIndex, size_t slot, bool bypassed) noexcept
{
    if (dataModel != nullptr)
        dataModel->setFxBypassed(trackIndex, slot, bypassed);
}

const TrackDataModel::FxRackSnapshot* AudioEngine::getMasterFxRackSnapshot() const noexcept
{
    return publishedMasterFxRack.load(std::memory_order_acquire);
}

void AudioEngine::setMasterFxProcessor(size_t slot, std::shared_ptr<AudioEffectProcessor> processor)
{
    if (slot >= TrackDataModel::maxFxSlots) return;
    auto* device = deviceManager.getCurrentAudioDevice();
    const auto sampleRate = device != nullptr ? device->getCurrentSampleRate()
                                              : juce::jmax(activeSampleRate.load(std::memory_order_relaxed),
                                                           dataModel != nullptr ? dataModel->getSampleRate() : 44100.0);
    const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
    if (processor != nullptr) processor->prepareToPlay(sampleRate, blockSize, 2);

    auto updated = std::make_unique<TrackDataModel::FxRackSnapshot>();
    if (const auto* current = getMasterFxRackSnapshot(); current != nullptr) *updated = *current;
    updated->processors[slot] = std::move(processor);
    publishMasterFxRack(std::move(updated));
}

void AudioEngine::setMasterFxBypassed(size_t slot, bool bypassed)
{
    if (slot >= TrackDataModel::maxFxSlots) return;
    auto updated = std::make_unique<TrackDataModel::FxRackSnapshot>();
    if (const auto* current = getMasterFxRackSnapshot(); current != nullptr) *updated = *current;
    updated->bypass[slot] = bypassed;
    publishMasterFxRack(std::move(updated));
}

void AudioEngine::prepareRack(const TrackDataModel::FxRackSnapshot* rack, double sampleRate, int blockSize)
{
    if (rack == nullptr || sampleRate <= 0.0 || blockSize <= 0)
        return;

    for (const auto& processor : rack->processors)
        if (processor != nullptr)
            processor->prepareToPlay(sampleRate, blockSize, 2);
}

int AudioEngine::rackLatencySamples(const TrackDataModel::FxRackSnapshot* rack) const noexcept
{
    if (rack == nullptr)
        return 0;

    auto latency = 0;
    for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
        if (const auto& processor = rack->processors[slot]; processor != nullptr && ! rack->bypass[slot]
            && shouldProcessForLive(*processor))
            latency += processor->getLatencySamples();
    return latency;
}

int AudioEngine::maximumTrackRackLatency(const TrackDataModel::RenderStructureSnapshot& structure) const noexcept
{
    auto result = 0;
    for (size_t trackIndex = 0; trackIndex < structure.trackCount; ++trackIndex)
        result = juce::jmax(result, rackLatencySamples(structure.tracks[trackIndex].fxRack));
    return result;
}

int AudioEngine::maximumActiveBusRackLatency(const TrackDataModel::RenderStructureSnapshot& structure) const noexcept
{
    // Only buses fed by a route participate. An empty return must never add
    // monitor delay merely because it still owns an inserted processor.
    auto result = 0;
    for (size_t trackIndex = 0; trackIndex < structure.trackCount; ++trackIndex)
    {
        const auto outputBusIndex = structure.getBusIndex(structure.tracks[trackIndex].outputBus);
        if (outputBusIndex >= 0)
            result = juce::jmax(result,
                                rackLatencySamples(structure.buses[static_cast<size_t>(outputBusIndex)].fxRack));

        for (const auto& send : structure.tracks[trackIndex].sends)
            if (send.targetBus.isValid())
            {
                const auto busIndex = structure.getBusIndex(send.targetBus);
                if (busIndex >= 0)
                    result = juce::jmax(result, rackLatencySamples(structure.buses[static_cast<size_t>(busIndex)].fxRack));
            }
    }
    return result;
}

void AudioEngine::applyTrackLatencyCompensation(size_t trackIndex, juce::AudioBuffer<float>& buffer,
                                                int delaySamples, int numSamples) noexcept
{
    // Zero is a true bypass. Routing zero through a circular delay line reads
    // stale history at the write head and turns a 0-sample path into a long
    // delayed duplicate — exactly the comb-filter/late-harmonic symptom PDC
    // must prevent.
    if (trackIndex >= trackLatencyDelayBuffers.size() || numSamples <= 0 || delaySamples <= 0)
        return;
    auto& history = trackLatencyDelayBuffers[trackIndex];
    const auto capacity = history.getNumSamples();
    const auto channels = juce::jmin(buffer.getNumChannels(), history.getNumChannels());
    if (capacity <= 0 || channels <= 0)
        return;

    const auto delay = juce::jlimit(0, capacity - 1, delaySamples);
    const auto count = juce::jmin(numSamples, buffer.getNumSamples());
    const auto writeStart = trackLatencyDelayWrite[trackIndex];
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* destination = buffer.getWritePointer(channel);
        auto* delayed = history.getWritePointer(channel);
        auto write = writeStart;
        for (int sample = 0; sample < count; ++sample)
        {
            const auto input = destination[sample];
            const auto read = (write + capacity - delay) % capacity;
            destination[sample] = delay == 0 ? input : delayed[read];
            delayed[write] = input;
            write = (write + 1) % capacity;
        }
    }
    trackLatencyDelayWrite[trackIndex] = (writeStart + count) % capacity;
}

void AudioEngine::applyBusLatencyCompensation(size_t busIndex, juce::AudioBuffer<float>& buffer,
                                              int delaySamples, int numSamples) noexcept
{
    if (busIndex >= busLatencyDelayBuffers.size() || numSamples <= 0 || delaySamples <= 0) return;
    auto& history = busLatencyDelayBuffers[busIndex];
    const auto capacity = history.getNumSamples();
    const auto channels = juce::jmin(buffer.getNumChannels(), history.getNumChannels());
    if (capacity <= 0 || channels <= 0) return;
    const auto delay = juce::jlimit(0, capacity - 1, delaySamples);
    const auto count = juce::jmin(numSamples, buffer.getNumSamples());
    const auto writeStart = busLatencyDelayWrite[busIndex];
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* destination = buffer.getWritePointer(channel);
        auto* delayLine = history.getWritePointer(channel);
        auto write = writeStart;
        for (int sample = 0; sample < count; ++sample)
        {
            const auto read = (write - delay + capacity) % capacity;
            const auto input = destination[sample];
            destination[sample] = delayLine[read];
            delayLine[write] = input;
            write = (write + 1) % capacity;
        }
    }
    busLatencyDelayWrite[busIndex] = (writeStart + count) % capacity;
}

void AudioEngine::applyDirectPathLatencyCompensation(int delaySamples, int numSamples) noexcept
{
    if (delaySamples <= 0) return;
    const auto capacity = directPathDelayBuffer.getNumSamples();
    const auto channels = juce::jmin(directPathBuffer.getNumChannels(), directPathDelayBuffer.getNumChannels());
    if (capacity <= 0 || channels <= 0 || numSamples <= 0) return;
    const auto delay = juce::jlimit(0, capacity - 1, delaySamples);
    const auto count = juce::jmin(numSamples, directPathBuffer.getNumSamples());
    const auto writeStart = directPathDelayWrite;
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* destination = directPathBuffer.getWritePointer(channel);
        auto* delayLine = directPathDelayBuffer.getWritePointer(channel);
        auto write = writeStart;
        for (int sample = 0; sample < count; ++sample)
        {
            const auto read = (write - delay + capacity) % capacity;
            const auto input = destination[sample];
            destination[sample] = delayLine[read];
            delayLine[write] = input;
            write = (write + 1) % capacity;
        }
    }
    directPathDelayWrite = (writeStart + count) % capacity;
}

void AudioEngine::stageDirectPathForBusCompensation(const TrackDataModel::RenderStructureSnapshot* structure,
                                                    int numSamples) noexcept
{
    if (structure == nullptr) return;
    const auto channels = juce::jmin(processingBuffer.getNumChannels(), directPathBuffer.getNumChannels());
    for (int channel = 0; channel < channels; ++channel)
        directPathBuffer.copyFrom(channel, 0, processingBuffer, channel, 0, numSamples);
    processingBuffer.clear(0, numSamples);

    // Studio mixes must align dry/direct paths to active AUX returns. Live
    // monitoring intentionally avoids this extra PDC: the performer hears
    // only intrinsic plug-in latency, never the longest unrelated return.
    if (! isLivePerformanceEnabled() && isStudioPluginDelayCompensationEnabled())
        applyDirectPathLatencyCompensation(maximumActiveBusRackLatency(*structure), numSamples);
}

int AudioEngine::protectedPluginCount(const TrackDataModel::FxRackSnapshot* rack) const noexcept
{
    if (rack == nullptr)
        return 0;

    auto count = 0;
    for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
        if (const auto& processor = rack->processors[slot]; processor != nullptr && ! rack->bypass[slot]
            && ! processor->isMidiEffect() && ! shouldProcessForLive(*processor))
            ++count;
    return count;
}

bool AudioEngine::shouldProcessForLive(const AudioEffectProcessor& processor) const noexcept
{
    return processor.isLivePerformanceEssential()
        || ! (isLiveLowLatencyProtectionEnabled()
              && processor.getLatencySamples() > livePluginLatencyBudgetSamples);
}

void AudioEngine::prepareActiveEffects()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    const auto sampleRate = device != nullptr ? device->getCurrentSampleRate()
                                              : juce::jmax(activeSampleRate.load(std::memory_order_relaxed),
                                                           dataModel != nullptr ? dataModel->getSampleRate() : 44100.0);
    const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples()
                                             : juce::jmax(1, processingBuffer.getNumSamples());
    const auto resolvedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    if (dataModel != nullptr)
    {
        dataModel->setSampleRate(resolvedSampleRate);
        for (size_t trackIndex = 0; trackIndex < TrackDataModel::maxTracks; ++trackIndex)
            prepareRack(dataModel->getFxRackSnapshot(trackIndex), resolvedSampleRate, blockSize);
        for (const auto& bus : dataModel->getBuses())
            prepareRack(dataModel->getBusFxRackSnapshot(bus.id), resolvedSampleRate, blockSize);
    }
    prepareRack(getMasterFxRackSnapshot(), resolvedSampleRate, blockSize);
}

void AudioEngine::publishMasterFxRack(std::unique_ptr<const TrackDataModel::FxRackSnapshot> snapshot)
{
    auto previous = std::move(masterFxRack);
    masterFxRack = std::move(snapshot);
    publishedMasterFxRack.store(masterFxRack.get(), std::memory_order_release);
    if (previous != nullptr) retiredMasterFxRacks.push_back(std::move(previous));
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    // CoreAudio routinely emits stopped/started around buffer negotiation.
    // Treat a fresh callback endpoint as a recovered session, not a lost take.
    recordingDeviceStopMs.store(0, std::memory_order_release);
    const auto outputChannels = device != nullptr ? device->getActiveOutputChannels().countNumberOfSetBits() : 2;
    const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
    // CoreAudio can briefly change callback size while a USB interface
    // renegotiates its stream. Allocate to the driver's largest supported
    // block here (never in the callback) so a momentary larger callback does
    // not hit the early-silence guard in audioDeviceIOCallbackWithContext.
    auto bufferCapacity = juce::jmax(1, blockSize);
    if (device != nullptr)
        for (const auto candidate : device->getAvailableBufferSizes())
            bufferCapacity = juce::jmax(bufferCapacity, candidate);
    bufferCapacity = juce::jmax(bufferCapacity, 4096);
    activeSampleRate.store(device != nullptr ? device->getCurrentSampleRate() : 0.0, std::memory_order_relaxed);
    activeBufferSize.store(blockSize, std::memory_order_relaxed);
    activeInputLatencySamples.store(device != nullptr ? device->getInputLatencyInSamples() : 0, std::memory_order_relaxed);
    activeOutputLatencySamples.store(device != nullptr ? device->getOutputLatencyInSamples() : 0, std::memory_order_relaxed);
    if (diagnosticTracer.isEnabled() && device != nullptr)
        diagnosticTracer.setCoreAudioInputDevice(device->getName());
    recordDeviceSessionEvent(1);
    callbackLoad.store(0.0f, std::memory_order_relaxed);
    rebuildInputChannelMap(device);
    rebuildOutputChannelMap(device);
    processingBuffer.setSize(juce::jmax(2, outputChannels), bufferCapacity,
                             false, true, true);
    directPathBuffer.setSize(2, bufferCapacity, false, true, true);
    directPathDelayBuffer.setSize(2, trackLatencyCompensationCapacity, false, true, true);
    directPathDelayBuffer.clear();
    directPathDelayWrite = 0;
    liveKeyReferenceBuffer.setSize(2, bufferCapacity, false, true, true);
    streamBusBuffer.setSize(2, bufferCapacity, false, true, true);
    streamMonitorBuffer.setSize(2, bufferCapacity, false, true, true);
    cueMonitorBuffer.setSize(2, bufferCapacity, false, true, true);
    for (auto& buffer : trackBuffers)
        buffer.setSize(2, processingBuffer.getNumSamples(), false, true, true);
    for (size_t index = 0; index < trackLatencyDelayBuffers.size(); ++index)
    {
        trackLatencyDelayBuffers[index].setSize(2, trackLatencyCompensationCapacity, false, true, true);
        trackLatencyDelayBuffers[index].clear();
        trackLatencyDelayWrite[index] = 0;
    }
    for (auto& buffer : busBuffers)
        buffer.setSize(2, processingBuffer.getNumSamples(), false, true, true);
    for (size_t index = 0; index < busLatencyDelayBuffers.size(); ++index)
    {
        busLatencyDelayBuffers[index].setSize(2, trackLatencyCompensationCapacity, false, true, true);
        busLatencyDelayBuffers[index].clear();
        busLatencyDelayWrite[index] = 0;
    }
    for (auto& midi : trackMidiBuffers)
        midi.ensureSize(MidiEventBuffer::capacity * 4);

    prepareActiveEffects();

    // CoreAudio may stop/start the device during a buffer-size negotiation or
    // a transient USB interruption.  Preserve an explicitly enabled monitor
    // route instead of leaving the track armed in the UI while its callback
    // path has silently lost permission.  Built-in speakers remain opt-in
    // blocked to avoid turning a microphone restart into feedback.
    if (dataModel != nullptr && device != nullptr)
    {
        dataModel->activateConfiguredMonitorRoutes(MonitorDestination::master);
        if (isLivePerformanceEnabled())
            restoreLiveVocalMonitorRoutes();
    }
}

void AudioEngine::audioDeviceStopped()
{
    if (recordingSession != nullptr && recordingSession->isRecording())
        recordingDeviceStopMs.store(static_cast<uint64_t>(juce::Time::getMillisecondCounterHiRes()),
                                    std::memory_order_release);
    recordDeviceSessionEvent(2);
    // CoreAudio calls this for benign changes too (buffer-size/device setup).
    // Do not silently turn off a performer's MIC route: audioDeviceAboutToStart
    // rebuilds the physical map and resumes the persisted runtime route.
    processingBuffer.clear();
    for (auto& callbackChannel : callbackInputByPhysicalChannel)
        callbackChannel.store(-1, std::memory_order_release);
    for (auto& callbackChannel : callbackOutputByPhysicalChannel)
        callbackChannel.store(-1, std::memory_order_release);
    activeCallbackInputCount.store(0, std::memory_order_release);
    activeSampleRate.store(0.0, std::memory_order_relaxed);
    activeBufferSize.store(0, std::memory_order_relaxed);
}

void AudioEngine::rebuildInputChannelMap(const juce::AudioIODevice* device) noexcept
{
    for (auto& callbackChannel : callbackInputByPhysicalChannel)
        callbackChannel.store(-1, std::memory_order_relaxed);

    auto callbackChannel = 0;
    if (device != nullptr)
    {
        const auto active = device->getActiveInputChannels();
        for (int physicalChannel = 0; physicalChannel <= active.getHighestBit(); ++physicalChannel)
            if (active[physicalChannel])
            {
                if (physicalChannel < maximumPhysicalInputChannels)
                    callbackInputByPhysicalChannel[static_cast<size_t>(physicalChannel)].store(callbackChannel,
                                                                                                  std::memory_order_relaxed);
                ++callbackChannel;
            }
    }
    activeCallbackInputCount.store(callbackChannel, std::memory_order_release);
}

void AudioEngine::rebuildOutputChannelMap(const juce::AudioIODevice* device) noexcept
{
    for (auto& callbackChannel : callbackOutputByPhysicalChannel)
        callbackChannel.store(-1, std::memory_order_relaxed);
    if (device == nullptr) return;

    const auto active = device->getActiveOutputChannels();
    auto callbackChannel = 0;
    for (int physicalChannel = 0; physicalChannel <= active.getHighestBit(); ++physicalChannel)
        if (active[physicalChannel])
        {
            if (physicalChannel < maximumPhysicalInputChannels)
                callbackOutputByPhysicalChannel[static_cast<size_t>(physicalChannel)].store(callbackChannel,
                                                                                               std::memory_order_relaxed);
            ++callbackChannel;
        }
}

int AudioEngine::callbackOutputForPhysicalChannel(int physicalChannel) const noexcept
{
    if (physicalChannel < 0 || physicalChannel >= maximumPhysicalInputChannels)
        return -1;
    return callbackOutputByPhysicalChannel[static_cast<size_t>(physicalChannel)].load(std::memory_order_acquire);
}

int AudioEngine::callbackInputForPhysicalChannel(int physicalChannel) const noexcept
{
    if (physicalChannel < 0 || physicalChannel >= maximumPhysicalInputChannels)
        return -1;
    return callbackInputByPhysicalChannel[static_cast<size_t>(physicalChannel)].load(std::memory_order_acquire);
}

bool AudioEngine::isPhysicalInputRouteAvailable(int physicalChannel, int channelCount) const noexcept
{
    if (channelCount < 1 || channelCount > 2)
        return false;
    const auto firstCallbackChannel = callbackInputForPhysicalChannel(physicalChannel);
    if (firstCallbackChannel < 0)
        return false;
    if (channelCount == 1)
        return true;
    const auto secondCallbackChannel = callbackInputForPhysicalChannel(physicalChannel + 1);
    return secondCallbackChannel == firstCallbackChannel + 1;
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message)
{
    MidiRealtimeEvent event;
    if (! MidiRealtimeEvent::fromMessage(message, event))
        return;
    if (message.isController())
    {
        const auto controller = message.getControllerNumber();
        const auto value = static_cast<float>(message.getControllerValue()) / 127.0f;
        const auto channel = message.getChannel();
        const auto pending = pendingMasterMidiLearn.exchange(-1, std::memory_order_acq_rel);
        if (pending >= 0 && pending < static_cast<int>(MasterMidiTarget::count))
            masterMidiCc[static_cast<size_t>(pending)].store(controller | (channel << 8), std::memory_order_release);
        for (size_t index = 0; index < masterMidiCc.size(); ++index)
        {
            const auto mapping = masterMidiCc[index].load(std::memory_order_acquire);
            if ((mapping & 0xff) != controller || ((mapping >> 8) != channel)) continue;
            const auto target = static_cast<MasterMidiTarget>(index);
            // The Master fader is calibrated to the console's engraved
            // -12..+12 dB travel.  Keep MIDI's 0..127 range on that same
            // scale so the physical midpoint is exactly unity gain.
            if (target == MasterMidiTarget::fader) setMasterGain(juce::Decibels::decibelsToGain(-12.0f + value * 24.0f));
            else if (target == MasterMidiTarget::pan) setMasterPan(value * 2.0f - 1.0f);
            else if (target == MasterMidiTarget::width) setMasterWidth(value * 2.0f);
            else if (target == MasterMidiTarget::mono) setMasterMono(value >= 0.5f);
            else if (target == MasterMidiTarget::dim) setMasterDim(value >= 0.5f);
            else if (target == MasterMidiTarget::mute) setMasterMuted(value >= 0.5f);
        }
    }

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    incomingMidiFifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 == 0)
    {
        droppedMidiInputEvents.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const auto token = source == nullptr ? 0u : static_cast<uint32_t>(source->getIdentifier().hashCode());
    incomingMidiEvents[static_cast<size_t>(start1)] = { event, token };
    incomingMidiFifo.finishedWrite(1);
}

AudioEngine::CallbackTimingScope::CallbackTimingScope(AudioEngine& owner, int samples) noexcept
    : engine(owner), numSamples(samples), started(std::chrono::steady_clock::now())
{
}

AudioEngine::CallbackTimingScope::~CallbackTimingScope()
{
    engine.recordCallbackTiming(numSamples, started);
}

void AudioEngine::recordCallbackTiming(int numSamples, std::chrono::steady_clock::time_point started) noexcept
{
    const auto sampleRate = activeSampleRate.load(std::memory_order_relaxed);
    if (sampleRate <= 0.0 || numSamples <= 0)
        return;

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const auto blockDuration = static_cast<double>(numSamples) / sampleRate;
    const auto instantaneousLoad = static_cast<float>(elapsed / blockDuration);
    const auto previousLoad = callbackLoad.load(std::memory_order_relaxed);
    callbackLoad.store(previousLoad * 0.90f + instantaneousLoad * 0.10f, std::memory_order_relaxed);
    if (elapsed > blockDuration)
        callbackOverloadCount.fetch_add(1, std::memory_order_relaxed);
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                   int numInputChannels,
                                                   float* const* outputChannelData,
                                                   int numOutputChannels,
                                                   int numSamples,
                                                   const juce::AudioIODeviceCallbackContext&)
{
    if (numSamples <= 0 || outputChannelData == nullptr)
        return;

    lastAudioCallbackMs.store(static_cast<uint64_t>(juce::Time::getMillisecondCounterHiRes()),
                              std::memory_order_release);
    CallbackTimingScope callbackTiming(*this, numSamples);

    for (int channel = 0; channel < numOutputChannels; ++channel)
        juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);

    if (dataModel == nullptr || numSamples > processingBuffer.getNumSamples())
        return;

    captureRecordingInput(inputChannelData, numInputChannels, numSamples);

    const auto blockStartSample = dataModel->getPlayheadPosition();
    const auto snapshots = dataModel->acquireRealtimeSnapshot();
    const auto* clips = snapshots.getAudioClips();
    const auto* structure = snapshots.getRenderStructure();
    const auto* midiClips = snapshots.getMidiClips();
    const auto playing = dataModel->isPlaying();
    updateTrackInputPeaks(inputChannelData, numInputChannels, structure, numSamples);
    const auto hasMonitoredAudio = hasMonitoredAudioTracks(structure);
    const auto hasPendingMidi = incomingMidiFifo.getNumReady() > 0;
    const auto hasActiveInstrument = std::any_of(instrumentVoices.begin(), instrumentVoices.end(),
                                                 [] (const InstrumentVoice& voice) { return voice.active; });
    if (! playing && ! hasMonitoredAudio && ! hasPendingMidi && ! hasActiveInstrument)
        return;

    clearRealtimeBuffers(numSamples);

    scheduledMidiEvents.clear();
    if (playing && clips != nullptr && structure != nullptr)
    {
        const auto cycleActive = dataModel->isCycleActive();
        const auto cycleStart = dataModel->getCycleStartSample();
        const auto cycleEnd = dataModel->getCycleEndSample();
        auto playhead = TransportUtils::normalisePlayhead(dataModel->getPlayheadPosition(), cycleActive,
                                                          cycleStart, cycleEnd);
        int renderedSamples = 0;
        while (renderedSamples < numSamples)
        {
            const auto segmentSamples = TransportUtils::samplesUntilBoundary(
                playhead, numSamples - renderedSamples, cycleActive, cycleStart, cycleEnd);
            renderClipSegment(*clips, *structure, playhead, renderedSamples, segmentSamples);
            if (midiClips != nullptr)
            {
                MidiEventBuffer segmentEvents;
                MidiScheduler::scheduleBlock(*midiClips, playhead, segmentSamples, segmentEvents);
                for (size_t eventIndex = 0; eventIndex < segmentEvents.size(); ++eventIndex)
                {
                    auto event = segmentEvents[eventIndex];
                    event.sampleOffset += renderedSamples;
                    if (! scheduledMidiEvents.add(event))
                        break;
                }
            }
            playhead = TransportUtils::advance(playhead, segmentSamples, cycleActive, cycleStart, cycleEnd);
            renderedSamples += segmentSamples;
        }
        dataModel->setPlayheadPosition(playhead);
    }

    bool anyTrackSoloed = false;
    const auto automationSample = dataModel->getPlayheadPosition();
    for (size_t trackIndex = 0; structure != nullptr && trackIndex < structure->trackCount; ++trackIndex)
        anyTrackSoloed = anyTrackSoloed || structure->tracks[trackIndex].solo;

    prepareTrackMidiBuffers(structure);
    if (structure != nullptr)
        appendIncomingMidiEvents(*structure, blockStartSample);
    processTrackMidiEffects(structure);
    sendExternalMidi(structure);

    if (structure != nullptr)
        renderInstrument(*structure, numSamples);

    renderInputMonitoring(inputChannelData, numInputChannels, structure, numSamples);

    processTrackAudio(structure, automationSample, anyTrackSoloed, numSamples);
    stageDirectPathForBusCompensation(structure, numSamples);
    processBusReturns(structure, numSamples);

    processMasterOutput(outputChannelData, numOutputChannels, numSamples, playing, blockStartSample);
    const auto traceTimestamp = static_cast<uint64_t>(juce::Time::getMillisecondCounterHiRes());
    if (diagnosticTracer.isEnabled() && diagnosticTracer.shouldRecordSignalEvent(traceTimestamp))
    {
        uint64_t monitoredTrackId = 0;
        auto monitoredPhysicalInput = -1;
        float monitoredTrackPostFxPeak = 0.0f;
        LiveProcessorHealth vibeHealth;
        auto hasNativeVibe = false;
        if (structure != nullptr)
        {
            // Prefer a live-monitored Vibe track, then any Vibe insert (so a
            // rendered/raw clip is diagnosable), then a monitored track as a
            // fallback for device-routing diagnostics.
            auto selectedIndex = structure->trackCount;
            const auto requestedTraceTrack = pluginTraceTrackIndex.load(std::memory_order_acquire);
            if (requestedTraceTrack >= 0 && requestedTraceTrack < static_cast<int>(structure->trackCount))
                selectedIndex = static_cast<size_t>(requestedTraceTrack);
            else
            {
                auto fallbackMonitoredIndex = structure->trackCount;
                for (size_t index = 0; index < structure->trackCount; ++index)
                {
                    const auto& track = structure->tracks[index];
                    auto containsNativeVibe = false;
                    if (const auto* rack = track.fxRack; rack != nullptr)
                        for (const auto& processor : rack->processors)
                            if (processor != nullptr && processor->isStudioForgeNativeVibeAutotune())
                            {
                                containsNativeVibe = true;
                                break;
                            }

                    const auto isMonitored = isInputMonitoringEligible(track);
                    if (containsNativeVibe && (selectedIndex == structure->trackCount || isMonitored))
                    {
                        selectedIndex = index;
                        if (isMonitored)
                            break;
                    }
                    if (isMonitored && fallbackMonitoredIndex == structure->trackCount)
                        fallbackMonitoredIndex = index;
                }
                if (selectedIndex == structure->trackCount)
                    selectedIndex = fallbackMonitoredIndex;
            }
            if (selectedIndex != structure->trackCount)
            {
                const auto& track = structure->tracks[selectedIndex];
                monitoredTrackId = track.id.value;
                monitoredPhysicalInput = track.inputChannel;
                monitoredTrackPostFxPeak = TrackMixing::peak(trackBuffers[selectedIndex], numSamples);
                if (const auto* rack = track.fxRack; rack != nullptr)
                    for (const auto& processor : rack->processors)
                        if (processor != nullptr && processor->isStudioForgeNativeVibeAutotune())
                        {
                            vibeHealth = processor->getLiveProcessorHealth();
                            hasNativeVibe = true;
                            break;
                        }
            }
        }
        AudioDiagnosticTracer::Event event {};
        event.ms = traceTimestamp;
        event.callbackFrames = static_cast<uint32_t>(numSamples);
        event.trackId = monitoredTrackId;
        event.input = monitoredPhysicalInput;
        event.mode = static_cast<int>(getWorkspaceMode());
        event.panic = static_cast<uint8_t>(panicMuteEnabled.load(std::memory_order_relaxed));
        event.trackPostFxPeak = monitoredTrackPostFxPeak;
        event.vibeState = hasNativeVibe ? static_cast<uint8_t>(vibeHealth.state) : 0;
        event.vibeWetMix = hasNativeVibe ? vibeHealth.wetMix : 0.0f;
        event.vibeTrackerVoiced = hasNativeVibe ? static_cast<uint8_t>(vibeHealth.trackerVoiced) : 0;
        event.vibeTrackerConfidence = hasNativeVibe ? vibeHealth.trackerConfidence : 0.0f;
        event.vibeTrackerF0Hz = hasNativeVibe ? vibeHealth.trackerF0Hz : 0.0f;
        event.vibeTrackerDetectedMidiNote = hasNativeVibe ? vibeHealth.trackerDetectedMidiNote : -1;
        event.vibeTrackerTargetMidiNote = hasNativeVibe ? vibeHealth.trackerTargetMidiNote : -1;
        event.vibeTrackerOutputMidiNote = hasNativeVibe ? vibeHealth.trackerOutputMidiNote : -1;
        event.vibeTrackerInputCents = hasNativeVibe ? vibeHealth.trackerInputCents : 0.0f;
        event.vibeCorrectionCents = hasNativeVibe ? vibeHealth.correctionCents : 0.0f;
        event.vibeRendererCollapsed = hasNativeVibe ? static_cast<uint8_t>(vibeHealth.rendererCollapsed) : 0;
        event.vibeRendererExcessive = hasNativeVibe ? static_cast<uint8_t>(vibeHealth.rendererExcessive) : 0;
        event.vibeRendererInvalid = hasNativeVibe ? static_cast<uint8_t>(vibeHealth.rendererInvalid) : 0;
        event.vibeCallbackOverloaded = hasNativeVibe ? static_cast<uint8_t>(vibeHealth.callbackOverloaded) : 0;
        event.vibeHardDryRescue = hasNativeVibe ? static_cast<uint8_t>(vibeHealth.hardDryRescue) : 0;
        event.vibeProtectionCount = hasNativeVibe ? vibeHealth.protectionCount : 0;
        event.vibeOverloadCount = hasNativeVibe ? vibeHealth.overloadCount : 0;
        event.pluginSlot = pluginTraceSlot.load(std::memory_order_relaxed);
        event.pluginLatencySamples = pluginTraceLatencySamples.load(std::memory_order_relaxed);
        event.pluginPreFxPeak = pluginTracePreFxPeak.load(std::memory_order_relaxed);
        event.pluginPostFxPeak = pluginTracePostFxPeak.load(std::memory_order_relaxed);
        event.pluginPresent = pluginTracePresent.load(std::memory_order_relaxed);
        event.pluginBypassed = pluginTraceBypassed.load(std::memory_order_relaxed);
        event.pluginProcessed = pluginTraceProcessed.load(std::memory_order_relaxed);
        event.pluginSkippedForLatency = pluginTraceSkippedForLatency.load(std::memory_order_relaxed);
        if (monitoredPhysicalInput >= 0)
        {
            const auto i = callbackInputForPhysicalChannel(monitoredPhysicalInput);
            if (i >= 0 && i < numInputChannels && inputChannelData[i] != nullptr)
            {
                const auto range = juce::FloatVectorOperations::findMinAndMax(inputChannelData[i], numSamples);
                event.inputPeak = juce::jmax(std::abs(range.getStart()), std::abs(range.getEnd()));
            }
        }
        // The Scarlett exposes two independent physical input streams.  Keep
        // a raw peak for the other callback channel in the trace so a bad
        // physical-to-callback map cannot masquerade as an audio fade.
        if (numInputChannels > 1 && inputChannelData[1] != nullptr)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax(inputChannelData[1], numSamples);
            event.secondaryInputPeak = juce::jmax(std::abs(range.getStart()), std::abs(range.getEnd()));
        }
        event.outputPeak = outputChannelData[0] != nullptr ? peakForChannel(juce::AudioBuffer<float>(outputChannelData, numOutputChannels, numSamples), 0, numSamples) : 0.0f;
        diagnosticTracer.record(event);
    }
}

void AudioEngine::processMasterOutput(float* const* outputChannelData, int numOutputChannels,
                                      int numSamples, bool isPlaying, double blockStartSample) noexcept
{
    if (const auto* rack = publishedMasterFxRack.load(std::memory_order_acquire); rack != nullptr)
        for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
            if (const auto& processor = rack->processors[slot]; processor != nullptr && !rack->bypass[slot]
                && shouldProcessForLive(*processor))
                processEffectBlock(*processor, processingBuffer, numSamples);

    graph.process(processingBuffer, numSamples);
    if (isPlaying && (metronomeEnabled.load(std::memory_order_acquire)
                      || countInMetronomeActive.load(std::memory_order_acquire)))
        renderMetronome(blockStartSample, numSamples);
    // Key learning is a rehearsal-pass feature, not a LIVE-mode side effect.
    // Analyse the chosen beat whenever transport is playing, then the UI can
    // lock the result before the singer starts the performance pass.
    if (isPlaying && liveKeyReferenceTrackId.load(std::memory_order_acquire) != 0)
        liveKeyDetector.push(liveKeyReferenceBuffer, numSamples, activeSampleRate.load(std::memory_order_relaxed));
    const auto pan = masterPan.load(std::memory_order_relaxed);
    const auto width = masterWidth.load(std::memory_order_relaxed);
    const auto dim = masterDim.load(std::memory_order_relaxed);
    const auto mono = masterMono.load(std::memory_order_relaxed);
    const auto muted = masterMuted.load(std::memory_order_relaxed);
    // Unity-centre pan law: at centre both channels remain unchanged, while
    // moving left/right fades only the opposite side. This keeps the Master
    // fader and existing mix levels backward-compatible at the default value.
    const auto panLeft = pan > 0.0f ? 1.0f - pan : 1.0f;
    const auto panRight = pan < 0.0f ? 1.0f + pan : 1.0f;
    for (int sample = 0; sample < numSamples; ++sample)
    {
        auto left = processingBuffer.getSample(0, sample);
        auto right = processingBuffer.getNumChannels() > 1 ? processingBuffer.getSample(1, sample) : left;
        if (mono) left = right = 0.5f * (left + right);
        const auto mid = 0.5f * (left + right);
        const auto side = 0.5f * (left - right) * width;
        const auto dimGain = dim ? 0.25f : 1.0f;
        const auto gain = muted ? 0.0f : dimGain;
        processingBuffer.setSample(0, sample, (mid + side) * panLeft * gain);
        if (processingBuffer.getNumChannels() > 1) processingBuffer.setSample(1, sample, (mid - side) * panRight * gain);
    }
    // Do not hard-clip the mix here. A safety ceiling must be a true limiter;
    // clipping at this point corrupts normal bus/send gain sums.
    const auto streamChannels = juce::jmin(streamBusBuffer.getNumChannels(), processingBuffer.getNumChannels());
    for (int channel = 0; channel < streamChannels; ++channel)
        streamBusBuffer.copyFrom(channel, 0, processingBuffer, channel, 0, numSamples);
    for (int channel = 0; channel < juce::jmin(streamMonitorBuffer.getNumChannels(), streamBusBuffer.getNumChannels()); ++channel)
        streamBusBuffer.addFrom(channel, 0, streamMonitorBuffer, channel, 0, numSamples);
    // The stream bus is captured post-FX but before the physical-output Panic
    // gate. Stream Mute is intentionally applied only by the bridge consumer.
    publishStreamOutput(streamBusBuffer, numSamples);
    if (panicMuteEnabled.load(std::memory_order_acquire))
    {
        processingBuffer.clear(0, numSamples);
        cueMonitorBuffer.clear(0, numSamples);
    }
    masterPeak.store(TrackMixing::peak(processingBuffer, numSamples), std::memory_order_relaxed);
    masterLeftPeak.store(peakForChannel(processingBuffer, 0, numSamples), std::memory_order_relaxed);
    masterRightPeak.store(peakForChannel(processingBuffer, 1, numSamples), std::memory_order_relaxed);
    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
    for (int channel = 0; channel < juce::jmin(2, juce::jmin(numOutputChannels, processingBuffer.getNumChannels())); ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::copy(outputChannelData[channel], processingBuffer.getReadPointer(channel), numSamples);
    const auto cueLeft = callbackOutputForPhysicalChannel(2);
    const auto cueRight = callbackOutputForPhysicalChannel(3);
    if (cueLeft >= 0 && cueLeft < numOutputChannels && outputChannelData[cueLeft] != nullptr)
        juce::FloatVectorOperations::copy(outputChannelData[cueLeft], cueMonitorBuffer.getReadPointer(0), numSamples);
    if (cueRight >= 0 && cueRight < numOutputChannels && outputChannelData[cueRight] != nullptr)
        juce::FloatVectorOperations::copy(outputChannelData[cueRight], cueMonitorBuffer.getReadPointer(1), numSamples);
}

void AudioEngine::prepareTrackMidiBuffers(const TrackDataModel::RenderStructureSnapshot* structure) noexcept
{
    for (auto& midiBuffer : trackMidiBuffers)
        midiBuffer.clear();
    if (structure == nullptr) return;
    for (size_t eventIndex = 0; eventIndex < scheduledMidiEvents.size(); ++eventIndex)
    {
        const auto& event = scheduledMidiEvents[eventIndex];
        const auto trackIndex = structure->getTrackIndex(event.trackId);
        if (trackIndex < 0) continue;
        const auto message = event.noteOn ? juce::MidiMessage::noteOn(event.channel, event.pitch, event.velocity)
                                          : juce::MidiMessage::noteOff(event.channel, event.pitch);
        trackMidiBuffers[static_cast<size_t>(trackIndex)].addEvent(message, event.sampleOffset);
    }
}

void AudioEngine::processTrackAudio(const TrackDataModel::RenderStructureSnapshot* structure,
                                    double automationSample, bool anyTrackSoloed, int numSamples) noexcept
{
    if (structure == nullptr) return;
    // PDC is essential while mixing/printing. In a live workspace it must not
    // make a monitored singer wait for an unrelated high-latency track; the
    // monitored path retains only the selected insert chain's own latency.
    const auto longestTrackLatency = isLivePerformanceEnabled() || ! isStudioPluginDelayCompensationEnabled()
        ? 0 : maximumTrackRackLatency(*structure);
    const auto traceTrackIndex = pluginTraceTrackIndex.load(std::memory_order_acquire);
    const auto traceSlot = pluginTraceSlot.load(std::memory_order_acquire);
    for (size_t trackIndex = 0; trackIndex < structure->trackCount; ++trackIndex)
    {
        auto& trackBuffer = trackBuffers[trackIndex];
        const auto& track = structure->tracks[trackIndex];
        if (track.fxRack != nullptr)
            for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
            {
                const auto& processor = track.fxRack->processors[slot];
                const auto tracingThisSlot = static_cast<int>(trackIndex) == traceTrackIndex
                                          && static_cast<int>(slot) == traceSlot;
                const auto bypassed = track.fxRack->bypass[slot];
                const auto processable = processor != nullptr && ! bypassed && ! processor->isMidiEffect();
                const auto admitted = processable && shouldProcessForLive(*processor);
                if (tracingThisSlot)
                {
                    pluginTracePresent.store(processor != nullptr, std::memory_order_relaxed);
                    pluginTraceBypassed.store(bypassed, std::memory_order_relaxed);
                    pluginTraceProcessed.store(false, std::memory_order_relaxed);
                    pluginTraceSkippedForLatency.store(processable && ! admitted, std::memory_order_relaxed);
                    pluginTraceLatencySamples.store(processor != nullptr ? processor->getLatencySamples() : 0,
                                                    std::memory_order_relaxed);
                    pluginTracePreFxPeak.store(TrackMixing::peak(trackBuffer, numSamples), std::memory_order_relaxed);
                }
                if (admitted)
                    processEffectBlock(*processor, trackBuffer, trackMidiBuffers[trackIndex], numSamples);
                if (tracingThisSlot)
                {
                    pluginTraceProcessed.store(admitted, std::memory_order_relaxed);
                    pluginTracePostFxPeak.store(TrackMixing::peak(trackBuffer, numSamples), std::memory_order_relaxed);
                }
            }

        // Align every insert chain before sends, faders and summing. A vocal
        // through Vibe therefore stays time-aligned with raw backing tracks.
        applyTrackLatencyCompensation(trackIndex, trackBuffer,
                                      juce::jmax(0, longestTrackLatency - rackLatencySamples(track.fxRack)), numSamples);

        // Reference audio is captured after the selected track's insert chain
        // but before fader, buses, master FX, vocal monitoring and streaming.
        if (track.id.value == liveKeyReferenceTrackId.load(std::memory_order_relaxed))
            for (int channel = 0; channel < juce::jmin(trackBuffer.getNumChannels(), liveKeyReferenceBuffer.getNumChannels()); ++channel)
                liveKeyReferenceBuffer.copyFrom(channel, 0, trackBuffer, channel, 0, numSamples);


        const auto sendLevel = [&track, automationSample] (size_t route) noexcept
        {
            const auto& send = track.sends[route];
            return send.automation.mode == AutomationMode::read && send.automation.pointCount > 0
                ? send.automation.evaluate(automationSample) : send.level;
        };
        for (size_t route = 0; route < TrackDataModel::maxSendsPerTrack; ++route)
            if (track.sends[route].targetBus.isValid() && track.sends[route].preFader)
                addTrackToBus(track, trackIndex, trackBuffer, structure, route, sendLevel(route), numSamples);

        const auto volume = track.volumeAutomation.mode == AutomationMode::read && track.volumeAutomation.pointCount > 0
            ? track.volumeAutomation.evaluate(automationSample) : track.volume;
        const auto pan = track.panAutomation.mode == AutomationMode::read && track.panAutomation.pointCount > 0
            ? track.panAutomation.evaluate(automationSample) : track.pan;
        const auto audible = ! track.muted && (! anyTrackSoloed || track.solo || track.soloSafe);
        for (size_t route = 0; route < TrackDataModel::maxSendsPerTrack; ++route)
            if (track.sends[route].targetBus.isValid() && ! track.sends[route].preFader
                && track.sends[route].postFaderBeforePan)
                addTrackToBus(track, trackIndex, trackBuffer, structure, route,
                              audible ? sendLevel(route) * volume : 0.0f, numSamples);
        TrackMixing::apply(trackBuffer, numSamples, { volume, pan, !track.muted && (!anyTrackSoloed || track.solo || track.soloSafe) });
        trackPeaks[trackIndex].store(TrackMixing::peak(trackBuffer, numSamples), std::memory_order_relaxed);

        auto& destination = structure->getBusIndex(track.outputBus) >= 0
            ? busBuffers[static_cast<size_t>(structure->getBusIndex(track.outputBus))] : processingBuffer;
        for (int channel = 0; channel < juce::jmin(trackBuffer.getNumChannels(), destination.getNumChannels()); ++channel)
            juce::FloatVectorOperations::add(destination.getWritePointer(channel), trackBuffer.getReadPointer(channel), numSamples);
        for (size_t route = 0; route < TrackDataModel::maxSendsPerTrack; ++route)
            if (track.sends[route].targetBus.isValid() && ! track.sends[route].preFader && ! track.sends[route].postFaderBeforePan)
                addTrackToBus(track, trackIndex, trackBuffer, structure, route, sendLevel(route), numSamples);
    }
}

void AudioEngine::processBusReturns(const TrackDataModel::RenderStructureSnapshot* structure, int numSamples) noexcept
{
    if (structure == nullptr) return;
    const auto longestBusLatency = isLivePerformanceEnabled() || ! isStudioPluginDelayCompensationEnabled()
        ? 0 : maximumActiveBusRackLatency(*structure);
    for (size_t busIndex = 0; busIndex < structure->busCount; ++busIndex)
    {
        const auto& bus = structure->buses[busIndex];
        if (bus.fxRack != nullptr)
            for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
                if (const auto& processor = bus.fxRack->processors[slot]; processor != nullptr && ! bus.fxRack->bypass[slot]
                    && shouldProcessForLive(*processor))
                    processEffectBlock(*processor, busBuffers[busIndex], numSamples);
        applyBusLatencyCompensation(busIndex, busBuffers[busIndex],
                                    juce::jmax(0, longestBusLatency - rackLatencySamples(bus.fxRack)), numSamples);
        if (bus.muted) busBuffers[busIndex].clear(0, numSamples);
        else for (int channel = 0; channel < busBuffers[busIndex].getNumChannels(); ++channel)
        {
            const auto balanceGain = channel == 0 ? 1.0f - juce::jmax(0.0f, bus.balance)
                                    : channel == 1 ? 1.0f + juce::jmin(0.0f, bus.balance) : 1.0f;
            busBuffers[busIndex].applyGain(channel, 0, numSamples, bus.gain * balanceGain);
        }
        busPeaks[busIndex].store(TrackMixing::peak(busBuffers[busIndex], numSamples), std::memory_order_relaxed);
        for (int channel = 0; channel < juce::jmin(busBuffers[busIndex].getNumChannels(), processingBuffer.getNumChannels()); ++channel)
            juce::FloatVectorOperations::add(processingBuffer.getWritePointer(channel), busBuffers[busIndex].getReadPointer(channel), numSamples);
    }
    for (int channel = 0; channel < juce::jmin(directPathBuffer.getNumChannels(), processingBuffer.getNumChannels()); ++channel)
        juce::FloatVectorOperations::add(processingBuffer.getWritePointer(channel), directPathBuffer.getReadPointer(channel), numSamples);
}

void AudioEngine::addTrackToBus(const TrackDataModel::RenderTrack& track,
                                size_t trackIndex,
                                const juce::AudioBuffer<float>& source,
                                const TrackDataModel::RenderStructureSnapshot* structure,
                                size_t route, float level, int numSamples) noexcept
{
    const auto busIndex = structure->getBusIndex(track.sends[route].targetBus);
    if (busIndex < 0) return;
    auto& destination = busBuffers[static_cast<size_t>(busIndex)];
    const auto targetGain = juce::jlimit(0.0f, TrackDataModel::maximumSendGain * AudioGainRange::maximumChannelGain, level);
    auto& previousGain = smoothedSendGains[trackIndex][route];
    // A newly-created route must be audible at its configured level on the
    // first callback. Subsequent automation still uses a ramp to avoid steps.
    if (previousGain == 0.0f && targetGain > 0.0f)
        previousGain = targetGain;
    for (int channel = 0; channel < juce::jmin(source.getNumChannels(), destination.getNumChannels()); ++channel)
        destination.addFromWithRamp(channel, 0, source.getReadPointer(channel), numSamples, previousGain, targetGain);
    previousGain = targetGain;
}

void AudioEngine::processTrackMidiEffects(const TrackDataModel::RenderStructureSnapshot* structure) noexcept
{
    if (structure == nullptr) return;
    for (size_t trackIndex = 0; trackIndex < structure->trackCount; ++trackIndex)
        if (const auto* rack = structure->tracks[trackIndex].fxRack; rack != nullptr)
            for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
                if (const auto& processor = rack->processors[slot]; processor != nullptr && ! rack->bypass[slot]
                    && processor->isMidiEffect())
                    processor->processBlock(trackBuffers[trackIndex], trackMidiBuffers[trackIndex]);
}

void AudioEngine::sendExternalMidi(const TrackDataModel::RenderStructureSnapshot* structure) noexcept
{
    if (structure == nullptr)
        return;

    midiOutputCallbackUsers.fetch_add(1, std::memory_order_acq_rel);
    auto* output = publishedMidiOutput.load(std::memory_order_acquire);
    if (output != nullptr)
        for (size_t trackIndex = 0; trackIndex < structure->trackCount; ++trackIndex)
            if (structure->tracks[trackIndex].type == TrackType::externalMidi)
                for (const auto metadata : trackMidiBuffers[trackIndex])
                    output->sendMessageNow(metadata.getMessage());
    midiOutputCallbackUsers.fetch_sub(1, std::memory_order_acq_rel);
}

void AudioEngine::captureRecordingInput(const float* const* inputChannelData,
                                        int numInputChannels, int numSamples) noexcept
{
    if (recordingSession == nullptr) return;
    auto captureOffset = 0;
    auto captureSamples = numSamples;
    if (recordingCaptureRangeActive.load(std::memory_order_acquire))
    {
        if (! dataModel->isPlaying()) captureSamples = 0;
        else
        {
            const auto blockStart = dataModel->getPlayheadPosition();
            const auto rangeStart = recordingCaptureStartSample.load(std::memory_order_acquire);
            const auto rangeEnd = recordingCaptureEndSample.load(std::memory_order_acquire);
            captureOffset = juce::jlimit(0, numSamples, static_cast<int>(std::ceil(rangeStart - blockStart)));
            const auto captureEnd = juce::jlimit(0, numSamples, static_cast<int>(std::ceil(rangeEnd - blockStart)));
            captureSamples = juce::jmax(0, captureEnd - captureOffset);
        }
    }
    if (captureSamples > 0)
        recordingSession->capture(inputChannelData, numInputChannels, captureOffset, captureSamples);
}

bool AudioEngine::hasMonitoredAudioTracks(const TrackDataModel::RenderStructureSnapshot* structure) const noexcept
{
    if (structure == nullptr) return false;
    for (size_t index = 0; index < structure->trackCount; ++index)
        if (isInputMonitoringEligible(structure->tracks[index]))
            return true;
    return false;
}

bool AudioEngine::isInputMonitoringEligible(const TrackDataModel::RenderTrack& track) const noexcept
{
    return resolveMonitor(track).emit;
}

AudioEngine::MonitorDecision AudioEngine::resolveMonitor(const TrackDataModel::RenderTrack& track) const noexcept
{
    const auto eligible = track.type == TrackType::audio
        && (track.inputMonitoring || (track.autoInputMonitoring && track.armed));
    if (! isSoftwareMonitoringEnabled() || ! eligible || ! track.monitorPermitted
        || track.monitorDestination == MonitorDestination::off)
        return {};

    return { true, track.monitorDestination };
}

void AudioEngine::restoreLiveVocalMonitorRoutes() noexcept
{
    if (dataModel == nullptr)
        return;

    const auto& routing = dataModel->getControlRoutingProfile();
    for (const auto target : { ControlTarget::mic1, ControlTarget::mic2 })
    {
        const auto* route = routing.find(target, ControlEffect::pitchCorrection);
        if (route == nullptr)
            continue;

        const auto index = dataModel->getTrackIndex(route->track);
        if (index < 0 || dataModel->getTrack(static_cast<size_t>(index)).type != TrackType::audio)
            continue;

        const auto& track = dataModel->getTrack(static_cast<size_t>(index));
        dataModel->setTrackInputConfiguration(static_cast<size_t>(index), false, true,
                                              track.inputChannel.load(std::memory_order_relaxed),
                                              track.inputChannelCount.load(std::memory_order_relaxed));
        dataModel->setTrackMonitorDestination(static_cast<size_t>(index), MonitorDestination::master);
        dataModel->setTrackMonitorPermission(static_cast<size_t>(index), true);
    }
}

void AudioEngine::renderInputMonitoring(const float* const* inputChannelData, int numInputChannels,
                                        const TrackDataModel::RenderStructureSnapshot* structure,
                                        int numSamples) noexcept
{
    if (structure == nullptr
        || inputChannelData == nullptr || numInputChannels <= 0) return;

    constexpr size_t maximumSafeInputChannels = 64;
    // Monitoring is unity-gain like a hardware console. Feedback safety comes
    // from the explicit global LIVE gate, not from silently attenuating input.
    constexpr float liveSoftwareMonitorGain = 1.0f;
    std::array<int, maximumSafeInputChannels> monitorOwner;
    monitorOwner.fill(-1);
    const auto focusedTrack = focusedMonitorTrackId.load(std::memory_order_acquire);

    // In a live setup, distinct physical inputs may all be monitored. When
    // several tracks reference one input, only one reaches the master. The
    // currently focused track wins the tie, otherwise the first route wins.
    for (size_t trackIndex = 0; trackIndex < structure->trackCount; ++trackIndex)
    {
        const auto& track = structure->tracks[trackIndex];
        if (! isInputMonitoringEligible(track)
            || (isFocusedInputMonitoringEnabled() && focusedTrack != 0 && track.id.value != focusedTrack)
            || track.inputChannel < 0
            || track.inputChannel >= static_cast<int>(maximumSafeInputChannels))
            continue;

        if (callbackInputForPhysicalChannel(track.inputChannel) < 0)
            continue;

        auto& owner = monitorOwner[static_cast<size_t>(track.inputChannel)];
        if (owner < 0 || track.id.value == focusedTrack)
            owner = static_cast<int>(trackIndex);
    }

    for (size_t trackIndex = 0; trackIndex < structure->trackCount; ++trackIndex)
    {
        const auto& track = structure->tracks[trackIndex];
        if (! isInputMonitoringEligible(track)
            || (isFocusedInputMonitoringEnabled() && focusedTrack != 0 && track.id.value != focusedTrack))
            continue;
        if (track.inputChannel < 0
            || track.inputChannel >= static_cast<int>(maximumSafeInputChannels)
            || monitorOwner[static_cast<size_t>(track.inputChannel)] != static_cast<int>(trackIndex))
            continue;

        const auto monitor = resolveMonitor(track);
        // Master-bound live input must enter the track buffer before inserts.
        // This is the only path by which a monitored vocal reaches Master.
        auto* destination = monitor.destination == MonitorDestination::master ? &trackBuffers[trackIndex]
                          : monitor.destination == MonitorDestination::cue ? &cueMonitorBuffer
                          : &streamMonitorBuffer;
        const auto anyTrackSoloed = [&] () noexcept
        {
            for (size_t index = 0; index < structure->trackCount; ++index)
                if (structure->tracks[index].solo)
                    return true;
            return false;
        }();
        const auto audible = ! track.muted && (! anyTrackSoloed || track.solo || track.soloSafe);
        if (! audible)
            continue;
        const auto gain = juce::jmax(0.0f, track.volume);
        const auto angle = (juce::jlimit(-1.0f, 1.0f, track.pan) + 1.0f)
            * (juce::MathConstants<float>::pi * 0.25f);
        for (int channel = 0; channel < destination->getNumChannels(); ++channel)
        {
            const auto physicalInput = track.inputChannel + (track.inputChannelCount == 2 ? channel : 0);
            const auto inputIndex = callbackInputForPhysicalChannel(physicalInput);
            if (inputIndex < 0 || inputIndex >= numInputChannels)
                continue;
            if (const auto* input = inputChannelData[inputIndex]; input != nullptr)
            {
                // Master-bound audio is panned/faded later by processTrackAudio
                // after the full insert chain. Cue/stream-only preserve their
                // existing monitor semantics until they gain dedicated FX buses.
                const auto monitorGain = monitor.destination == MonitorDestination::master ? liveSoftwareMonitorGain
                    : liveSoftwareMonitorGain * (monitor.destination == MonitorDestination::cue ? 1.0f : gain);
                destination->addFrom(channel, 0, input, numSamples, monitorGain);
            }
        }
    }
}

void AudioEngine::updateTrackInputPeaks(const float* const* inputChannelData, int numInputChannels,
                                        const TrackDataModel::RenderStructureSnapshot* structure,
                                        int numSamples) noexcept
{
    const auto trackCount = structure != nullptr ? structure->trackCount : 0;
    for (size_t trackIndex = 0; trackIndex < trackInputPeaks.size(); ++trackIndex)
    {
        if (trackIndex >= trackCount || inputChannelData == nullptr || numInputChannels <= 0)
        {
            trackInputPeaks[trackIndex].store(0.0f, std::memory_order_relaxed);
            continue;
        }

        const auto& track = structure->tracks[trackIndex];
        if (track.type != TrackType::audio || track.inputChannel < 0
            || callbackInputForPhysicalChannel(track.inputChannel) < 0)
        {
            trackInputPeaks[trackIndex].store(0.0f, std::memory_order_relaxed);
            continue;
        }

        auto peak = 0.0f;
        for (int offset = 0; offset < track.inputChannelCount; ++offset)
        {
            const auto inputIndex = callbackInputForPhysicalChannel(track.inputChannel + offset);
            if (inputIndex < 0 || inputIndex >= numInputChannels || inputChannelData[inputIndex] == nullptr)
                continue;
            const auto range = juce::FloatVectorOperations::findMinAndMax(inputChannelData[inputIndex], numSamples);
            peak = juce::jmax(peak, juce::jmax(std::abs(range.getStart()), std::abs(range.getEnd())));
        }
        trackInputPeaks[trackIndex].store(peak, std::memory_order_relaxed);
    }
}

void AudioEngine::clearRealtimeBuffers(int numSamples) noexcept
{
    processingBuffer.clear(0, numSamples);
    directPathBuffer.clear(0, numSamples);
    liveKeyReferenceBuffer.clear(0, numSamples);
    streamMonitorBuffer.clear(0, numSamples);
    cueMonitorBuffer.clear(0, numSamples);
    for (auto& trackBuffer : trackBuffers)
        trackBuffer.clear(0, numSamples);
    for (auto& busBuffer : busBuffers)
        busBuffer.clear(0, numSamples);
    for (auto& busPeak : busPeaks)
        busPeak.store(0.0f, std::memory_order_relaxed);
}

void AudioEngine::renderMetronome(double blockStartSample, int numSamples) noexcept
{
    if (dataModel == nullptr || processingBuffer.getNumChannels() == 0)
        return;

    const auto sampleRate = juce::jmax(1.0, activeSampleRate.load(std::memory_order_relaxed));
    const auto bpm = dataModel->getTempoAtSample(blockStartSample);
    const auto samplesPerBeat = sampleRate * 60.0 / juce::jmax(1.0, bpm);
    const auto beatsPerBar = juce::jmax(1, dataModel->getTimeSignatureNumerator());
    const auto firstBeat = static_cast<long long>(std::floor(blockStartSample / samplesPerBeat));
    const auto lastBeat = static_cast<long long>(std::floor((blockStartSample + numSamples - 1) / samplesPerBeat));
    constexpr int clickLength = 192;

    for (auto beat = firstBeat; beat <= lastBeat; ++beat)
    {
        const auto beatSample = static_cast<double>(beat) * samplesPerBeat;
        const auto offset = static_cast<int>(std::llround(beatSample - blockStartSample));
        const auto start = juce::jmax(0, offset);
        const auto end = juce::jmin(numSamples, offset + clickLength);
        if (start >= end)
            continue;

        const auto accent = (beat % beatsPerBar) == 0;
        const auto frequency = accent ? 1760.0 : 1320.0;
        const auto amplitude = accent ? 0.16f : 0.10f;
        for (int sample = start; sample < end; ++sample)
        {
            const auto age = sample - offset;
            const auto envelope = std::exp(-static_cast<float>(age) / 42.0f);
            const auto value = amplitude * envelope * std::sin(static_cast<float>(juce::MathConstants<double>::twoPi
                * frequency * static_cast<double>(age) / sampleRate));
            for (int channel = 0; channel < processingBuffer.getNumChannels(); ++channel)
                processingBuffer.addSample(channel, sample, value);
        }
    }
}

void AudioEngine::appendIncomingMidiEvents(const TrackDataModel::RenderStructureSnapshot& structure,
                                           double blockStartSample) noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    incomingMidiFifo.prepareToRead(incomingMidiCapacity, start1, size1, start2, size2);
    const auto appendRange = [this, &structure, blockStartSample] (int start, int count) noexcept
    {
        for (int offset = 0; offset < count; ++offset)
        {
            const auto& event = incomingMidiEvents[static_cast<size_t>(start + offset)];
            const auto channelMatches = [&event] (const TrackDataModel::RenderTrack& track) noexcept
            {
                return track.midiInputChannel == 0 || track.midiInputChannel == event.message.channel;
            };
            const auto acceptsMidi = [&channelMatches, &event] (const TrackDataModel::RenderTrack& track) noexcept
            {
                return (track.type == TrackType::instrument || track.type == TrackType::externalMidi)
                    && channelMatches(track)
                    && (track.midiInputDeviceToken == 0 || track.midiInputDeviceToken == event.sourceDeviceToken);
            };

            bool hasArmedMidiTrack = false;
            for (size_t trackIndex = 0; trackIndex < structure.trackCount; ++trackIndex)
                hasArmedMidiTrack = hasArmedMidiTrack || (structure.tracks[trackIndex].armed && acceptsMidi(structure.tracks[trackIndex]));

            bool deliveredToRecordingTrack = false;
            for (size_t trackIndex = 0; trackIndex < structure.trackCount; ++trackIndex)
            {
                const auto& track = structure.tracks[trackIndex];
                if (! acceptsMidi(track) || (hasArmedMidiTrack && ! track.armed))
                    continue;

                trackMidiBuffers[trackIndex].addEvent(event.message.toMessage(), 0);
                deliveredToRecordingTrack = deliveredToRecordingTrack || track.id == midiRecordingTrack;
            }

            const auto isNote = event.message.type == MidiMessageType::noteOn || event.message.type == MidiMessageType::noteOff;
            if (isNote && deliveredToRecordingTrack && midiRecordingActive.load(std::memory_order_acquire))
            {
                midiRecordingCallbackUsers.fetch_add(1, std::memory_order_acq_rel);
                if (midiRecordingActive.load(std::memory_order_acquire))
                {
                    int writeStart1 = 0, writeSize1 = 0, writeStart2 = 0, writeSize2 = 0;
                    recordedMidiFifo.prepareToWrite(1, writeStart1, writeSize1, writeStart2, writeSize2);
                    if (writeSize1 > 0)
                    {
                        recordedMidiEvents[static_cast<size_t>(writeStart1)] = {
                            blockStartSample, event.message.data1, event.message.data2 / 127.0f,
                            event.message.channel, event.message.type == MidiMessageType::noteOn
                        };
                        recordedMidiFifo.finishedWrite(1);
                    }
                    else
                    {
                        droppedMidiRecordingEvents.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                midiRecordingCallbackUsers.fetch_sub(1, std::memory_order_acq_rel);
            }
        }
    };
    appendRange(start1, size1);
    appendRange(start2, size2);
    incomingMidiFifo.finishedRead(size1 + size2);
}

void AudioEngine::renderInstrument(const TrackDataModel::RenderStructureSnapshot& structure, int numSamples) noexcept
{
    const auto rate = dataModel != nullptr ? dataModel->getSampleRate() : 44100.0;
    for (size_t trackIndex = 0; trackIndex < structure.trackCount; ++trackIndex)
    {
        if (structure.tracks[trackIndex].type != TrackType::instrument)
            continue;

        auto events = trackMidiBuffers[trackIndex].begin();
        const auto eventsEnd = trackMidiBuffers[trackIndex].end();
        juce::MidiMessage message;
        int eventSample = 0;
        const auto readNextEvent = [&]() noexcept
        {
            if (events == eventsEnd)
                return false;

            const auto metadata = *events++;
            message = metadata.getMessage();
            eventSample = metadata.samplePosition;
            return true;
        };
        auto hasEvent = readNextEvent();
        auto& voice = instrumentVoices[trackIndex];
        for (int sample = 0; sample < numSamples; ++sample)
        {
            while (hasEvent && eventSample <= sample)
            {
                if (message.isNoteOn())
                {
                    voice.active = true;
                    voice.pitch = message.getNoteNumber();
                    voice.velocity = message.getFloatVelocity();
                }
                else if (message.isNoteOff() && voice.pitch == message.getNoteNumber())
                {
                    voice.active = false;
                }
                hasEvent = readNextEvent();
            }
            if (! voice.active) continue;
            const auto frequency = 440.0 * std::pow(2.0, (voice.pitch - 69) / 12.0);
            const auto value = static_cast<float>(std::sin(voice.phase) * voice.velocity * 0.2);
            voice.phase += (juce::MathConstants<double>::twoPi * frequency) / rate;
            if (voice.phase >= juce::MathConstants<double>::twoPi) voice.phase -= juce::MathConstants<double>::twoPi;
            for (int channel = 0; channel < trackBuffers[trackIndex].getNumChannels(); ++channel)
                trackBuffers[trackIndex].addSample(channel, sample, value);
        }
    }
}

void AudioEngine::renderClipSegment(const std::vector<AudioClipState>& clips,
                                    const TrackDataModel::RenderStructureSnapshot& structure,
                                    double segmentStartSample, int destinationOffset,
                                    int numSamples) noexcept
{
    const auto segmentEndSample = segmentStartSample + numSamples;
    for (const auto& clip : clips)
    {
        if (clip.takeGroupId != 0 && ! clip.takeActive)
            continue;
        const auto trackIndex = structure.getTrackIndex(clip.trackId);
        if (clip.cachedBuffer == nullptr || clip.cachedBuffer->getNumChannels() == 0 || trackIndex < 0
            || static_cast<size_t>(trackIndex) >= trackBuffers.size())
            continue;

        const auto overlapStart = juce::jmax(segmentStartSample, clip.startSample);
        const auto overlapEnd = juce::jmin(segmentEndSample, clip.startSample + clip.durationSamples);
        if (overlapEnd <= overlapStart)
            continue;

        const auto segmentOffset = destinationOffset + static_cast<int>(overlapStart - segmentStartSample);
        const auto sourceOffset = static_cast<int>(clip.sourceOffsetSamples + overlapStart - clip.startSample);
        if (! clip.loopEnabled && (sourceOffset < 0 || sourceOffset >= clip.cachedBuffer->getNumSamples()))
            continue;
        const auto samplesToCopy = static_cast<int>(overlapEnd - overlapStart);
        if (samplesToCopy <= 0)
            continue;
        for (int sample = 0; sample < samplesToCopy; ++sample)
        {
            const auto clipSample = overlapStart - clip.startSample + sample;
            auto gain = clip.gain;
            if (clip.fadeInSamples > 0.0 && clipSample < clip.fadeInSamples)
                gain *= static_cast<float>(clipSample / clip.fadeInSamples);
            const auto fadeOutStart = clip.durationSamples - clip.fadeOutSamples;
            if (clip.fadeOutSamples > 0.0 && clipSample > fadeOutStart)
                gain *= static_cast<float>((clip.durationSamples - clipSample) / clip.fadeOutSamples);
            for (int channel = 0; channel < trackBuffers[static_cast<size_t>(trackIndex)].getNumChannels(); ++channel)
            {
                const auto sourceChannel = juce::jmin(channel, clip.cachedBuffer->getNumChannels() - 1);
                auto sourceSample = sourceOffset + sample;
                if (clip.loopEnabled)
                {
                    const auto loopLength = juce::jmax(1, static_cast<int>(std::round(clip.loopLengthSamples)));
                    sourceSample = static_cast<int>(clip.sourceOffsetSamples)
                        + ((sourceOffset + sample - static_cast<int>(clip.sourceOffsetSamples)) % loopLength);
                }
                if (sourceSample >= 0 && sourceSample < clip.cachedBuffer->getNumSamples())
                    trackBuffers[static_cast<size_t>(trackIndex)].addSample(channel, segmentOffset + sample,
                        clip.cachedBuffer->getSample(sourceChannel, sourceSample) * gain);
            }
        }
    }
}
