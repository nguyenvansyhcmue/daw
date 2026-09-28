#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <vector>

class LiveKeyDetector final : private juce::Thread
{
public:
    struct Result
    {
        int root = 0;
        bool minor = false;
        float confidence = 0.0f;
        uint64_t revision = 0;
    };

    LiveKeyDetector();
    ~LiveKeyDetector() override;

    // Realtime-safe: only copies a mono mix to a fixed-size FIFO.
    void push(const juce::AudioBuffer<float>& source, int numSamples, double sampleRate) noexcept;
    Result getLatestResult() const noexcept;

private:
    void run() override;
    static Result analyse(const std::vector<float>& audio, double sampleRate, uint64_t revision) noexcept;

    static constexpr int fifoCapacity = 48000 * 8;
    juce::AbstractFifo fifo { fifoCapacity };
    std::array<float, fifoCapacity> fifoSamples {};
    std::atomic<double> sourceSampleRate { 0.0 };
    std::atomic<uint64_t> packedResult { 0 };
};
