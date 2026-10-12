#pragma once

#include <juce_core/juce_core.h>
#include <memory>

class WaveformThumbnail;

// Disk cache for visual peak envelopes only. It intentionally never stores
// decoded PCM, so project media ownership remains in AudioMediaPool.
class WaveformCacheStore final
{
public:
    std::shared_ptr<WaveformThumbnail> load(const juce::File& sourceFile) const;
    void store(const juce::File& sourceFile, const WaveformThumbnail&) const;

private:
    juce::File cacheFileFor(const juce::File& sourceFile) const;
};
