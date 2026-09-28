#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <array>
#include <memory>
#include <vector>

#include "../Models/TrackDataModel.h"

class RecordingSession final
{
public:
    struct TrackTarget
    {
        size_t trackIndex = 0;
        juce::File destination;
        int inputChannel = 0;
        int inputChannelCount = 1;
    };

    explicit RecordingSession(TrackDataModel& model);
    ~RecordingSession();

    juce::Result start(size_t trackIndex, const juce::File& destination, double startSample,
                       double sampleRate, int inputChannel, int maximumBlockSize);
    juce::Result startMany(const std::vector<TrackTarget>& targets, double startSample,
                           double sampleRate, int maximumBlockSize);
    juce::Result stop();
    void capture(const float* const* input, int inputChannels, int sourceOffset, int numSamples) noexcept;
    bool isRecording() const noexcept;
    int getDroppedBlockCount() const noexcept;

private:
    struct TrackRecorder
    {
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
        juce::AudioBuffer<float> captureBuffer;
        juce::File destinationFile;
        size_t targetTrack = 0;
        double targetStartSample = 0.0;
        int sourceInputChannel = 0;
        int sourceInputChannelCount = 1;
        std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
        std::atomic<int> droppedBlocks { 0 };
        std::atomic<int64_t> capturedSamples { 0 };
    };

    juce::Result prepareRecorder(TrackRecorder&, const TrackTarget&, double startSample,
                                 double sampleRate, int maximumBlockSize);
    void captureRecorder(TrackRecorder&, const float* const* input, int inputChannels,
                         int sourceOffset, int numSamples) noexcept;
    juce::Result finaliseRecorder(TrackRecorder&);

    TrackDataModel& model;
    juce::TimeSliceThread writerThread { "StudioForge recording writer" };
    std::array<TrackRecorder, TrackDataModel::maxTracks> recorders;
    std::atomic<unsigned int> callbackUsers { 0 };
    std::atomic<size_t> activeRecorderCount { 0 };
};
