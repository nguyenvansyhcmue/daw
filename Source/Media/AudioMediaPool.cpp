#include "AudioMediaPool.h"

const AudioMediaPool::Source& AudioMediaPool::registerDecodedSource(
    const juce::File& sourceFile, std::shared_ptr<juce::AudioBuffer<float>> playbackData)
{
    const auto key = canonicalPath(sourceFile);
    if (key.isEmpty())
    {
        unnamedSources.push_back({ { nextSourceId++ }, sourceFile, std::move(playbackData) });
        return unnamedSources.back();
    }

    auto [position, inserted] = sourcesByPath.try_emplace(key);
    auto& source = position->second;
    if (inserted)
    {
        source.id = { nextSourceId++ };
        source.sourceFile = sourceFile;
    }
    if (source.playbackData == nullptr)
        source.playbackData = std::move(playbackData);
    return source;
}

const AudioMediaPool::Source* AudioMediaPool::find(const juce::File& sourceFile) const
{
    const auto key = canonicalPath(sourceFile);
    if (key.isEmpty())
        return nullptr;
    if (const auto found = sourcesByPath.find(key); found != sourcesByPath.end())
        return &found->second;
    return nullptr;
}

std::shared_ptr<juce::AudioBuffer<float>> AudioMediaPool::findPlaybackData(const juce::File& sourceFile) const
{
    if (const auto* source = find(sourceFile); source != nullptr)
        return source->playbackData;
    return {};
}

void AudioMediaPool::clear() noexcept
{
    sourcesByPath.clear();
    unnamedSources.clear();
    nextSourceId = 1;
}

juce::String AudioMediaPool::canonicalPath(const juce::File& sourceFile)
{
    return sourceFile.getFullPathName().toLowerCase();
}
