#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdint>
#include <limits>

// File acceptance belongs to the media-import domain, rather than to a view.
// This keeps Finder drops, the asset browser, and future paste/import commands
// on the same supported-format contract.
namespace AudioImportSupport
{
struct AudioFileMetadata
{
    int channels = 0;
    int64_t lengthInSamples = 0;
    double sampleRate = 0.0;

    bool isValid() const noexcept
    {
        return channels > 0 && lengthInSamples > 0 && sampleRate > 0.0;
    }

    double durationSeconds() const noexcept
    {
        return isValid() ? static_cast<double>(lengthInSamples) / sampleRate : 0.0;
    }

    juce::String description() const
    {
        if (! isValid())
            return "Reading audio info…";

        const auto channelLabel = channels == 1 ? "mono" : channels == 2 ? "stereo"
                                                                     : juce::String(channels) + " ch";
        const auto minutes = static_cast<int>(durationSeconds()) / 60;
        const auto seconds = std::fmod(durationSeconds(), 60.0);
        return channelLabel + "  •  " + juce::String(minutes) + ":"
            + juce::String(seconds, 1).paddedLeft('0', 4);
    }
};

// Only opens the file header; sample decoding stays off the message thread.
inline AudioFileMetadata readMetadata(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples <= 0)
        return {};

    return { static_cast<int>(reader->numChannels), reader->lengthInSamples, reader->sampleRate };
}

inline bool isSupported(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".mp3" || extension == ".aif"
        || extension == ".aiff" || extension == ".flac";
}

inline juce::Array<juce::File> supportedFiles(const juce::StringArray& paths)
{
    juce::Array<juce::File> files;
    for (const auto& path : paths)
    {
        const juce::File file(path);
        if (isSupported(file))
            files.add(file);
    }
    return files;
}

inline juce::String summaryLabel(const juce::Array<juce::File>& files)
{
    if (files.isEmpty()) return {};
    if (files.size() == 1) return files.getFirst().getFileNameWithoutExtension();
    return files.getFirst().getFileNameWithoutExtension() + " + "
        + juce::String(files.size() - 1) + " more";
}
}
