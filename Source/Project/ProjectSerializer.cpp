#include "ProjectSerializer.h"

#include "ProjectState.h"
#include "../Models/TrackDataModel.h"
#include "../Media/MediaReloadService.h"

#include <charconv>
#include <cmath>
#include <map>
#include <system_error>

namespace
{
constexpr auto rootTag = "StudioForgeProject";

juce::File getProjectMediaDirectory(const juce::File& projectFile)
{
    return projectFile.getParentDirectory().getChildFile(projectFile.getFileNameWithoutExtension() + " Media");
}

juce::Result makeMediaPortable(ProjectState& state, const juce::File& projectFile)
{
    const auto mediaDirectory = getProjectMediaDirectory(projectFile);
    std::map<juce::String, juce::String> portablePaths;
    for (auto& track : state.tracks)
        for (auto& clip : track.clips)
        {
            if (clip.sourcePath.isEmpty())
                continue;

            const auto source = juce::File(clip.sourcePath);
            if (! source.existsAsFile())
                continue; // Preserve a missing reference so it can be relinked later.

            const auto sourcePath = source.getFullPathName();
            if (const auto existing = portablePaths.find(sourcePath); existing != portablePaths.end())
            {
                clip.sourcePath = existing->second;
                continue;
            }

            const auto destination = mediaDirectory.getChildFile("clip-" + juce::String(clip.id.value)
                                                                  + source.getFileExtension());
            if (source != destination)
            {
                if (! mediaDirectory.createDirectory() || ! source.copyFileTo(destination))
                    return juce::Result::fail("Cannot copy project media: " + source.getFullPathName());
            }

            clip.sourcePath = mediaDirectory.getFileName() + "/" + destination.getFileName();
            portablePaths.emplace(sourcePath, clip.sourcePath);
        }
    return juce::Result::ok();
}

bool readUnsigned(const juce::XmlElement& element, const char* name, uint64_t& value)
{
    const auto text = element.getStringAttribute(name).trim();
    if (text.isEmpty()) return false;
    const auto source = text.toStdString();
    const auto [end, error] = std::from_chars(source.data(), source.data() + source.size(), value);
    return error == std::errc {} && end == source.data() + source.size();
}

bool readInt(const juce::XmlElement& element, const char* name, int& value)
{
    const auto text = element.getStringAttribute(name).trim();
    if (text.isEmpty()) return false;
    const auto source = text.toStdString();
    const auto [end, error] = std::from_chars(source.data(), source.data() + source.size(), value);
    return error == std::errc {} && end == source.data() + source.size();
}

bool readDouble(const juce::XmlElement& element, const char* name, double& value)
{
    const auto text = element.getStringAttribute(name).trim();
    if (text.isEmpty()) return false;
    const auto source = text.toStdString();
    const auto [end, error] = std::from_chars(source.data(), source.data() + source.size(), value);
    return error == std::errc {} && end == source.data() + source.size() && std::isfinite(value);
}

bool readBool(const juce::XmlElement& element, const char* name, bool& value)
{
    const auto text = element.getStringAttribute(name);
    if (text == "0") { value = false; return true; }
    if (text == "1") { value = true; return true; }
    return false;
}

juce::Result parseProject(const juce::File& file, ProjectState& state)
{
    juce::XmlDocument document(file);
    const auto root = document.getDocumentElement();
    if (root == nullptr)
        return juce::Result::fail("Cannot parse project: " + document.getLastParseError());
    if (! root->hasTagName(rootTag)) return juce::Result::fail("Not a StudioForge project file");

    int version = 0;
    if (! readInt(*root, "formatVersion", version)) return juce::Result::fail("Project format version is missing or invalid");
    if (version < 1 || version > ProjectState::formatVersion)
        return juce::Result::fail("Unsupported project format version: " + juce::String(version));
    if (! readDouble(*root, "bpm", state.bpm) || ! readInt(*root, "timeSignatureNumerator", state.timeSignatureNumerator)
        || ! readBool(*root, "cycleActive", state.cycleActive) || ! readDouble(*root, "cycleStartSample", state.cycleStartSample)
        || ! readDouble(*root, "cycleEndSample", state.cycleEndSample))
        return juce::Result::fail("Project transport state is malformed");

    const auto* tempoMap = root->getChildByName("TempoMap");
    const auto* tracks = root->getChildByName("Tracks");
    if (tempoMap == nullptr || tracks == nullptr) return juce::Result::fail("Project is missing TempoMap or Tracks");
    for (const auto* event : tempoMap->getChildIterator())
    {
        if (! event->hasTagName("Tempo")) return juce::Result::fail("Tempo map has an unknown element");
        PersistedTempoEvent parsed;
        if (! readDouble(*event, "samplePosition", parsed.samplePosition) || ! readDouble(*event, "bpm", parsed.bpm))
            return juce::Result::fail("Tempo event is malformed");
        state.tempoMap.push_back(parsed);
    }
    for (const auto* track : tracks->getChildIterator())
    {
        if (! track->hasTagName("Track")) return juce::Result::fail("Tracks has an unknown element");
        PersistedTrackState parsedTrack;
        uint64_t trackId = 0;
        double volume = 0.0, pan = 0.0;
        if (! readUnsigned(*track, "id", trackId) || ! readDouble(*track, "volume", volume)
            || ! readDouble(*track, "pan", pan) || ! readBool(*track, "muted", parsedTrack.muted)
            || ! readBool(*track, "solo", parsedTrack.solo))
            return juce::Result::fail("Track state is malformed");
        parsedTrack.id = { trackId };
        if (version >= 5)
        {
            int type = 0;
            if (! readInt(*track, "type", type) || type < static_cast<int>(TrackType::audio)
                || type > static_cast<int>(TrackType::externalMidi))
                return juce::Result::fail("Track type is malformed");
            parsedTrack.type = static_cast<TrackType>(type);
        }
        if (version >= 6)
        {
            if (! readBool(*track, "inputMonitoring", parsedTrack.inputMonitoring)
                || ! readInt(*track, "inputChannel", parsedTrack.inputChannel))
                return juce::Result::fail("Track input routing is malformed");
        }
        if (version >= 7)
        {
            uint64_t outputBus = 0, sendBus = 0;
            double sendAmount = 0.0;
            if (! readUnsigned(*track, "outputBus", outputBus) || ! readUnsigned(*track, "sendBus", sendBus)
                || ! readDouble(*track, "sendAmount", sendAmount))
                return juce::Result::fail("Track bus routing is malformed");
            parsedTrack.outputBus = { outputBus };
            parsedTrack.sendBus = { sendBus };
            parsedTrack.sendAmount = static_cast<float>(sendAmount);
        }
        if (version >= 4)
        {
            if (! track->hasAttribute("name")) return juce::Result::fail("Track name is missing");
            parsedTrack.name = track->getStringAttribute("name").trim().substring(0, 64);
            if (parsedTrack.name.isEmpty()) return juce::Result::fail("Track name is invalid");
        }
        parsedTrack.volume = static_cast<float>(volume);
        parsedTrack.pan = static_cast<float>(pan);
        const auto* clips = track->getChildByName("Clips");
        if (clips == nullptr) return juce::Result::fail("Track is missing Clips");
        for (const auto* clip : clips->getChildIterator())
        {
            if (! clip->hasTagName("Clip")) return juce::Result::fail("Clips has an unknown element");
            PersistedClipState parsedClip;
            uint64_t clipId = 0, clipTrackId = 0;
            if (! readUnsigned(*clip, "id", clipId) || ! readUnsigned(*clip, "trackId", clipTrackId)
                || ! readDouble(*clip, "startSample", parsedClip.startSample)
                || ! readDouble(*clip, "durationSamples", parsedClip.durationSamples)
                || ! readDouble(*clip, "sourceOffsetSamples", parsedClip.sourceOffsetSamples)
                || ! clip->hasAttribute("sourcePath") || ! clip->hasAttribute("clipName"))
                return juce::Result::fail("Clip state is malformed");
            if (version >= 2)
            {
                double gain = 1.0;
                if (! readDouble(*clip, "gain", gain) || ! readDouble(*clip, "fadeInSamples", parsedClip.fadeInSamples)
                    || ! readDouble(*clip, "fadeOutSamples", parsedClip.fadeOutSamples))
                    return juce::Result::fail("Clip editing state is malformed");
                parsedClip.gain = static_cast<float>(gain);
            }
            parsedClip.id = { clipId };
            parsedClip.trackId = { clipTrackId };
            parsedClip.sourcePath = clip->getStringAttribute("sourcePath");
            if (parsedClip.sourcePath.isNotEmpty() && ! juce::File::isAbsolutePath(parsedClip.sourcePath))
                parsedClip.sourcePath = file.getParentDirectory().getChildFile(parsedClip.sourcePath).getFullPathName();
            parsedClip.clipName = clip->getStringAttribute("clipName");
            parsedTrack.clips.push_back(std::move(parsedClip));
        }
        state.tracks.push_back(std::move(parsedTrack));
    }

    if (version >= 7)
    {
        const auto* buses = root->getChildByName("Buses");
        if (buses == nullptr) return juce::Result::fail("Project is missing Buses");
        for (const auto* bus : buses->getChildIterator())
        {
            if (! bus->hasTagName("Bus")) return juce::Result::fail("Buses has an unknown element");
            PersistedBusState parsedBus;
            uint64_t busId = 0;
            double gain = 0.0;
            if (! readUnsigned(*bus, "id", busId) || ! readDouble(*bus, "gain", gain)
                || ! readBool(*bus, "muted", parsedBus.muted))
                return juce::Result::fail("Bus state is malformed");
            parsedBus.id = { busId };
            parsedBus.gain = static_cast<float>(gain);
            state.buses.push_back(parsedBus);
        }
    }

    if (version >= 3)
    {
        const auto* midiClips = root->getChildByName("MidiClips");
        if (midiClips == nullptr) return juce::Result::fail("Project is missing MIDI clips");
        for (const auto* clip : midiClips->getChildIterator())
        {
            if (! clip->hasTagName("MidiClip")) return juce::Result::fail("MIDI clips has an unknown element");
            PersistedMidiClipState parsedClip;
            uint64_t clipId = 0, trackId = 0;
            if (! readUnsigned(*clip, "id", clipId) || ! readUnsigned(*clip, "trackId", trackId)
                || ! readDouble(*clip, "startSample", parsedClip.startSample))
                return juce::Result::fail("MIDI clip state is malformed");
            parsedClip.id = { clipId };
            parsedClip.trackId = { trackId };
            for (const auto* note : clip->getChildIterator())
            {
                if (! note->hasTagName("Note")) return juce::Result::fail("MIDI clip has an unknown element");
                PersistedMidiNoteState parsedNote;
                uint64_t noteId = 0;
                double velocity = 0.0;
                if (! readUnsigned(*note, "id", noteId) || ! readInt(*note, "pitch", parsedNote.pitch)
                    || ! readDouble(*note, "velocity", velocity) || ! readDouble(*note, "startSample", parsedNote.startSample)
                    || ! readDouble(*note, "durationSamples", parsedNote.durationSamples)
                    || ! readInt(*note, "channel", parsedNote.channel))
                    return juce::Result::fail("MIDI note state is malformed");
                parsedNote.id = { noteId };
                parsedNote.velocity = static_cast<float>(velocity);
                parsedClip.notes.push_back(std::move(parsedNote));
            }
            state.midiClips.push_back(std::move(parsedClip));
        }
    }
    return juce::Result::ok();
}
}

