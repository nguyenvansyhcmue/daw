#include "MediaReloadService.h"

#include "AudioMediaPool.h"
#include "../Models/TrackDataModel.h"

#include <limits>
#include <map>

juce::Result MediaReloadService::decode(const juce::File& file,
                                        std::shared_ptr<juce::AudioBuffer<float>>& decodedBuffer)
{
    decodedBuffer.reset();
    if (! file.existsAsFile()) return juce::Result::fail("Media file is missing: " + file.getFullPathName());

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) return juce::Result::fail("Unsupported or unreadable media: " + file.getFullPathName());
    if (reader->numChannels == 0 || reader->lengthInSamples <= 0
        || reader->lengthInSamples > std::numeric_limits<int>::max())
        return juce::Result::fail("Media has no supported audio samples: " + file.getFullPathName());

    auto buffer = std::make_shared<juce::AudioBuffer<float>>(static_cast<int>(reader->numChannels),
                                                              static_cast<int>(reader->lengthInSamples));
    if (! reader->read(buffer.get(), 0, buffer->getNumSamples(), 0, true, true))
        return juce::Result::fail("Cannot decode media samples: " + file.getFullPathName());
    decodedBuffer = std::move(buffer);
    return juce::Result::ok();
}

MediaReloadReport MediaReloadService::reloadProjectMedia(TrackDataModel& model)
{
    MediaReloadReport report;
    std::map<juce::String, std::shared_ptr<juce::AudioBuffer<float>>> decodedByPath;
    const auto project = model.createProjectState();
    for (const auto& track : project.tracks)
        for (const auto& clip : track.clips)
        {
            const auto file = juce::File(clip.sourcePath);
            if (! file.existsAsFile())
            {
                model.setClipMediaStatus(clip.id, AudioMediaStatus::Missing);
                if (clip.sourcePath.isNotEmpty()) report.missingMediaReferences.addIfNotAlreadyThere(clip.sourcePath);
                continue;
            }
            const auto path = AudioMediaPool::canonicalPath(file);
            auto decoded = decodedByPath.find(path);
            if (decoded == decodedByPath.end())
            {
                std::shared_ptr<juce::AudioBuffer<float>> buffer;
                if (const auto result = decode(file, buffer); result.failed())
                {
                    model.setClipMediaStatus(clip.id, AudioMediaStatus::DecodeFailed);
                    report.decodeFailures.addIfNotAlreadyThere(result.getErrorMessage());
                    continue;
                }
                decoded = decodedByPath.emplace(path, std::move(buffer)).first;
            }
            if (model.setClipMediaResource(clip.id, file, decoded->second)) ++report.decodedClipCount;
        }
    return report;
}

juce::Result MediaReloadService::relinkClip(TrackDataModel& model, ClipId clipId,
                                             const juce::File& replacementFile)
{
    std::shared_ptr<juce::AudioBuffer<float>> decodedBuffer;
    if (const auto result = decode(replacementFile, decodedBuffer); result.failed()) return result;
    if (! model.setClipMediaResource(clipId, replacementFile, std::move(decodedBuffer)))
        return juce::Result::fail("Cannot relink an unknown clip");
    return juce::Result::ok();
}
