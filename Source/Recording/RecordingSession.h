#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>

class TrackDataModel;

class RecordingSession final
{
public:
    explicit RecordingSession(TrackDataModel& model);
    ~RecordingSession();

    juce::Result start(size_t trackIndex, const juce::File& destination, double startSample,
                       double sampleRate, int inputChannel, int maximumBlockSize);
    juce::Result stop();
    void capture(const float* const* input, int inputChannels, int sourceOffset, int numSamples) noexcept;
    bool isRecording() const noexcept;
    int getDroppedBlockCount() const noexcept;

private:
    TrackDataModel& model;
    juce::TimeSliceThread writerThread { "StudioForge recording writer" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::AudioBuffer<float> captureBuffer;
    juce::File destinationFile;
    size_t targetTrack = 0;
    double targetStartSample = 0.0;
    int sourceInputChannel = 0;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
    std::atomic<unsigned int> callbackUsers { 0 };
    std::atomic<int> droppedBlocks { 0 };
    std::atomic<int64_t> capturedSamples { 0 };
};
