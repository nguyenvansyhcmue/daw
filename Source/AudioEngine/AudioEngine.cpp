#include "AudioEngine.h"
#include "GainUtilityProcessor.h"

#include <algorithm>
#include <cmath>

namespace
{
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
}

AudioEngine::AudioEngine(TrackDataModel* model)
    : dataModel(model)
{
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
    deviceManager.initialiseWithDefaultDevices(2, 2);
    deviceManager.addAudioCallback(this);
    deviceManager.addMidiInputDeviceCallback({}, this);
    setMasterGain(1.0f);
}

void AudioEngine::shutdown()
{
    if (recordingSession != nullptr) recordingSession->stop();
    deviceManager.removeMidiInputDeviceCallback({}, this);
    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
    processingBuffer.clear();
}

void AudioEngine::setMasterGain(float gain) noexcept
{
    masterGain.store(juce::jlimit(0.0f, 2.0f, gain));
    graph.setMasterGain(masterGain.load());
}

float AudioEngine::getMasterGain() const noexcept
{
    return masterGain.load();
}

void AudioEngine::setTempo(double newTempo) noexcept
{
    if (dataModel != nullptr)
        dataModel->setBpm(newTempo);
}

void AudioEngine::setMetronomeEnabled(bool enabled) noexcept
{
    metronomeEnabled.store(enabled, std::memory_order_release);
}

bool AudioEngine::isMetronomeEnabled() const noexcept
{
    return metronomeEnabled.load(std::memory_order_acquire);
}

juce::Result AudioEngine::startRecording(size_t trackIndex, const juce::File& destination,
                                         double timelineStartSample)
{
    if (recordingSession == nullptr) return juce::Result::fail("Audio engine has no project model");
    if (trackIndex >= dataModel->getTrackCount()
        || dataModel->getTrack(trackIndex).type != TrackType::audio)
        return juce::Result::fail("Recording requires an audio track");
    auto* device = deviceManager.getCurrentAudioDevice();
    const auto sampleRate = device != nullptr ? device->getCurrentSampleRate() : dataModel->getSampleRate();
    const auto inputChannels = device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 2;
    const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : processingBuffer.getNumSamples();
    const auto inputChannel = dataModel->getTrack(trackIndex).inputChannel.load(std::memory_order_relaxed) % inputChannels;
    const auto startSample = timelineStartSample >= 0.0 ? timelineStartSample : dataModel->getPlayheadPosition();
    const auto result = recordingSession->start(trackIndex, destination, startSample, sampleRate,
                                                inputChannel, blockSize);
    if (result.wasOk()) dataModel->setTrackArmed(trackIndex, true);
    return result;
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

    destination.deleteFile();
    auto stream = destination.createOutputStream();
    if (stream == nullptr) return juce::Result::fail("Cannot create offline render file");
    auto* rawStream = stream.release();
    std::unique_ptr<juce::AudioFormatWriter> writer(format->createWriterFor(rawStream, options.sampleRate, 2, bitDepth, {}, 0));
    if (writer == nullptr)
    {
        delete rawStream;
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
        dataModel->setPlayheadPosition(previousPlayhead);
        dataModel->setPlaying(wasPlaying);
        if (cycleWasActive) dataModel->setCycle(cycleStart, cycleEnd); else dataModel->clearCycle();
        dataModel->restoreProjectRevision(previousProjectRevision);
    };
    dataModel->setSampleRate(options.sampleRate);
    if (! options.useCycle) dataModel->clearCycle();
    dataModel->setPlayheadPosition(options.useCycle && cycleWasActive ? cycleStart : 0.0);
    dataModel->setPlaying(true);
    processingBuffer.setSize(2, options.blockSize, false, true, true);
    for (auto& buffer : trackBuffers) buffer.setSize(2, options.blockSize, false, true, true);
    for (auto& buffer : busBuffers) buffer.setSize(2, options.blockSize, false, true, true);
    for (auto& midi : trackMidiBuffers) midi.ensureSize(MidiEventBuffer::capacity * 4);
    juce::AudioBuffer<float> renderBuffer(2, options.blockSize);
    float* outputs[] { renderBuffer.getWritePointer(0), renderBuffer.getWritePointer(1) };
    const auto totalSamples = static_cast<int64_t>(std::ceil(endSample));
    int64_t rendered = 0;
    while (rendered < totalSamples)
    {
        const auto count = static_cast<int>(juce::jmin<int64_t>(options.blockSize, totalSamples - rendered));
        audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, count, {});
        if (! writer->writeFromAudioSampleBuffer(renderBuffer, 0, count))
        {
            restoreTransport();
            return juce::Result::fail("Offline WAV write failed");
        }
        rendered += count;
    }
    restoreTransport();
    return juce::Result::ok();
}

