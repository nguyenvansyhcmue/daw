#include "AudioEngine.h"
#include "GainUtilityProcessor.h"

#include <algorithm>

AudioEngine::AudioEngine(TrackDataModel* model)
    : dataModel(model)
{
    graph.addNode(std::make_unique<GainNode>(1.0f));
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
    tempo.store(std::max(40.0, std::min(220.0, newTempo)));
}

double AudioEngine::getTempo() const noexcept
{
    return tempo.load();
}

void AudioEngine::setPlaybackState(bool shouldPlay) noexcept
{
    playing.store(shouldPlay);
}

bool AudioEngine::isPlaying() const noexcept
{
    return playing.load();
}

juce::AudioDeviceManager& AudioEngine::getAudioDeviceManager() noexcept
{
    return deviceManager;
}

float AudioEngine::getTrackPeak(size_t trackIndex) const noexcept
{
    return trackIndex < trackPeaks.size() ? trackPeaks[trackIndex].load(std::memory_order_relaxed) : 0.0f;
}

void AudioEngine::setFxProcessor(size_t trackID, size_t slot,
                                 std::shared_ptr<AudioEffectProcessor> processor)
{
    if (dataModel == nullptr || trackID >= 8 || slot >= TrackDataModel::maxFxSlots)
        return;

    auto* device = deviceManager.getCurrentAudioDevice();
    const auto sampleRate = device != nullptr ? device->getCurrentSampleRate()
                                              : dataModel->getSampleRate();
    const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
    if (processor != nullptr)
        processor->prepareToPlay(sampleRate, blockSize, 2);
    dataModel->setFxProcessor(trackID, slot, std::move(processor));
}

void AudioEngine::setFxBypassed(size_t trackID, size_t slot, bool bypassed) noexcept
{
    if (dataModel != nullptr)
        dataModel->setFxBypassed(trackID, slot, bypassed);
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice*)
{
    auto* device = const_cast<juce::AudioIODevice*>(deviceManager.getCurrentAudioDevice());
    processingBuffer.setSize(juce::jmax(2, device != nullptr ? device->getActiveOutputChannels().countNumberOfSetBits() : 2),
                             juce::jmax(1, device != nullptr ? device->getCurrentBufferSizeSamples() : 512),
                             false, true, true);
    for (auto& buffer : trackBuffers)
        buffer.setSize(2, processingBuffer.getNumSamples(), false, true, true);
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

    if (dataModel == nullptr || !dataModel->isPlaying())
        return;

    const auto playhead = dataModel->getPlayheadPosition();
    for (auto& buffer : trackBuffers)
        buffer.clear(0, numSamples);

    const auto clips = dataModel->getAudioClipSnapshot();
    if (clips != nullptr)
    {
        for (const auto& clip : *clips)
        {
            if (clip.cachedBuffer == nullptr || clip.trackID < 0)
                continue;
            if (static_cast<size_t>(clip.trackID) >= trackBuffers.size())
                continue;

            const auto overlapStart = juce::jmax(playhead, clip.startSample);
            const auto overlapEnd = juce::jmin(playhead + numSamples,
                                               clip.startSample + clip.durationSamples);
            if (overlapEnd <= overlapStart)
                continue;

            const auto destinationOffset = static_cast<int>(overlapStart - playhead);
            const auto sourceOffset = static_cast<int>(clip.sourceOffsetSamples + overlapStart - clip.startSample);
            const auto samplesToCopy = static_cast<int>(overlapEnd - overlapStart);
            const auto& track = dataModel->getTrack(static_cast<size_t>(clip.trackID));
            if (track.muted.load(std::memory_order_relaxed))
                continue;

            const auto gain = track.volume.load(std::memory_order_relaxed);
            for (int channel = 0; channel < juce::jmin(2, numOutputChannels); ++channel)
            {
                const auto sourceChannel = juce::jmin(channel, clip.cachedBuffer->getNumChannels() - 1);
                juce::FloatVectorOperations::add(trackBuffers[static_cast<size_t>(clip.trackID)].getWritePointer(channel)
                                                     + destinationOffset,
                                                 clip.cachedBuffer->getReadPointer(sourceChannel, sourceOffset),
                                                 gain, samplesToCopy);
            }
        }
    }

    for (size_t trackID = 0; trackID < trackBuffers.size(); ++trackID)
    {
        auto& trackBuffer = trackBuffers[trackID];
        const auto rack = dataModel->getFxRackSnapshot(trackID);
        if (rack != nullptr)
        {
            for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
            {
                const auto processor = rack->processors[slot];
                if (processor != nullptr && !rack->bypass[slot].load(std::memory_order_acquire))
                    processor->processBlock(trackBuffer);
            }
        }

        for (int channel = 0; channel < juce::jmin(2, numOutputChannels); ++channel)
            juce::FloatVectorOperations::add(outputChannelData[channel],
                                             trackBuffer.getReadPointer(channel),
                                             numSamples);
    }

    dataModel->setPlayheadPosition(playhead + numSamples);

    for (size_t track = 0; track < trackPeaks.size(); ++track)
    {
        if (numOutputChannels == 0)
            break;
        const auto channel = static_cast<int>(track % static_cast<size_t>(numOutputChannels));
        const auto* samples = outputChannelData[channel];
        const auto range = juce::FloatVectorOperations::findMinAndMax(samples, numSamples);
        const auto peak = juce::jmax(std::abs(range.getStart()), std::abs(range.getEnd()));
        trackPeaks[track].store(peak, std::memory_order_relaxed);
    }

}