juce::Result ProjectSerializer::save(const TrackDataModel& model, const juce::File& file)
{
    auto state = model.createProjectState();
    if (const auto result = makeMediaPortable(state, file); result.failed()) return result;
    juce::XmlElement root(rootTag);
    root.setAttribute("formatVersion", ProjectState::formatVersion);
    root.setAttribute("bpm", state.bpm);
    root.setAttribute("timeSignatureNumerator", state.timeSignatureNumerator);
    root.setAttribute("cycleActive", state.cycleActive ? 1 : 0);
    root.setAttribute("cycleStartSample", state.cycleStartSample);
    root.setAttribute("cycleEndSample", state.cycleEndSample);

    auto* tempoMap = root.createNewChildElement("TempoMap");
    for (const auto& event : state.tempoMap)
    {
        auto* element = tempoMap->createNewChildElement("Tempo");
        element->setAttribute("samplePosition", event.samplePosition);
        element->setAttribute("bpm", event.bpm);
    }
    auto* tracks = root.createNewChildElement("Tracks");
    for (const auto& track : state.tracks)
    {
        auto* element = tracks->createNewChildElement("Track");
        element->setAttribute("id", juce::String(track.id.value));
        element->setAttribute("type", static_cast<int>(track.type));
        element->setAttribute("name", track.name);
        element->setAttribute("volume", static_cast<double>(track.volume));
        element->setAttribute("pan", static_cast<double>(track.pan));
        element->setAttribute("muted", track.muted ? 1 : 0);
        element->setAttribute("solo", track.solo ? 1 : 0);
        element->setAttribute("inputMonitoring", track.inputMonitoring ? 1 : 0);
        element->setAttribute("inputChannel", track.inputChannel);
        element->setAttribute("outputBus", juce::String(track.outputBus.value));
        element->setAttribute("sendBus", juce::String(track.sendBus.value));
        element->setAttribute("sendAmount", static_cast<double>(track.sendAmount));
        auto* clips = element->createNewChildElement("Clips");
        for (const auto& clip : track.clips)
        {
            auto* savedClip = clips->createNewChildElement("Clip");
            savedClip->setAttribute("id", juce::String(clip.id.value));
            savedClip->setAttribute("trackId", juce::String(clip.trackId.value));
            savedClip->setAttribute("sourcePath", clip.sourcePath);
            savedClip->setAttribute("clipName", clip.clipName);
            savedClip->setAttribute("startSample", clip.startSample);
            savedClip->setAttribute("durationSamples", clip.durationSamples);
            savedClip->setAttribute("sourceOffsetSamples", clip.sourceOffsetSamples);
            savedClip->setAttribute("gain", static_cast<double>(clip.gain));
            savedClip->setAttribute("fadeInSamples", clip.fadeInSamples);
            savedClip->setAttribute("fadeOutSamples", clip.fadeOutSamples);
        }
    }
    auto* buses = root.createNewChildElement("Buses");
    for (const auto& bus : state.buses)
    {
        auto* element = buses->createNewChildElement("Bus");
        element->setAttribute("id", juce::String(bus.id.value));
        element->setAttribute("gain", static_cast<double>(bus.gain));
        element->setAttribute("muted", bus.muted ? 1 : 0);
    }
    auto* midiClips = root.createNewChildElement("MidiClips");
    for (const auto& clip : state.midiClips)
    {
        auto* savedClip = midiClips->createNewChildElement("MidiClip");
        savedClip->setAttribute("id", juce::String(clip.id.value));
        savedClip->setAttribute("trackId", juce::String(clip.trackId.value));
        savedClip->setAttribute("startSample", clip.startSample);
        for (const auto& note : clip.notes)
        {
            auto* savedNote = savedClip->createNewChildElement("Note");
            savedNote->setAttribute("id", juce::String(note.id.value));
            savedNote->setAttribute("pitch", note.pitch);
            savedNote->setAttribute("velocity", static_cast<double>(note.velocity));
            savedNote->setAttribute("startSample", note.startSample);
            savedNote->setAttribute("durationSamples", note.durationSamples);
            savedNote->setAttribute("channel", note.channel);
        }
    }
    if (! root.writeTo(file)) return juce::Result::fail("Cannot write project file: " + file.getFullPathName());
    return juce::Result::ok();
}

juce::Result ProjectSerializer::load(TrackDataModel& model, const juce::File& file,
                                     juce::StringArray* missingMediaReferences)
{
    ProjectState candidate;
    if (const auto result = parseProject(file, candidate); result.failed()) return result;
    if (const auto result = model.applyProjectState(candidate); result.failed()) return result;
    const auto mediaReport = MediaReloadService::reloadProjectMedia(model);
    if (missingMediaReferences != nullptr) *missingMediaReferences = mediaReport.missingMediaReferences;
    return juce::Result::ok();
}
