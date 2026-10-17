#include "WaveformThumbnailCache.h"
#include "Waveform/WaveformCacheStore.h"

#include <algorithm>
#include <cmath>

namespace
{
// Dense base level preserves detail at high timeline zoom; coarser levels
// continue to service overview drawing efficiently.
constexpr int maximumBaseBuckets = 1 << 18; // 262,144 peak columns
constexpr int directPeakQueryLimit = 256;
constexpr int thumbnailPayloadVersion = 2;

WaveformPeak mergePeaks(WaveformPeak first, WaveformPeak second) noexcept
{
    return { juce::jmin(first.minimum, second.minimum), juce::jmax(first.maximum, second.maximum),
             first.energy + second.energy, first.sampleCount + second.sampleCount };
}
}

void WaveformThumbnail::rebuildPeakLevels(int channel)
{
    auto& levels = resolutionLevels[static_cast<size_t>(channel)];
    while (levels.back().peaks.size() > 1)
    {
        const auto& finerLevel = levels.back();
        ResolutionLevel coarserLevel;
        coarserLevel.samplesPerBucket = finerLevel.samplesPerBucket * 2;
        coarserLevel.peaks.reserve((finerLevel.peaks.size() + 1) / 2);
        for (size_t index = 0; index < finerLevel.peaks.size(); index += 2)
        {
            const auto second = juce::jmin(index + 1, finerLevel.peaks.size() - 1);
            coarserLevel.peaks.push_back(mergePeaks(finerLevel.peaks[index], finerLevel.peaks[second]));
        }
        levels.push_back(std::move(coarserLevel));
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

    displayChannels = juce::jmin(2, source.getNumChannels());

    const auto bucketCount = juce::jmin(maximumBaseBuckets, sourceSampleCount);
    for (int displayChannel = 0; displayChannel < displayChannels; ++displayChannel)
    {
        ResolutionLevel baseLevel;
        baseLevel.samplesPerBucket = juce::jmax(1, (sourceSampleCount + bucketCount - 1) / bucketCount);
        baseLevel.peaks.reserve(static_cast<size_t>(bucketCount));
        const auto sourceChannel = displayChannels == 1 ? 0 : displayChannel;
        for (int bucket = 0; bucket < bucketCount; ++bucket)
        {
            const auto firstSample = bucket * baseLevel.samplesPerBucket;
            const auto samples = juce::jmin(baseLevel.samplesPerBucket, sourceSampleCount - firstSample);
            const auto range = juce::FloatVectorOperations::findMinAndMax(source.getReadPointer(sourceChannel, firstSample), samples);
            auto energy = 0.0;
            const auto* samplesPointer = source.getReadPointer(sourceChannel, firstSample);
            for (int sample = 0; sample < samples; ++sample)
                energy += static_cast<double>(samplesPointer[sample]) * samplesPointer[sample];
            baseLevel.peaks.push_back({ range.getStart(), range.getEnd(), energy, static_cast<uint32_t>(samples) });
            displayPeaks[static_cast<size_t>(displayChannel)] = juce::jmax(displayPeaks[static_cast<size_t>(displayChannel)],
                juce::jmax(std::abs(range.getStart()), std::abs(range.getEnd())));
        }
        resolutionLevels[static_cast<size_t>(displayChannel)].push_back(std::move(baseLevel));

        rebuildPeakLevels(displayChannel);
    }
}

bool WaveformThumbnail::writePersistent(juce::OutputStream& output) const
{
    if (isEmpty() || ! output.writeInt(thumbnailPayloadVersion)
        || ! output.writeInt(sourceSampleCount) || ! output.writeInt(displayChannels))
        return false;
    for (int channel = 0; channel < displayChannels; ++channel)
    {
        const auto& base = resolutionLevels[static_cast<size_t>(channel)].front();
        if (! output.writeInt(base.samplesPerBucket) || ! output.writeInt(static_cast<int>(base.peaks.size()))
            || ! output.writeFloat(displayPeaks[static_cast<size_t>(channel)]))
            return false;
        for (const auto& peak : base.peaks)
            if (! output.writeFloat(peak.minimum) || ! output.writeFloat(peak.maximum)
                || ! output.writeDouble(peak.energy) || ! output.writeInt(static_cast<int>(peak.sampleCount)))
                return false;
    }
    return true;
}

std::shared_ptr<WaveformThumbnail> WaveformThumbnail::readPersistent(juce::InputStream& input)
{
    if (input.readInt() != thumbnailPayloadVersion)
        return {};
    const auto sampleCount = input.readInt();
    const auto channels = input.readInt();
    if (sampleCount <= 0 || channels < 1 || channels > 2)
        return {};

    auto thumbnail = std::shared_ptr<WaveformThumbnail>(new WaveformThumbnail());
    thumbnail->sourceSampleCount = sampleCount;
    thumbnail->displayChannels = channels;
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto samplesPerBucket = input.readInt();
        const auto count = input.readInt();
        const auto peak = input.readFloat();
        if (samplesPerBucket <= 0 || count <= 0 || count > maximumBaseBuckets || ! std::isfinite(peak))
            return {};
        ResolutionLevel base;
        base.samplesPerBucket = samplesPerBucket;
        base.peaks.resize(static_cast<size_t>(count));
        thumbnail->displayPeaks[static_cast<size_t>(channel)] = peak;
        for (auto& entry : base.peaks)
        {
            entry.minimum = input.readFloat(); entry.maximum = input.readFloat();
            entry.energy = input.readDouble(); entry.sampleCount = static_cast<uint32_t>(input.readInt());
            if (! std::isfinite(entry.minimum) || ! std::isfinite(entry.maximum)
                || ! std::isfinite(entry.energy) || entry.energy < 0.0 || entry.sampleCount == 0)
                return {};
        }
        thumbnail->resolutionLevels[static_cast<size_t>(channel)].push_back(std::move(base));
        thumbnail->rebuildPeakLevels(channel);
    }
    return thumbnail;
}

