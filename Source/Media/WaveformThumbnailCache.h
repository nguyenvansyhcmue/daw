#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <map>
#include <memory>
#include <vector>

struct WaveformPeak
{
    float minimum = 0.0f;
    float maximum = 0.0f;
};

class WaveformThumbnail final
{
public:
    explicit WaveformThumbnail(const juce::AudioBuffer<float>& source);
    explicit WaveformThumbnail(std::shared_ptr<const juce::AudioBuffer<float>> source);

    WaveformPeak peakForSourceRange(double firstSample, double lastSample) const noexcept;
    float getDisplayPeak() const noexcept { return displayPeak; }
    bool isEmpty() const noexcept;

private:
    struct ResolutionLevel
    {
        int samplesPerBucket = 1;
        std::vector<WaveformPeak> peaks;
    };

    const ResolutionLevel& levelForRange(double rangeLength) const noexcept;
    void buildPeakLevels(const juce::AudioBuffer<float>& source);

    std::vector<ResolutionLevel> resolutionLevels;
    std::shared_ptr<const juce::AudioBuffer<float>> sourceBuffer;
    int sourceSampleCount = 0;
    float displayPeak = 0.0f;
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
