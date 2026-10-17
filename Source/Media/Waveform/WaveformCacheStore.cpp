#include "WaveformCacheStore.h"

#include "../WaveformThumbnailCache.h"

namespace
{
constexpr int cacheMagic = 0x53465750; // "SFWP"
constexpr int cacheVersion = 2;

uint64_t modificationMilliseconds(const juce::File& file)
{
    return static_cast<uint64_t>(file.getLastModificationTime().toMilliseconds());
}
}

std::shared_ptr<WaveformThumbnail> WaveformCacheStore::load(const juce::File& sourceFile) const
{
    const auto cacheFile = cacheFileFor(sourceFile);
    if (! cacheFile.existsAsFile())
        return {};

    juce::FileInputStream input(cacheFile);
    if (! input.openedOk() || input.readInt() != cacheMagic || input.readInt() != cacheVersion)
        return {};
    const auto sourceSize = static_cast<uint64_t>(input.readInt64());
    const auto modified = static_cast<uint64_t>(input.readInt64());
    if (sourceSize != static_cast<uint64_t>(sourceFile.getSize())
        || modified != modificationMilliseconds(sourceFile))
        return {};
    return WaveformThumbnail::readPersistent(input);
}

void WaveformCacheStore::store(const juce::File& sourceFile, const WaveformThumbnail& thumbnail) const
{
    if (! sourceFile.existsAsFile())
        return;

    const auto destination = cacheFileFor(sourceFile);
    destination.getParentDirectory().createDirectory();
    const auto temporary = destination.getSiblingFile(destination.getFileName() + ".tmp");
    temporary.deleteFile();
    juce::FileOutputStream output(temporary);
    if (! output.openedOk())
        return;

    output.writeInt(cacheMagic);
    output.writeInt(cacheVersion);
    output.writeInt64(static_cast<juce::int64>(sourceFile.getSize()));
    output.writeInt64(static_cast<juce::int64>(modificationMilliseconds(sourceFile)));
    if (! thumbnail.writePersistent(output))
    {
        temporary.deleteFile();
        return;
    }
    output.flush();
    // Never delete a valid envelope before its replacement is complete.
    // A cancelled write or a full disk must leave the previous cache usable.
    const auto committed = destination.existsAsFile() ? temporary.replaceFileIn(destination)
                                                      : temporary.moveFileTo(destination);
    if (! committed)
        temporary.deleteFile();
}

juce::File WaveformCacheStore::cacheFileFor(const juce::File& sourceFile) const
{
    const auto directory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                               .getChildFile("StudioForge")
                               .getChildFile("WaveformCache");
    const auto key = sourceFile.getFullPathName().toLowerCase() + ":" + juce::String(sourceFile.getSize());
    return directory.getChildFile(juce::String::toHexString(key.hashCode()) + ".sfpeak");
}