double AudioEngine::getTempo() const noexcept
{
    return dataModel != nullptr ? dataModel->getBpm() : 120.0;
}

void AudioEngine::setPlaybackState(bool shouldPlay) noexcept
{
    if (dataModel != nullptr)
        dataModel->setPlaying(shouldPlay);
}

bool AudioEngine::isPlaying() const noexcept
{
    return dataModel != nullptr && dataModel->isPlaying();
}

juce::AudioDeviceManager& AudioEngine::getAudioDeviceManager() noexcept
{
    return deviceManager;
}

float AudioEngine::getTrackPeak(size_t trackIndex) const noexcept
{
    return trackIndex < trackPeaks.size() ? trackPeaks[trackIndex].load(std::memory_order_relaxed) : 0.0f;
}

float AudioEngine::getBusPeak(size_t busIndex) const noexcept
{
    return busIndex < busPeaks.size() ? busPeaks[busIndex].load(std::memory_order_relaxed) : 0.0f;
}

float AudioEngine::getMasterPeak() const noexcept
{
    return masterPeak.load(std::memory_order_relaxed);
}

float AudioEngine::getMasterLeftPeak() const noexcept
{
    return masterLeftPeak.load(std::memory_order_relaxed);
}

float AudioEngine::getMasterRightPeak() const noexcept
{
    return masterRightPeak.load(std::memory_order_relaxed);
}

AudioEngine::RealtimeDiagnostics AudioEngine::getRealtimeDiagnostics() const noexcept
{
    return { activeSampleRate.load(std::memory_order_relaxed),
             activeBufferSize.load(std::memory_order_relaxed),
             activeInputLatencySamples.load(std::memory_order_relaxed),
             activeOutputLatencySamples.load(std::memory_order_relaxed),
             callbackLoad.load(std::memory_order_relaxed),
             callbackOverloadCount.load(std::memory_order_relaxed),
             droppedMidiInputEvents.load(std::memory_order_relaxed),
             droppedMidiRecordingEvents.load(std::memory_order_relaxed) };
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
    const auto outputChannels = device != nullptr ? device->getActiveOutputChannels().countNumberOfSetBits() : 2;
    const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
    activeSampleRate.store(device != nullptr ? device->getCurrentSampleRate() : 0.0, std::memory_order_relaxed);
    activeBufferSize.store(blockSize, std::memory_order_relaxed);
    activeInputLatencySamples.store(device != nullptr ? device->getInputLatencyInSamples() : 0, std::memory_order_relaxed);
    activeOutputLatencySamples.store(device != nullptr ? device->getOutputLatencyInSamples() : 0, std::memory_order_relaxed);
    callbackLoad.store(0.0f, std::memory_order_relaxed);
    processingBuffer.setSize(juce::jmax(2, outputChannels), juce::jmax(1, blockSize),
                             false, true, true);
    for (auto& buffer : trackBuffers)
        buffer.setSize(2, processingBuffer.getNumSamples(), false, true, true);
    for (auto& buffer : busBuffers)
        buffer.setSize(2, processingBuffer.getNumSamples(), false, true, true);
    for (auto& midi : trackMidiBuffers)
        midi.ensureSize(MidiEventBuffer::capacity * 4);

    prepareActiveEffects();
}