WaveformPeak WaveformThumbnail::peakForSourceRange(double firstSample, double lastSample, int channel) const noexcept
{
    if (isEmpty())
        return {};

    // A clip can be extended past its decoded media.  That part is a valid
    // silent region, not a repetition of the thumbnail's final peak.
    if (firstSample >= static_cast<double>(sourceSampleCount) || lastSample <= 0.0)
        return {};

    const auto start = juce::jlimit(0.0, static_cast<double>(sourceSampleCount), firstSample);
    const auto end = juce::jlimit(start, static_cast<double>(sourceSampleCount), lastSample);

    // Near zoom reads the decoded samples themselves.  This preserves
    // transient detail beyond the base thumbnail resolution without a copy or
    // an allocation in the paint path.
    const auto selectedChannel = displayChannels <= 1 ? 0 : juce::jlimit(0, displayChannels - 1, channel < 0 ? 0 : channel);
    if (sourceBuffer != nullptr && end - start <= directPeakQueryLimit)
    {
        const auto first = juce::jlimit(0, sourceSampleCount - 1, static_cast<int>(std::floor(start)));
        const auto last = juce::jlimit(first + 1, sourceSampleCount, static_cast<int>(std::ceil(end)));
        const auto count = last - first;
        const auto* values = sourceBuffer->getReadPointer(selectedChannel, first);
        const auto range = juce::FloatVectorOperations::findMinAndMax(values, count);
        auto energy = 0.0;
        for (int index = 0; index < count; ++index)
            energy += static_cast<double>(values[index]) * values[index];
        return { range.getStart(), range.getEnd(), energy, static_cast<uint32_t>(count) };
    }

    const auto& level = levelForRange(end - start, selectedChannel);
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
    return displayChannels <= 0 || resolutionLevels.front().empty() || resolutionLevels.front().front().peaks.empty();
}

float WaveformThumbnail::sampleAt(double sample, int channel) const noexcept
{
    if (sourceBuffer == nullptr || sourceSampleCount <= 0)
        return 0.0f;
    const auto selected = juce::jlimit(0, sourceBuffer->getNumChannels() - 1, channel);
    const auto first = juce::jlimit(0, sourceSampleCount - 1, static_cast<int>(std::floor(sample)));
    const auto second = juce::jmin(sourceSampleCount - 1, first + 1);
    const auto fraction = static_cast<float>(sample - std::floor(sample));
    return sourceBuffer->getSample(selected, first) + (sourceBuffer->getSample(selected, second)
        - sourceBuffer->getSample(selected, first)) * fraction;
}

void WaveformThumbnail::attachSourceSamples(std::shared_ptr<const juce::AudioBuffer<float>> source) noexcept
{
    if (source != nullptr && source->getNumSamples() == sourceSampleCount)
        sourceBuffer = std::move(source);
}

float WaveformThumbnail::getDisplayPeak(int channel) const noexcept
{
    if (displayChannels <= 0)
        return 0.0f;
    if (channel < 0)
        return *std::max_element(displayPeaks.begin(), displayPeaks.begin() + displayChannels);
    return displayPeaks[static_cast<size_t>(juce::jlimit(0, displayChannels - 1, channel))];
}

const WaveformThumbnail::ResolutionLevel& WaveformThumbnail::levelForRange(double rangeLength, int channel) const noexcept
{
    const auto& levels = resolutionLevels[static_cast<size_t>(juce::jlimit(0, displayChannels - 1, channel))];
    const auto* selected = &levels.front();
    for (const auto& level : levels)
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

    static const WaveformCacheStore diskStore;
    if (const auto persisted = diskStore.load(sourceFile); persisted != nullptr)
    {
        persisted->attachSourceSamples(decodedBuffer);
        // A cache file is an optimisation, never the authority. Reject a
        // silent/corrupt envelope for non-silent PCM so it cannot make an
        // otherwise valid region appear empty in the arrangement.
        if (persisted->getDisplayPeak() > 0.000001f || decodedBuffer->getMagnitude(0, decodedBuffer->getNumSamples()) <= 0.000001f)
        {
            const juce::ScopedLock lock(cacheLock);
            auto [position, inserted] = thumbnailsBySource.emplace(key, persisted);
            return position->second;
        }
    }

    std::shared_ptr<const juce::AudioBuffer<float>> sharedSource = decodedBuffer;
    auto thumbnail = std::make_shared<const WaveformThumbnail>(std::move(sharedSource));
    diskStore.store(sourceFile, *thumbnail);
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
    const auto path = sourceFile.getFullPathName().toLowerCase();
    if (path.isEmpty() || ! sourceFile.existsAsFile())
        return path;

    // A project may keep running while an external editor replaces the file.
    // Include a cheap on-disk fingerprint so the in-memory envelope is never
    // reused for changed media just because its pathname is unchanged.
    return path + ":" + juce::String(sourceFile.getSize())
        + ":" + juce::String(sourceFile.getLastModificationTime().toMilliseconds());
}
