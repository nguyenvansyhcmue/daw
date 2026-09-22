#include "WaveformThumbnailCache.h"

#include <algorithm>
#include <cmath>

namespace
{
// Dense base level preserves detail at high timeline zoom; coarser levels
// continue to service overview drawing efficiently.
constexpr int maximumBaseBuckets = 1 << 18; // 262,144 peak columns
constexpr int directPeakQueryLimit = 256;

WaveformPeak mergePeaks(WaveformPeak first, WaveformPeak second) noexcept
{
    return { juce::jmin(first.minimum, second.minimum), juce::jmax(first.maximum, second.maximum) };
}
}

WaveformThumbnail::WaveformThumbnail(const juce::AudioBuffer<float>& source)
{
    buildPeakLevels(source);
}

WaveformThumbnail::WaveformThumbnail(std::shared_ptr<const juce::AudioBuffer<float>> source)
    : sourceBuffer(std::move(source))
{
    if (sourceBuffer != nullptr)
        buildPeakLevels(*sourceBuffer);
}

void WaveformThumbnail::buildPeakLevels(const juce::AudioBuffer<float>& source)
{
    sourceSampleCount = source.getNumSamples();
    if (sourceSampleCount <= 0 || source.getNumChannels() <= 0)
        return;

    const auto bucketCount = juce::jmin(maximumBaseBuckets, sourceSampleCount);
    ResolutionLevel baseLevel;
    baseLevel.samplesPerBucket = juce::jmax(1, (sourceSampleCount + bucketCount - 1) / bucketCount);
    baseLevel.peaks.reserve(static_cast<size_t>(bucketCount));

    for (int bucket = 0; bucket < bucketCount; ++bucket)
    {
        const auto firstSample = bucket * baseLevel.samplesPerBucket;
        const auto samples = juce::jmin(baseLevel.samplesPerBucket, sourceSampleCount - firstSample);
        auto minimum = 0.0f;
        auto maximum = 0.0f;
        for (int channel = 0; channel < source.getNumChannels(); ++channel)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax(source.getReadPointer(channel, firstSample), samples);
            minimum = juce::jmin(minimum, range.getStart());
            maximum = juce::jmax(maximum, range.getEnd());
        }
        baseLevel.peaks.push_back({ minimum, maximum });
        displayPeak = juce::jmax(displayPeak, juce::jmax(std::abs(minimum), std::abs(maximum)));
    }
    resolutionLevels.push_back(std::move(baseLevel));

    while (resolutionLevels.back().peaks.size() > 1)
    {
        const auto& finerLevel = resolutionLevels.back();
        ResolutionLevel coarserLevel;
        coarserLevel.samplesPerBucket = finerLevel.samplesPerBucket * 2;
        coarserLevel.peaks.reserve((finerLevel.peaks.size() + 1) / 2);
        for (size_t index = 0; index < finerLevel.peaks.size(); index += 2)
        {
            const auto second = juce::jmin(index + 1, finerLevel.peaks.size() - 1);
            coarserLevel.peaks.push_back(mergePeaks(finerLevel.peaks[index], finerLevel.peaks[second]));
        }
        resolutionLevels.push_back(std::move(coarserLevel));
    }
}

WaveformPeak WaveformThumbnail::peakForSourceRange(double firstSample, double lastSample) const noexcept
{
    if (isEmpty())
        return {};

    const auto start = juce::jlimit(0.0, static_cast<double>(sourceSampleCount), firstSample);
    const auto end = juce::jlimit(start, static_cast<double>(sourceSampleCount), lastSample);

    // Near zoom reads the decoded samples themselves.  This preserves
    // transient detail beyond the base thumbnail resolution without a copy or
    // an allocation in the paint path.
    if (sourceBuffer != nullptr && end - start <= directPeakQueryLimit)
    {
        const auto first = juce::jlimit(0, sourceSampleCount - 1, static_cast<int>(std::floor(start)));
        const auto last = juce::jlimit(first + 1, sourceSampleCount, static_cast<int>(std::ceil(end)));
        auto minimum = 0.0f;
        auto maximum = 0.0f;
        for (int channel = 0; channel < sourceBuffer->getNumChannels(); ++channel)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax(sourceBuffer->getReadPointer(channel, first), last - first);
            minimum = juce::jmin(minimum, range.getStart());
            maximum = juce::jmax(maximum, range.getEnd());
        }
        return { minimum, maximum };
    }

    const auto& level = levelForRange(end - start);
    const auto bucketCount = static_cast<int>(level.peaks.size());
    const auto firstBucket = juce::jlimit(0, bucketCount - 1,
                                          static_cast<int>(std::floor(start / level.samplesPerBucket)));
    const auto lastBucket = juce::jlimit(firstBucket, bucketCount - 1,
                                         static_cast<int>(std::ceil(end / level.samplesPerBucket)) - 1);

    // A display column may cross a bucket boundary.  Merge every bucket it
    // covers rather than sampling its midpoint, otherwise zoomed-out clips
    // alias into a thin, jagged line.
    auto merged = level.peaks[static_cast<size_t>(firstBucket)];
    for (auto bucket = firstBucket + 1; bucket <= lastBucket; ++bucket)
        merged = mergePeaks(merged, level.peaks[static_cast<size_t>(bucket)]);
    return merged;
}

bool WaveformThumbnail::isEmpty() const noexcept
{
    return resolutionLevels.empty() || resolutionLevels.front().peaks.empty();
}

const WaveformThumbnail::ResolutionLevel& WaveformThumbnail::levelForRange(double rangeLength) const noexcept
{
    const auto* selected = &resolutionLevels.front();
    for (const auto& level : resolutionLevels)
    {
        if (level.samplesPerBucket > rangeLength)
            break;
        selected = &level;
    }
    return *selected;
}

std::shared_ptr<const WaveformThumbnail> WaveformThumbnailCache::prepare(
    const juce::File& sourceFile, const std::shared_ptr<juce::AudioBuffer<float>>& decodedBuffer)
{
    if (decodedBuffer == nullptr || decodedBuffer->getNumSamples() <= 0)
        return {};

    const auto key = sourceKey(sourceFile);
    if (key.isEmpty())
        return {};

    {
        const juce::ScopedLock lock(cacheLock);
        if (const auto existing = thumbnailsBySource.find(key); existing != thumbnailsBySource.end())
            return existing->second;
    }

    std::shared_ptr<const juce::AudioBuffer<float>> sharedSource = decodedBuffer;
    auto thumbnail = std::make_shared<const WaveformThumbnail>(std::move(sharedSource));
    const juce::ScopedLock lock(cacheLock);
    if (const auto existing = thumbnailsBySource.find(key); existing != thumbnailsBySource.end())
        return existing->second;
    thumbnailsBySource.emplace(key, thumbnail);
    return thumbnail;
}

std::shared_ptr<const WaveformThumbnail> WaveformThumbnailCache::find(const juce::File& sourceFile) const
{
    const auto key = sourceKey(sourceFile);
    if (key.isEmpty())
        return {};
    const juce::ScopedLock lock(cacheLock);
    if (const auto found = thumbnailsBySource.find(key); found != thumbnailsBySource.end())
        return found->second;
    return {};
}

juce::String WaveformThumbnailCache::sourceKey(const juce::File& sourceFile)
{
    return sourceFile.getFullPathName().toLowerCase();
}