void AudioEngine::audioDeviceStopped()
{
    processingBuffer.clear();
    activeSampleRate.store(0.0, std::memory_order_relaxed);
    activeBufferSize.store(0, std::memory_order_relaxed);
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    if (! message.isNoteOnOrOff())
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    incomingMidiFifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 == 0)
    {
        droppedMidiInputEvents.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    incomingMidiEvents[static_cast<size_t>(start1)] = { message.getNoteNumber(), static_cast<float>(message.getVelocity()) / 127.0f,
                                                         message.getChannel(), message.isNoteOn() };
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

    CallbackTimingScope callbackTiming(*this, numSamples);

    for (int channel = 0; channel < numOutputChannels; ++channel)
        juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);

    if (dataModel == nullptr || numSamples > processingBuffer.getNumSamples())
        return;

    if (recordingSession != nullptr)
    {
        auto captureOffset = 0;
        auto captureSamples = numSamples;
        if (recordingCaptureRangeActive.load(std::memory_order_acquire))
        {
            if (! dataModel->isPlaying())
                captureSamples = 0;
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

    const auto blockStartSample = dataModel->getPlayheadPosition();
    const auto snapshots = dataModel->acquireRealtimeSnapshot();
    const auto* clips = snapshots.getAudioClips();
    const auto* structure = snapshots.getRenderStructure();
    const auto* midiClips = snapshots.getMidiClips();
    const auto playing = dataModel->isPlaying();
    bool hasMonitoredAudio = false;
    for (size_t index = 0; structure != nullptr && index < structure->trackCount; ++index)
        hasMonitoredAudio = hasMonitoredAudio || (structure->tracks[index].type == TrackType::audio
            && structure->tracks[index].inputMonitoring);
    const auto hasPendingMidi = incomingMidiFifo.getNumReady() > 0;
    const auto hasActiveInstrument = std::any_of(instrumentVoices.begin(), instrumentVoices.end(),
                                                 [] (const InstrumentVoice& voice) { return voice.active; });
    if (! playing && ! hasMonitoredAudio && ! hasPendingMidi && ! hasActiveInstrument)
        return;

    processingBuffer.clear(0, numSamples);
    for (auto& buffer : trackBuffers)
        buffer.clear(0, numSamples);
    for (auto& buffer : busBuffers)
        buffer.clear(0, numSamples);
    for (auto& peak : busPeaks)
        peak.store(0.0f, std::memory_order_relaxed);

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

    if (structure != nullptr)
        appendIncomingMidiEvents(*structure, blockStartSample);

    bool anyTrackSoloed = false;
    const auto automationSample = dataModel->getPlayheadPosition();
    for (size_t trackIndex = 0; structure != nullptr && trackIndex < structure->trackCount; ++trackIndex)
        anyTrackSoloed = anyTrackSoloed || structure->tracks[trackIndex].solo;

    for (auto& midi : trackMidiBuffers)
        midi.clear();
    if (structure != nullptr)
    {
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

    if (structure != nullptr)
        for (size_t trackIndex = 0; trackIndex < structure->trackCount; ++trackIndex)
            if (const auto* rack = structure->tracks[trackIndex].fxRack; rack != nullptr)
                for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
                    if (const auto& processor = rack->processors[slot]; processor != nullptr && ! rack->bypass[slot]
                        && processor->isMidiEffect())
                        processor->processBlock(trackBuffers[trackIndex], trackMidiBuffers[trackIndex]);

    if (structure != nullptr)
        renderInstrument(*structure, numSamples);

    if (structure != nullptr && inputChannelData != nullptr && numInputChannels > 0)
        for (size_t trackIndex = 0; trackIndex < structure->trackCount; ++trackIndex)
        {
            const auto& track = structure->tracks[trackIndex];
            if (track.type != TrackType::audio || ! track.inputMonitoring) continue;
            auto& destination = trackBuffers[trackIndex];
            for (int channel = 0; channel < destination.getNumChannels(); ++channel)
            {
                const auto inputIndex = (track.inputChannel + channel) % numInputChannels;
                if (const auto* input = inputChannelData[inputIndex]; input != nullptr)
                    juce::FloatVectorOperations::add(destination.getWritePointer(channel), input, numSamples);
            }
        }

    for (size_t trackIndex = 0; structure != nullptr && trackIndex < structure->trackCount; ++trackIndex)
    {
        auto& trackBuffer = trackBuffers[trackIndex];
        const auto* rack = structure->tracks[trackIndex].fxRack;
        if (rack != nullptr)
        {
            for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
            {
                const auto& processor = rack->processors[slot];
                if (processor != nullptr && !rack->bypass[slot] && ! processor->isMidiEffect())
                    processor->processBlock(trackBuffer, trackMidiBuffers[trackIndex]);
            }
        }

        const auto& track = structure->tracks[trackIndex];
        const auto getSendLevel = [&track, automationSample] (size_t route) noexcept
        {
            const auto& send = track.sends[route];
            return send.automation.mode == AutomationMode::read && send.automation.pointCount > 0
                ? send.automation.evaluate(automationSample) : send.level;
        };
        for (size_t route = 0; route < TrackDataModel::maxSendsPerTrack; ++route)
            if (track.sends[route].targetBus.isValid() && track.sends[route].preFader)
                if (const auto bus = structure->getBusIndex(track.sends[route].targetBus); bus >= 0 && getSendLevel(route) > 0.0f)
                    for (int channel = 0; channel < juce::jmin(trackBuffer.getNumChannels(), busBuffers[static_cast<size_t>(bus)].getNumChannels()); ++channel)
                        juce::FloatVectorOperations::addWithMultiply(busBuffers[static_cast<size_t>(bus)].getWritePointer(channel),
                                                                      trackBuffer.getReadPointer(channel), getSendLevel(route), numSamples);
        const auto automatedVolume = track.volumeAutomation.mode == AutomationMode::read && track.volumeAutomation.pointCount > 0
            ? track.volumeAutomation.evaluate(automationSample) : track.volume;
        const auto automatedPan = track.panAutomation.mode == AutomationMode::read && track.panAutomation.pointCount > 0
            ? track.panAutomation.evaluate(automationSample) : track.pan;
        TrackMixing::apply(trackBuffer, numSamples,
                           { automatedVolume, automatedPan,
                             !track.muted && (!anyTrackSoloed || track.solo || track.soloSafe) });
        trackPeaks[trackIndex].store(TrackMixing::peak(trackBuffer, numSamples), std::memory_order_relaxed);

        const auto outputBusIndex = structure->getBusIndex(track.outputBus);
        auto& destination = outputBusIndex >= 0 ? busBuffers[static_cast<size_t>(outputBusIndex)] : processingBuffer;
        for (int channel = 0; channel < juce::jmin(trackBuffer.getNumChannels(), destination.getNumChannels()); ++channel)
            juce::FloatVectorOperations::add(destination.getWritePointer(channel), trackBuffer.getReadPointer(channel), numSamples);
        for (size_t route = 0; route < TrackDataModel::maxSendsPerTrack; ++route)
            if (track.sends[route].targetBus.isValid() && ! track.sends[route].preFader)
                if (const auto bus = structure->getBusIndex(track.sends[route].targetBus); bus >= 0 && getSendLevel(route) > 0.0f)
                    for (int channel = 0; channel < juce::jmin(trackBuffer.getNumChannels(), busBuffers[static_cast<size_t>(bus)].getNumChannels()); ++channel)
                        juce::FloatVectorOperations::addWithMultiply(busBuffers[static_cast<size_t>(bus)].getWritePointer(channel),
                                                                      trackBuffer.getReadPointer(channel), getSendLevel(route), numSamples);
    }

    for (size_t busIndex = 0; busIndex < structure->busCount; ++busIndex)
    {
        const auto& bus = structure->buses[busIndex];
        if (bus.fxRack != nullptr)
            for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
                if (const auto& processor = bus.fxRack->processors[slot]; processor != nullptr && ! bus.fxRack->bypass[slot])
                    processor->processBlock(busBuffers[busIndex]);
        if (bus.muted) busBuffers[busIndex].clear(0, numSamples);
        else
        {
            for (int channel = 0; channel < busBuffers[busIndex].getNumChannels(); ++channel)
                busBuffers[busIndex].applyGain(channel, 0, numSamples, bus.gain);
        }
        busPeaks[busIndex].store(TrackMixing::peak(busBuffers[busIndex], numSamples), std::memory_order_relaxed);
        for (int channel = 0; channel < juce::jmin(busBuffers[busIndex].getNumChannels(), processingBuffer.getNumChannels()); ++channel)
            juce::FloatVectorOperations::add(processingBuffer.getWritePointer(channel), busBuffers[busIndex].getReadPointer(channel), numSamples);
    }

    if (const auto* rack = publishedMasterFxRack.load(std::memory_order_acquire); rack != nullptr)
        for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
            if (const auto& processor = rack->processors[slot]; processor != nullptr && !rack->bypass[slot])
                    processor->processBlock(processingBuffer);

    graph.process(processingBuffer, numSamples);
    if (playing && metronomeEnabled.load(std::memory_order_acquire))
        renderMetronome(blockStartSample, numSamples);
    masterPeak.store(TrackMixing::peak(processingBuffer, numSamples), std::memory_order_relaxed);
    masterLeftPeak.store(peakForChannel(processingBuffer, 0, numSamples), std::memory_order_relaxed);
    masterRightPeak.store(peakForChannel(processingBuffer, 1, numSamples), std::memory_order_relaxed);

    for (int channel = 0; channel < juce::jmin(numOutputChannels, processingBuffer.getNumChannels()); ++channel)
        juce::FloatVectorOperations::copy(outputChannelData[channel], processingBuffer.getReadPointer(channel), numSamples);
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
    int targetTrack = -1;
    for (size_t index = 0; index < structure.trackCount; ++index)
        if (structure.tracks[index].type == TrackType::instrument && structure.tracks[index].armed)
        {
            targetTrack = static_cast<int>(index);
            break;
        }
    if (targetTrack < 0)
        for (size_t index = 0; index < structure.trackCount; ++index)
            if (structure.tracks[index].type == TrackType::instrument)
            {
                targetTrack = static_cast<int>(index);
                break;
            }

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    incomingMidiFifo.prepareToRead(incomingMidiCapacity, start1, size1, start2, size2);
    const auto appendRange = [this, &structure, targetTrack, blockStartSample] (int start, int count) noexcept
    {
        if (targetTrack < 0)
            return;
        for (int offset = 0; offset < count; ++offset)
        {
            const auto& event = incomingMidiEvents[static_cast<size_t>(start + offset)];
            if (! scheduledMidiEvents.add({ structure.tracks[static_cast<size_t>(targetTrack)].id, 0,
                                            event.pitch, event.velocity, event.channel, event.noteOn }))
                break;
            if (midiRecordingActive.load(std::memory_order_acquire))
            {
                midiRecordingCallbackUsers.fetch_add(1, std::memory_order_acq_rel);
                if (midiRecordingActive.load(std::memory_order_acquire))
                {
                    int writeStart1 = 0, writeSize1 = 0, writeStart2 = 0, writeSize2 = 0;
                    recordedMidiFifo.prepareToWrite(1, writeStart1, writeSize1, writeStart2, writeSize2);
                    if (writeSize1 > 0)
                    {
                        recordedMidiEvents[static_cast<size_t>(writeStart1)] = { blockStartSample, event.pitch,
                                                                                  event.velocity, event.channel, event.noteOn };
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

        juce::MidiBuffer::Iterator events(trackMidiBuffers[trackIndex]);
        juce::MidiMessage message;
        int eventSample = 0;
        auto hasEvent = events.getNextEvent(message, eventSample);
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
                hasEvent = events.getNextEvent(message, eventSample);
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
        if (sourceOffset < 0 || sourceOffset >= clip.cachedBuffer->getNumSamples())
            continue;
        const auto samplesToCopy = juce::jmin(static_cast<int>(overlapEnd - overlapStart),
                                              clip.cachedBuffer->getNumSamples() - sourceOffset);
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
                trackBuffers[static_cast<size_t>(trackIndex)].addSample(channel, segmentOffset + sample,
                    clip.cachedBuffer->getSample(sourceChannel, sourceOffset + sample) * gain);
            }
        }
    }
}
