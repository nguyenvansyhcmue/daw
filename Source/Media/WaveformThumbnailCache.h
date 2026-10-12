#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <map>
#include <memory>
#include <array>
#include <cmath>
#include <vector>

struct WaveformPeak
{
    float minimum = 0.0f;
    float maximum = 0.0f;
    double energy = 0.0;
    uint32_t sampleCount = 0;

    float rms() const noexcept
    {
        return sampleCount > 0 ? static_cast<float>(std::sqrt(energy / sampleCount)) : 0.0f;
    }
};

class WaveformThumbnail final
{
public:
    explicit WaveformThumbnail(const juce::AudioBuffer<float>& source);
    explicit WaveformThumbnail(std::shared_ptr<const juce::AudioBuffer<float>> source);

    bool writePersistent(juce::OutputStream&) const;
    static std::shared_ptr<WaveformThumbnail> readPersistent(juce::InputStream&);

    WaveformPeak peakForSourceRange(double firstSample, double lastSample, int channel = -1) const noexcept;
    bool canReadSamples() const noexcept { return sourceBuffer != nullptr; }
    float sampleAt(double sample, int channel = 0) const noexcept;
    void attachSourceSamples(std::shared_ptr<const juce::AudioBuffer<float>> source) noexcept;
    float getDisplayPeak(int channel = -1) const noexcept;
    int getDisplayChannelCount() const noexcept { return displayChannels; }
    bool isEmpty() const noexcept;

private:
    WaveformThumbnail() = default;
    struct ResolutionLevel
    {
        int samplesPerBucket = 1;
        std::vector<WaveformPeak> peaks;
    };

    const ResolutionLevel& levelForRange(double rangeLength, int channel) const noexcept;
    void buildPeakLevels(const juce::AudioBuffer<float>& source);
    void rebuildPeakLevels(int channel);

    std::array<std::vector<ResolutionLevel>, 2> resolutionLevels;
    std::shared_ptr<const juce::AudioBuffer<float>> sourceBuffer;
    int sourceSampleCount = 0;
    int displayChannels = 0;
    std::array<float, 2> displayPeaks {};
};

class WaveformThumbnailCache final
{
public:
    std::shared_ptr<const WaveformThumbnail> prepare(const juce::File& sourceFile,
                                                      const std::shared_ptr<juce::AudioBuffer<float>>& decodedBuffer);
    std::shared_ptr<const WaveformThumbnail> find(const juce::File& sourceFile) const;

private:
    static juce::String sourceKey(const juce::File& sourceFile);

    mutable juce::CriticalSection cacheLock;
    std::map<juce::String, std::shared_ptr<const WaveformThumbnail>> thumbnailsBySource;
};
