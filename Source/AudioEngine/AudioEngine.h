#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <array>

#include "AudioGraph.h"
#include "AudioEffectProcessor.h"
#include "TrackMixing.h"
#include "TransportUtils.h"
#include "../Recording/RecordingSession.h"
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
    float getMasterPeak() const noexcept;
    void setFxProcessor(size_t trackIndex, size_t slot,
                        std::shared_ptr<AudioEffectProcessor> processor);
    void setFxBypassed(size_t trackIndex, size_t slot, bool bypassed) noexcept;
    juce::Result startRecording(size_t trackIndex, const juce::File& destination);
    juce::Result stopRecording();
    bool isRecording() const noexcept;
    size_t getScheduledMidiEventCount() const noexcept { return scheduledMidiEvents.size(); }
    juce::Result renderOfflineWav(const juce::File& destination, double sampleRate,
                                  int blockSize = 512, bool useCycle = false);

    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;

private:
    void renderClipSegment(const std::vector<AudioClipState>& clips,
                           const TrackDataModel::RenderStructureSnapshot& structure,
                           double segmentStartSample, int destinationOffset,
                           int numSamples) noexcept;
    void renderInstrument(const TrackDataModel::RenderStructureSnapshot& structure, int numSamples) noexcept;

    juce::AudioDeviceManager deviceManager;
    AudioGraph graph;
    std::atomic<float> masterGain { 1.0f };
    juce::AudioBuffer<float> processingBuffer;
    std::array<std::atomic<float>, TrackDataModel::maxTracks> trackPeaks {};
    std::atomic<float> masterPeak { 0.0f };
    std::array<juce::AudioBuffer<float>, TrackDataModel::maxTracks> trackBuffers;
    std::array<juce::AudioBuffer<float>, TrackDataModel::maxBuses> busBuffers;
    std::array<juce::MidiBuffer, TrackDataModel::maxTracks> trackMidiBuffers;
    MidiEventBuffer scheduledMidiEvents;
    struct InstrumentVoice { bool active = false; int pitch = 60; float velocity = 0.0f; double phase = 0.0; };
    std::array<InstrumentVoice, TrackDataModel::maxTracks> instrumentVoices {};
    std::unique_ptr<RecordingSession> recordingSession;
    TrackDataModel* dataModel = nullptr;
};
