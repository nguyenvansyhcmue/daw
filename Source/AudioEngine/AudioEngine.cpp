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
    setMasterGain(1.0f);
}

void AudioEngine::shutdown()
{
    if (recordingSession != nullptr) recordingSession->stop();
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

juce::Result AudioEngine::startRecording(size_t trackIndex, const juce::File& destination)
{
    if (recordingSession == nullptr) return juce::Result::fail("Audio engine has no project model");
    if (trackIndex >= dataModel->getTrackCount()
        || dataModel->getTrack(trackIndex).type != TrackType::audio)
        return juce::Result::fail("Recording requires an audio track");
    auto* device = deviceManager.getCurrentAudioDevice();
    const auto sampleRate = device != nullptr ? device->getCurrentSampleRate() : dataModel->getSampleRate();
    const auto inputChannels = device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 2;
    const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : processingBuffer.getNumSamples();
    const auto result = recordingSession->start(trackIndex, destination, dataModel->getPlayheadPosition(), sampleRate,
                                                inputChannels, blockSize);
    if (result.wasOk()) dataModel->setTrackArmed(trackIndex, true);
    return result;
}

juce::Result AudioEngine::stopRecording()
{
    if (recordingSession == nullptr) return juce::Result::ok();
    const auto result = recordingSession->stop();
    if (dataModel != nullptr)
        for (size_t index = 0; index < dataModel->getTrackCount(); ++index) dataModel->setTrackArmed(index, false);
    return result;
}
bool AudioEngine::isRecording() const noexcept { return recordingSession != nullptr && recordingSession->isRecording(); }

juce::Result AudioEngine::renderOfflineWav(const juce::File& destination, double sampleRate,
                                           int blockSize, bool useCycle)
{
    if (dataModel == nullptr || sampleRate <= 0.0 || blockSize <= 0)
        return juce::Result::fail("Offline render configuration is invalid");
    if (isRecording()) return juce::Result::fail("Stop recording before offline render");
    double endSample = 0.0;
    for (const auto& track : dataModel->createProjectState().tracks)
        for (const auto& clip : track.clips)
            endSample = juce::jmax(endSample, clip.startSample + clip.durationSamples);
    if (useCycle && dataModel->isCycleActive()) endSample = dataModel->getCycleEndSample();
    if (endSample <= 0.0) return juce::Result::fail("Project has no audio to render");

    destination.deleteFile();
    auto stream = destination.createOutputStream();
    if (stream == nullptr) return juce::Result::fail("Cannot create offline render file");
    juce::WavAudioFormat wav;
    auto* rawStream = stream.release();
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(rawStream, sampleRate, 2, 32, {}, 0));
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
    dataModel->setSampleRate(sampleRate);
    if (! useCycle) dataModel->clearCycle();
    dataModel->setPlayheadPosition(useCycle && cycleWasActive ? cycleStart : 0.0);
    dataModel->setPlaying(true);
    processingBuffer.setSize(2, blockSize, false, true, true);
    for (auto& buffer : trackBuffers) buffer.setSize(2, blockSize, false, true, true);
    for (auto& buffer : busBuffers) buffer.setSize(2, blockSize, false, true, true);
    for (auto& midi : trackMidiBuffers) midi.ensureSize(MidiEventBuffer::capacity * 4);
    juce::AudioBuffer<float> renderBuffer(2, blockSize);
    float* outputs[] { renderBuffer.getWritePointer(0), renderBuffer.getWritePointer(1) };
    const auto totalSamples = static_cast<int64_t>(std::ceil(endSample));
    int64_t rendered = 0;
    while (rendered < totalSamples)
    {
        const auto count = static_cast<int>(juce::jmin<int64_t>(blockSize, totalSamples - rendered));
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
                                              : (dataModel != nullptr ? dataModel->getSampleRate() : 44100.0);
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
    processingBuffer.setSize(juce::jmax(2, outputChannels), juce::jmax(1, blockSize),
                             false, true, true);
    for (auto& buffer : trackBuffers)
        buffer.setSize(2, processingBuffer.getNumSamples(), false, true, true);
    for (auto& buffer : busBuffers)
        buffer.setSize(2, processingBuffer.getNumSamples(), false, true, true);
    for (auto& midi : trackMidiBuffers)
        midi.ensureSize(MidiEventBuffer::capacity * 4);

    if (dataModel != nullptr && device != nullptr && device->getCurrentSampleRate() > 0.0)
    {
        dataModel->setSampleRate(device->getCurrentSampleRate());
        for (size_t trackIndex = 0; trackIndex < trackBuffers.size(); ++trackIndex)
        {
            const auto* rack = dataModel->getFxRackSnapshot(trackIndex);
            if (rack == nullptr)
                continue;
            for (const auto& processor : rack->processors)
                if (processor != nullptr)
                    processor->prepareToPlay(device->getCurrentSampleRate(), processingBuffer.getNumSamples(), 2);
        }
    }

    if (device != nullptr && device->getCurrentSampleRate() > 0.0)
        if (const auto* rack = getMasterFxRackSnapshot(); rack != nullptr)
            for (const auto& processor : rack->processors)
                if (processor != nullptr)
                    processor->prepareToPlay(device->getCurrentSampleRate(), processingBuffer.getNumSamples(), 2);
}

void AudioEngine::audioDeviceStopped()
{
    processingBuffer.clear();
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

    for (int channel = 0; channel < numOutputChannels; ++channel)
        juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);

    if (recordingSession != nullptr) recordingSession->capture(inputChannelData, numInputChannels, numSamples);

    if (dataModel == nullptr || numSamples > processingBuffer.getNumSamples())
        return;

    const auto snapshots = dataModel->acquireRealtimeSnapshot();
    const auto* clips = snapshots.getAudioClips();
    const auto* structure = snapshots.getRenderStructure();
    const auto* midiClips = snapshots.getMidiClips();
    const auto playing = dataModel->isPlaying();
    bool hasMonitoredAudio = false;
    for (size_t index = 0; structure != nullptr && index < structure->trackCount; ++index)
        hasMonitoredAudio = hasMonitoredAudio || (structure->tracks[index].type == TrackType::audio
            && structure->tracks[index].inputMonitoring);
    if (! playing && ! hasMonitoredAudio)
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

    bool anyTrackSoloed = false;
    for (size_t trackIndex = 0; structure != nullptr && trackIndex < structure->trackCount; ++trackIndex)
        anyTrackSoloed = anyTrackSoloed || structure->tracks[trackIndex].solo;

    if (playing && structure != nullptr) renderInstrument(*structure, numSamples);

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
                if (processor != nullptr && !rack->bypass[slot])
                    processor->processBlock(trackBuffer, trackMidiBuffers[trackIndex]);
            }
        }

        const auto& track = structure->tracks[trackIndex];
        for (size_t route = 0; route < track.activeSendCount; ++route)
            if (track.sends[route].preFader)
                if (const auto bus = structure->getBusIndex(track.sends[route].targetBus); bus >= 0 && track.sends[route].level > 0.0f)
                    for (int channel = 0; channel < juce::jmin(trackBuffer.getNumChannels(), busBuffers[static_cast<size_t>(bus)].getNumChannels()); ++channel)
                        juce::FloatVectorOperations::addWithMultiply(busBuffers[static_cast<size_t>(bus)].getWritePointer(channel), trackBuffer.getReadPointer(channel), track.sends[route].level, numSamples);
        TrackMixing::apply(trackBuffer, numSamples,
                           { track.volume, track.pan,
                             !track.muted && (!anyTrackSoloed || track.solo) });
        trackPeaks[trackIndex].store(TrackMixing::peak(trackBuffer, numSamples), std::memory_order_relaxed);

        const auto outputBusIndex = structure->getBusIndex(track.outputBus);
        auto& destination = outputBusIndex >= 0 ? busBuffers[static_cast<size_t>(outputBusIndex)] : processingBuffer;
        for (int channel = 0; channel < juce::jmin(trackBuffer.getNumChannels(), destination.getNumChannels()); ++channel)
            juce::FloatVectorOperations::add(destination.getWritePointer(channel), trackBuffer.getReadPointer(channel), numSamples);
        for (size_t route = 0; route < track.activeSendCount; ++route)
            if (! track.sends[route].preFader)
                if (const auto bus = structure->getBusIndex(track.sends[route].targetBus); bus >= 0 && track.sends[route].level > 0.0f)
                for (int channel = 0; channel < juce::jmin(trackBuffer.getNumChannels(), busBuffers[static_cast<size_t>(bus)].getNumChannels()); ++channel)
                    juce::FloatVectorOperations::addWithMultiply(busBuffers[static_cast<size_t>(bus)].getWritePointer(channel), trackBuffer.getReadPointer(channel), track.sends[route].level, numSamples);
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
    masterPeak.store(TrackMixing::peak(processingBuffer, numSamples), std::memory_order_relaxed);
    masterLeftPeak.store(peakForChannel(processingBuffer, 0, numSamples), std::memory_order_relaxed);
    masterRightPeak.store(peakForChannel(processingBuffer, 1, numSamples), std::memory_order_relaxed);

    for (int channel = 0; channel < juce::jmin(numOutputChannels, processingBuffer.getNumChannels()); ++channel)
        juce::FloatVectorOperations::copy(outputChannelData[channel], processingBuffer.getReadPointer(channel), numSamples);
}

void AudioEngine::renderInstrument(const TrackDataModel::RenderStructureSnapshot& structure, int numSamples) noexcept
{
    const auto rate = dataModel != nullptr ? dataModel->getSampleRate() : 44100.0;
    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (size_t eventIndex = 0; eventIndex < scheduledMidiEvents.size(); ++eventIndex)
        {
            const auto& event = scheduledMidiEvents[eventIndex];
            if (event.sampleOffset != sample) continue;
            const auto trackIndex = structure.getTrackIndex(event.trackId);
            if (trackIndex < 0) continue;
            auto& voice = instrumentVoices[static_cast<size_t>(trackIndex)];
            if (event.noteOn) { voice.active = true; voice.pitch = event.pitch; voice.velocity = event.velocity; }
            else if (voice.pitch == event.pitch) voice.active = false;
        }
        for (size_t trackIndex = 0; trackIndex < structure.trackCount; ++trackIndex)
        {
            if (structure.tracks[trackIndex].type != TrackType::instrument)
                continue;
            auto& voice = instrumentVoices[trackIndex];
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
