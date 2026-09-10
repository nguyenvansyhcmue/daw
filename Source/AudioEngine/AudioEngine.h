#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <array>

#include "AudioGraph.h"
#include "AudioEffectProcessor.h"
#include "LockFreeBuffer.h"
#include "../Models/TrackDataModel.h"

class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    explicit AudioEngine(TrackDataModel* model = nullptr);
    ~AudioEngine() override;

    void initialise();
    void shutdown();

    void setMasterGain(float gain) noexcept;
    float getMasterGain() const noexcept;

    void setTempo(double newTempo) noexcept;
    double getTempo() const noexcept;

    void setPlaybackState(bool shouldPlay) noexcept;
    bool isPlaying() const noexcept;

    juce::AudioDeviceManager& getAudioDeviceManager() noexcept;
    float getTrackPeak(size_t trackIndex) const noexcept;
    void setFxProcessor(size_t trackID, size_t slot,
                        std::shared_ptr<AudioEffectProcessor> processor);
    void setFxBypassed(size_t trackID, size_t slot, bool bypassed) noexcept;

    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;

private:
    juce::AudioDeviceManager deviceManager;
    AudioGraph graph;
    LockFreeBuffer lockFreeBuffer { 2, 8192 };
    std::atomic<float> masterGain { 1.0f };
    std::atomic<double> tempo { 120.0 };
    std::atomic<bool> playing { false };
    juce::AudioBuffer<float> processingBuffer;
    std::array<std::atomic<float>, 8> trackPeaks {};
    std::array<juce::AudioBuffer<float>, 8> trackBuffers;
    TrackDataModel* dataModel = nullptr;
};
