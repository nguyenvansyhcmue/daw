#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <array>
#include <memory>
#include <vector>
#include <chrono>
#include <functional>

#include "AudioGraph.h"
#include "AudioDiagnosticTracer.h"
#include "AudioEffectProcessor.h"
#include "LiveKeyDetector.h"
#include "TrackMixing.h"
#include "TransportUtils.h"
#include "../Recording/RecordingSession.h"
#include "../Models/TrackDataModel.h"
#include "../Core/WorkspaceProfile.h"

class AudioEngine final : public juce::AudioIODeviceCallback,
                          public juce::MidiInputCallback,
                          private juce::ChangeListener
{
public:
    struct RealtimeDiagnostics
    {
        double sampleRate = 0.0;
        int bufferSize = 0;
        int inputLatencySamples = 0;
        int outputLatencySamples = 0;
        int pluginLatencySamples = 0;
        int protectedPluginCount = 0;
        float callbackLoad = 0.0f;
        uint64_t overloadCount = 0;
        uint64_t midiInputOverflowCount = 0;
        uint64_t midiRecordingOverflowCount = 0;
    };

    explicit AudioEngine(TrackDataModel* model = nullptr);
    ~AudioEngine() override;

    void initialise();
    void shutdown();
    void saveAudioDeviceState() const;

    void setMasterGain(float gain) noexcept;
    float getMasterGain() const noexcept;
    struct MasterOutputState
    {
        float gain = 1.0f;
        float pan = 0.0f;
        float width = 1.0f;
        bool mono = false;
        bool dim = false;
        bool muted = false;
    };
    MasterOutputState getMasterOutputState() const noexcept;
    void setMasterOutputState(const MasterOutputState& state) noexcept;
    enum class MasterMidiTarget : uint8_t { fader, pan, width, mono, dim, mute, count };
    void beginMasterMidiLearn(MasterMidiTarget target) noexcept;
    int getMasterMidiCc(MasterMidiTarget target) const noexcept;
    void setMasterPan(float pan) noexcept;
    float getMasterPan() const noexcept;
    void setMasterWidth(float width) noexcept;
    float getMasterWidth() const noexcept;
    void setMasterDim(bool enabled) noexcept;
    bool isMasterDim() const noexcept;
    void setMasterMono(bool enabled) noexcept;
    bool isMasterMono() const noexcept;
    void setMasterMuted(bool enabled) noexcept;
    bool isMasterMuted() const noexcept;

    void setTempo(double newTempo) noexcept;
    double getTempo() const noexcept;

    void setPlaybackState(bool shouldPlay) noexcept;
    bool isPlaying() const noexcept;
    void setMetronomeEnabled(bool enabled) noexcept;
    bool isMetronomeEnabled() const noexcept;
    void setCountInMetronomeActive(bool active) noexcept;
    // Applies only the realtime-safe projection of a workspace profile. Device
    // changes happen on the message thread in this call, never in callback.
    juce::Result applyWorkspaceProfile(const WorkspaceProfile& profile);
    juce::Result validateWorkspaceProfile(const WorkspaceProfile& profile) const;
    WorkspaceMode getWorkspaceMode() const noexcept;
    void setPanicMute(bool enabled) noexcept;
    void startAudioTrace();
    void stopAudioTrace();
    void markAudioTraceIssue() noexcept;
    // Diagnostic-only selection. This never changes routing or plug-in state;
    // it chooses the slot whose pre/post-FX evidence is written to the trace.
    void selectPluginTraceTarget(size_t trackIndex, size_t slot) noexcept;
    void clearPluginTraceTarget() noexcept;
    bool isPanicMuteEnabled() const noexcept;
    void setStreamMuted(bool muted) noexcept;
    bool isStreamMuted() const noexcept;
    float getStreamPeak() const noexcept;
    juce::StringArray getAvailableAudioOutputDevices() const;
    juce::Result selectStreamOutputDevice(const juce::String& deviceName);
    juce::String getStreamOutputDevice() const;
    bool isStreamOutputConfigured() const noexcept;
    juce::Result validateMonitorRoute(size_t trackIndex, MonitorDestination destination,
                                      bool allowSpeakerRisk = false) const;
    juce::Result enableTrackMonitor(size_t trackIndex, MonitorDestination destination,
                                    bool allowSpeakerRisk = false);
    void disableTrackMonitor(size_t trackIndex) noexcept;
    void setLivePerformanceEnabled(bool enabled) noexcept;
    bool isLivePerformanceEnabled() const noexcept;
    bool isLiveLowLatencyProtectionEnabled() const noexcept;
    // Global monitor policy is deliberately independent from each track's
    // In/Auto/Off state. The audio callback reads only atomics here.
    void setSoftwareMonitoringEnabled(bool enabled) noexcept;
    bool isSoftwareMonitoringEnabled() const noexcept;
    void setFocusedInputMonitoringEnabled(bool enabled) noexcept;
    bool isFocusedInputMonitoringEnabled() const noexcept;
    void setLiveLowLatencyProtectionEnabled(bool enabled) noexcept;
    void setStudioPluginDelayCompensationEnabled(bool enabled) noexcept;
    bool isStudioPluginDelayCompensationEnabled() const noexcept;
    // The UI selection is also the tie-breaker when several live tracks are
    // routed from the same physical input. It prevents accidental summing of
    // one microphone into the master output while retaining every input meter.
    void setFocusedMonitorTrack(TrackId track) noexcept;
    juce::Result optimiseDeviceForLivePerformance();
    int getRecommendedLiveBufferSize() noexcept;
    bool needsLiveBufferOptimisation() noexcept;

    juce::AudioDeviceManager& getAudioDeviceManager() noexcept;
    // Device-setup mutations belong to the engine. UI code may request one
    // of these explicit operations, but must never edit AudioDeviceSetup
    // directly while a live callback is running.
    juce::Result enableAllAvailableInputChannels();
    juce::Result selectStereoOutputPair(int firstPhysicalOutputChannel);
    // Physical device channels are stable project-routing identifiers. They
    // are deliberately distinct from compact callback buffer indices.
    juce::Array<int> getActiveAudioInputChannels() const;
    void refreshMidiInputDevices();
    juce::Array<juce::MidiDeviceInfo> getAvailableMidiOutputDevices() const;
    juce::String getMidiOutputDeviceIdentifier() const;
    // Control-thread operation. An empty identifier disconnects MIDI output.
    juce::Result setMidiOutputDevice(const juce::String& identifier);
    float getTrackPeak(size_t trackIndex) const noexcept;
    float getTrackInputPeak(size_t trackIndex) const noexcept;
    float getBusPeak(size_t busIndex) const noexcept;
    float getMasterPeak() const noexcept;
    float getMasterLeftPeak() const noexcept;
    float getMasterRightPeak() const noexcept;
    RealtimeDiagnostics getRealtimeDiagnostics() const noexcept;
    int getEstimatedLivePluginLatencySamples() const noexcept;
    int getLiveProtectedPluginCount() const noexcept;
    LiveKeyDetector::Result getLiveKeyResult() const noexcept;
    void resetLiveKeyDetection() noexcept { liveKeyDetector.reset(); }
    // The musical reference is intentionally explicit. A zero TrackId disables
    // live Auto-Key rather than analysing vocals mixed into the master bus.
    void setLiveKeyReferenceTrack(TrackId track) noexcept;
    TrackId getLiveKeyReferenceTrack() const noexcept;
    [[deprecated("Use PitchCorrectionIntegrationService and StudioForgeControlBridge")]] int applyDetectedKeyToPitchCorrection();
    [[deprecated("Use PitchCorrectionIntegrationService and StudioForgeControlBridge")]] int applyKeyToPitchCorrection(int rootNote, bool minor);
    void setFxProcessor(size_t trackIndex, size_t slot,
                        std::shared_ptr<AudioEffectProcessor> processor);
    void setFxBypassed(size_t trackIndex, size_t slot, bool bypassed) noexcept;
    const TrackDataModel::FxRackSnapshot* getMasterFxRackSnapshot() const noexcept;
    void setMasterFxProcessor(size_t slot, std::shared_ptr<AudioEffectProcessor> processor);
    void setMasterFxBypassed(size_t slot, bool bypassed);
    void prepareActiveEffects();
    struct AudioRecordingTarget
    {
        size_t trackIndex = 0;
        juce::File destination;
    };
    juce::Result startRecording(size_t trackIndex, const juce::File& destination,
                                double timelineStartSample = -1.0);
    juce::Result startRecordings(const std::vector<AudioRecordingTarget>& targets,
                                 double timelineStartSample = -1.0);
    // Runs the same device/input checks as recording without opening files or
    // altering transport state. Use it before a count-in begins.
    juce::Result validateRecordingTargets(const std::vector<AudioRecordingTarget>& targets) const;
    juce::Result stopRecording();
    bool isRecording() const noexcept;
    // Consumed on the message thread to end an interrupted take safely after
    // the device callback reports that its interface disappeared.
    bool consumeRecordingInterruptedByDeviceDisconnect() noexcept;
    // Device callbacks only set this flag. The message thread clears model
    // permissions, keeping device-loss handling realtime-safe.
    juce::Result startMidiRecording(size_t trackIndex);
    MidiClipId stopMidiRecording();
    bool isMidiRecording() const noexcept;
    void setRecordingCaptureRange(double startSample, double endSample) noexcept;
    void clearRecordingCaptureRange() noexcept;
    size_t getScheduledMidiEventCount() const noexcept { return scheduledMidiEvents.size(); }
    struct OfflineRenderOptions
    {
        double sampleRate = 44100.0;
        int blockSize = 512;
        bool useCycle = false;
        int bitDepth = 24;
        // Offline-only observer used by stem/selected-track exporters. It is
        // called after each rendered block, before the next block is started.
        std::function<void(size_t, const juce::AudioBuffer<float>&, int)> trackBlockCallback;
        std::function<bool(size_t)> trackExportFilter;
    };

    juce::Result renderOfflineAudio(const juce::File& destination, const OfflineRenderOptions& options);
    juce::Result renderOfflineStems(const juce::File& directory, const OfflineRenderOptions& options);
    juce::Result renderOfflineWav(const juce::File& destination, double sampleRate,
                                  int blockSize = 512, bool useCycle = false);

    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;

private:
    struct MonitorDecision
    {
        bool emit = false;
        MonitorDestination destination = MonitorDestination::off;
    };
    class CallbackTimingScope
    {
    public:
        CallbackTimingScope(AudioEngine& owner, int samples) noexcept;
        ~CallbackTimingScope();

    private:
        AudioEngine& engine;
        int numSamples;
        std::chrono::steady_clock::time_point started;
    };

    void renderClipSegment(const std::vector<AudioClipState>& clips,
                           const TrackDataModel::RenderStructureSnapshot& structure,
                           double segmentStartSample, int destinationOffset,
                           int numSamples) noexcept;
    void renderInstrument(const TrackDataModel::RenderStructureSnapshot& structure, int numSamples) noexcept;
    void publishMasterFxRack(std::unique_ptr<const TrackDataModel::FxRackSnapshot> snapshot);
    void prepareRack(const TrackDataModel::FxRackSnapshot* rack, double sampleRate, int blockSize);
    int rackLatencySamples(const TrackDataModel::FxRackSnapshot* rack) const noexcept;
    int maximumTrackRackLatency(const TrackDataModel::RenderStructureSnapshot& structure) const noexcept;
    int maximumActiveBusRackLatency(const TrackDataModel::RenderStructureSnapshot& structure) const noexcept;
    void applyTrackLatencyCompensation(size_t trackIndex, juce::AudioBuffer<float>& buffer,
                                       int delaySamples, int numSamples) noexcept;
    void applyBusLatencyCompensation(size_t busIndex, juce::AudioBuffer<float>& buffer,
                                     int delaySamples, int numSamples) noexcept;
    void applyDirectPathLatencyCompensation(int delaySamples, int numSamples) noexcept;
    void stageDirectPathForBusCompensation(const TrackDataModel::RenderStructureSnapshot* structure,
                                           int numSamples) noexcept;
    int protectedPluginCount(const TrackDataModel::FxRackSnapshot* rack) const noexcept;
    bool shouldProcessForLive(const AudioEffectProcessor& processor) const noexcept;
    void recordCallbackTiming(int numSamples, std::chrono::steady_clock::time_point started) noexcept;
    void appendIncomingMidiEvents(const TrackDataModel::RenderStructureSnapshot& structure,
                                  double blockStartSample) noexcept;
    void renderMetronome(double blockStartSample, int numSamples) noexcept;
    void clearRealtimeBuffers(int numSamples) noexcept;
    void captureRecordingInput(const float* const* inputChannelData, int numInputChannels,
                               int numSamples) noexcept;
    bool hasMonitoredAudioTracks(const TrackDataModel::RenderStructureSnapshot* structure) const noexcept;
    void renderInputMonitoring(const float* const* inputChannelData, int numInputChannels,
                               const TrackDataModel::RenderStructureSnapshot* structure,
                               int numSamples) noexcept;
    bool isInputMonitoringEligible(const TrackDataModel::RenderTrack& track) const noexcept;
    MonitorDecision resolveMonitor(const TrackDataModel::RenderTrack& track) const noexcept;
    // Live workspaces own the named vocal routes. Reapply them after CoreAudio
    // restarts so an interface reconnect cannot leave a visually armed singer
    // silently disconnected from the Master bus.
    void restoreLiveVocalMonitorRoutes() noexcept;
    void updateTrackInputPeaks(const float* const* inputChannelData, int numInputChannels,
                               const TrackDataModel::RenderStructureSnapshot* structure,
                               int numSamples) noexcept;
    int callbackInputForPhysicalChannel(int physicalChannel) const noexcept;
    bool isPhysicalInputRouteAvailable(int physicalChannel, int channelCount) const noexcept;
    void rebuildInputChannelMap(const juce::AudioIODevice* device) noexcept;
    void rebuildOutputChannelMap(const juce::AudioIODevice* device) noexcept;
    void recordDeviceSessionEvent(uint8_t kind) noexcept;
    // Control-thread recovery for a CoreAudio session that reports a device
    // but has stopped delivering callbacks. Playback has no transport clock
    // in that condition, so recover before declaring the transport running.
    void restartAudioDeviceIfCallbackStalled();
    // Runs only on JUCE's message thread after CoreAudio reports that its
    // device list changed. It never touches the realtime callback.
    void recoverPreferredAudioDeviceAfterHotPlug();
    // The mixer always renders a stereo Master bus. CoreAudio device changes
    // do not reliably retain an active output mask (notably on USB
    // interfaces), so ensure physical outputs 1/2 are explicitly available.
    juce::Result ensureCurrentStereoOutputPair();
    // Cue is a real hardware route, never an alias for the Master bus. It
    // needs outputs 3/4 in the primary device callback while preserving the
    // master pair and any explicitly active output channels.
    juce::Result ensureCurrentCueOutputPair();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    int callbackOutputForPhysicalChannel(int physicalChannel) const noexcept;
    void prepareTrackMidiBuffers(const TrackDataModel::RenderStructureSnapshot* structure) noexcept;
    void processTrackMidiEffects(const TrackDataModel::RenderStructureSnapshot* structure) noexcept;
    void sendExternalMidi(const TrackDataModel::RenderStructureSnapshot* structure) noexcept;
    void processTrackAudio(const TrackDataModel::RenderStructureSnapshot* structure,
                           double automationSample, bool anyTrackSoloed, int numSamples) noexcept;
    void processBusReturns(const TrackDataModel::RenderStructureSnapshot* structure, int numSamples) noexcept;
    void processMasterOutput(float* const* outputChannelData, int numOutputChannels,
                             int numSamples, bool isPlaying, double blockStartSample) noexcept;
    class StreamOutputBridge;
    juce::Result openStreamOutputDevice(const juce::String& deviceName);
    void closeStreamOutputDevice();
    void publishStreamOutput(const juce::AudioBuffer<float>& source, int numSamples) noexcept;
    void addTrackToBus(const TrackDataModel::RenderTrack& track,
                       size_t trackIndex,
                       const juce::AudioBuffer<float>& source,
                       const TrackDataModel::RenderStructureSnapshot* structure,
                       size_t route, float level, int numSamples) noexcept;

    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<juce::MidiOutput> midiOutput;
    std::atomic<juce::MidiOutput*> publishedMidiOutput { nullptr };
    std::atomic<unsigned int> midiOutputCallbackUsers { 0 };
    std::atomic<uint64_t> lastAudioCallbackMs { 0 };
    std::atomic<bool> restartingStalledAudioDevice { false };
    // Names loaded from audio-device.xml.  They deliberately survive a
    // fallback to the built-in device, so a DAW opened before a Scarlett can
    // reclaim the user's chosen interface as soon as it is plugged in.
    juce::String preferredInputDeviceName;
    juce::String preferredOutputDeviceName;
    // True only after a saved interface genuinely disappeared. It prevents an
    // explicit Device Settings selection from being overwritten by stale
    // launch preferences on the next JUCE change notification.
    bool preferredDeviceWasUnavailable = false;
    std::atomic<bool> recoveringPreferredAudioDevice { false };
    juce::String midiOutputDeviceIdentifier;
    AudioGraph graph;
    std::atomic<float> masterGain { 1.0f };
    std::atomic<float> masterPan { 0.0f };
    std::atomic<float> masterWidth { 1.0f };
    std::atomic<bool> masterDim { false }, masterMono { false }, masterMuted { false };
    std::array<std::atomic<int>, static_cast<size_t>(MasterMidiTarget::count)> masterMidiCc {};
    std::atomic<int> pendingMasterMidiLearn { -1 };
    std::atomic<bool> metronomeEnabled { false };
    std::atomic<bool> countInMetronomeActive { false };
    std::atomic<WorkspaceMode> workspaceMode { WorkspaceMode::studio };
    std::atomic<bool> liveLowLatencyProtectionEnabled { false };
    std::atomic<bool> liveLowLatencyMonitoringPreferred { true };
    std::atomic<bool> softwareMonitoringEnabled { true };
    std::atomic<bool> focusedInputMonitoringEnabled { false };
    std::atomic<bool> studioPluginDelayCompensationEnabled { true };
    // Final-output safety gate. It deliberately does not touch track mutes,
    // faders or project state, so releasing PANIC restores the exact mix.
    std::atomic<bool> panicMuteEnabled { false };
    std::atomic<bool> streamMuted { false };
    std::atomic<float> streamPeak { 0.0f };
    juce::String streamOutputDeviceName;
    std::atomic<bool> streamOutputConfigured { false };
    std::unique_ptr<juce::AudioIODevice> streamOutputDevice;
    std::unique_ptr<StreamOutputBridge> streamOutputBridge;
    std::atomic<StreamOutputBridge*> publishedStreamOutputBridge { nullptr };
    // Bridges retired while the primary callback is live remain allocated until
    // shutdown, so that a callback which observed the old atomic pointer can
    // finish safely without a lock or an allocation.
    std::vector<std::unique_ptr<StreamOutputBridge>> retiredStreamOutputBridges;
    int studioBufferSizeBeforeLive = 0;
    std::atomic<uint64_t> focusedMonitorTrackId { 0 };
    std::atomic<uint64_t> liveKeyReferenceTrackId { 0 };
    AudioDiagnosticTracer diagnosticTracer;
    std::atomic<int> pluginTraceTrackIndex { -1 };
    std::atomic<int> pluginTraceSlot { -1 };
    std::atomic<int> pluginTraceLatencySamples { 0 };
    std::atomic<float> pluginTracePreFxPeak { 0.0f };
    std::atomic<float> pluginTracePostFxPeak { 0.0f };
    std::atomic<uint8_t> pluginTracePresent { 0 };
    std::atomic<uint8_t> pluginTraceBypassed { 0 };
    std::atomic<uint8_t> pluginTraceProcessed { 0 };
    std::atomic<uint8_t> pluginTraceSkippedForLatency { 0 };
    LiveKeyDetector liveKeyDetector;
    juce::AudioBuffer<float> processingBuffer;
    juce::AudioBuffer<float> liveKeyReferenceBuffer;
    juce::AudioBuffer<float> streamBusBuffer;
    juce::AudioBuffer<float> streamMonitorBuffer;
    juce::AudioBuffer<float> cueMonitorBuffer;
    // The sole software-monitor bus. It is inserted after the project/master
    // processing, so input monitoring has no plug-in or automation latency.
    // Keeping one canonical path avoids the doubled/competing monitor lanes
    // that make a live route impossible to reason about.
    std::array<std::atomic<float>, TrackDataModel::maxTracks> trackPeaks {};
    std::array<std::atomic<float>, TrackDataModel::maxTracks> trackInputPeaks {};
    std::array<std::atomic<float>, TrackDataModel::maxBuses> busPeaks {};
    std::atomic<float> masterPeak { 0.0f };
    std::atomic<float> masterLeftPeak { 0.0f };
    std::atomic<float> masterRightPeak { 0.0f };
    std::atomic<double> activeSampleRate { 0.0 };
    std::atomic<int> activeBufferSize { 0 };
    std::atomic<int> activeInputLatencySamples { 0 };
    std::atomic<int> activeOutputLatencySamples { 0 };
    // A device stop is also emitted for harmless CoreAudio renegotiations.
    // Store its time and only surface an interruption if no matching start
    // arrives within the recovery grace period.
    std::atomic<uint64_t> recordingDeviceStopMs { 0 };
    std::atomic<float> callbackLoad { 0.0f };
    std::atomic<uint64_t> callbackOverloadCount { 0 };
    std::unique_ptr<const TrackDataModel::FxRackSnapshot> masterFxRack;
    std::atomic<const TrackDataModel::FxRackSnapshot*> publishedMasterFxRack { nullptr };
    std::vector<std::unique_ptr<const TrackDataModel::FxRackSnapshot>> retiredMasterFxRacks;
    std::array<juce::AudioBuffer<float>, TrackDataModel::maxTracks> trackBuffers;
    static constexpr int trackLatencyCompensationCapacity = 32768;
    std::array<juce::AudioBuffer<float>, TrackDataModel::maxTracks> trackLatencyDelayBuffers;
    std::array<int, TrackDataModel::maxTracks> trackLatencyDelayWrite {};
    std::array<juce::AudioBuffer<float>, TrackDataModel::maxBuses> busLatencyDelayBuffers;
    std::array<int, TrackDataModel::maxBuses> busLatencyDelayWrite {};
    juce::AudioBuffer<float> directPathBuffer;
    juce::AudioBuffer<float> directPathDelayBuffer;
    int directPathDelayWrite = 0;
    std::array<juce::AudioBuffer<float>, TrackDataModel::maxBuses> busBuffers;
    std::array<std::array<float, TrackDataModel::maxSendsPerTrack>, TrackDataModel::maxTracks> smoothedSendGains {};
    std::array<juce::MidiBuffer, TrackDataModel::maxTracks> trackMidiBuffers;
    MidiEventBuffer scheduledMidiEvents;
    struct IncomingMidiEvent
    {
        MidiRealtimeEvent message;
        uint32_t sourceDeviceToken = 0;
    };
    static constexpr int incomingMidiCapacity = 256;
    std::array<IncomingMidiEvent, incomingMidiCapacity> incomingMidiEvents {};
    juce::AbstractFifo incomingMidiFifo { incomingMidiCapacity };
    std::atomic<uint64_t> droppedMidiInputEvents { 0 };
    static constexpr int maximumPhysicalInputChannels = 64;
    std::array<std::atomic<int>, maximumPhysicalInputChannels> callbackInputByPhysicalChannel {};
    std::array<std::atomic<int>, maximumPhysicalInputChannels> callbackOutputByPhysicalChannel {};
    std::atomic<int> activeCallbackInputCount { 0 };
    juce::StringArray managedMidiInputIdentifiers;
    struct RecordedMidiEvent
    {
        double samplePosition = 0.0;
        int pitch = 60;
        float velocity = 0.0f;
        int channel = 1;
        bool noteOn = false;
    };
    static constexpr int recordedMidiCapacity = 2048;
    std::array<RecordedMidiEvent, recordedMidiCapacity> recordedMidiEvents {};
    juce::AbstractFifo recordedMidiFifo { recordedMidiCapacity };
    std::atomic<bool> midiRecordingActive { false };
    std::atomic<unsigned int> midiRecordingCallbackUsers { 0 };
    std::atomic<uint64_t> droppedMidiRecordingEvents { 0 };
    TrackId midiRecordingTrack;
    double midiRecordingStartSample = 0.0;
    struct InstrumentVoice { bool active = false; int pitch = 60; float velocity = 0.0f; double phase = 0.0; };
    std::array<InstrumentVoice, TrackDataModel::maxTracks> instrumentVoices {};
    std::unique_ptr<RecordingSession> recordingSession;
    std::atomic<bool> recordingCaptureRangeActive { false };
    std::atomic<double> recordingCaptureStartSample { 0.0 };
    std::atomic<double> recordingCaptureEndSample { 0.0 };
    TrackDataModel* dataModel = nullptr;
};
