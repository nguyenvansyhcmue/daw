#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "../Models/ProjectIdentifiers.h"

#include <map>
#include <memory>
#include <vector>

class AudioMediaPool final
{
public:
    struct Source
    {
        AudioSourceId id;
        juce::File sourceFile;
        // The model and its immutable snapshots own decoded audio while it is in use.
        // Keeping only a weak cache entry prevents removed media from being retained by
        // the pool for the lifetime of a large project.
        std::weak_ptr<juce::AudioBuffer<float>> playbackData;
    };

    const Source& registerDecodedSource(const juce::File& sourceFile,
                                        std::shared_ptr<juce::AudioBuffer<float>> playbackData);
    const Source* find(const juce::File& sourceFile) const;
    std::shared_ptr<juce::AudioBuffer<float>> findPlaybackData(const juce::File& sourceFile) const;
    void clear() noexcept;
    size_t size() const noexcept { return sourcesByPath.size() + unnamedSources.size(); }

    static juce::String canonicalPath(const juce::File& sourceFile);

private:
    std::map<juce::String, Source> sourcesByPath;
    std::vector<Source> unnamedSources;
    uint64_t nextSourceId = 1;
};
