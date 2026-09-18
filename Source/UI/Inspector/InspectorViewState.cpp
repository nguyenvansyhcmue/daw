#include "InspectorViewState.h"

#include <algorithm>

namespace
{
juce::String outputName(const TrackDataModel& model, BusId output)
{
    if (! output.isValid())
        return "Output: Stereo Out";

    const auto& buses = model.getBuses();
    for (size_t index = 0; index < buses.size(); ++index)
        if (buses[index].id == output)
            return "Output: Aux Bus " + juce::String(static_cast<int>(index + 1));

    return "Output: Stereo Out";
}

juce::String trackInputName(const TrackDataModel::TrackState& track)
{
    switch (track.type)
    {
        case TrackType::audio:        return "Input " + juce::String(track.inputChannel.load() + 1) + " (Audio)";
        case TrackType::instrument:   return "Software Instrument";
        case TrackType::externalMidi: return "External MIDI";
    }

    return "Input: --";
}
}

InspectorRegionViewState InspectorViewStateBuilder::makeEmptyRegion()
{
    return { InspectorRegionKind::none,
             "No region selected",
             "Select an audio or MIDI region to inspect it",
             {} };
}

InspectorRegionViewState InspectorViewStateBuilder::makeAudioRegion(const TrackDataModel& model, ClipId clipId)
{
    for (size_t trackIndex = 0; trackIndex < model.getTrackCount(); ++trackIndex)
        for (const auto& clip : model.getTrack(trackIndex).clips)
            if (clip.id == clipId)
            {
                const auto sampleRate = model.getSampleRate();
                return { InspectorRegionKind::audio,
                         clip.clipName.isNotEmpty() ? clip.clipName : clip.sourceFile.getFileName(),
                         "Start " + juce::String(clip.startSample / sampleRate, 2) + " s  |  Length "
                             + juce::String(clip.durationSamples / sampleRate, 2) + " s",
                         "Gain " + juce::String(juce::Decibels::gainToDecibels(clip.gain), 1) + " dB  |  Fades "
                             + juce::String(clip.fadeInSamples / sampleRate, 2) + " / "
                             + juce::String(clip.fadeOutSamples / sampleRate, 2) + " s" };
            }

    return makeEmptyRegion();
}

InspectorRegionViewState InspectorViewStateBuilder::makeMidiRegion(const TrackDataModel& model, MidiClipId clipId)
{
    const auto& clips = model.getMidiClips();
    const auto found = std::find_if(clips.begin(), clips.end(), [clipId] (const MidiClipState& clip) { return clip.id == clipId; });
    if (found == clips.end())
        return makeEmptyRegion();

    return { InspectorRegionKind::midi,
             "MIDI Region",
             "Start " + juce::String(found->startSample / model.getSampleRate(), 2) + " s  |  Notes "
                 + juce::String(static_cast<int>(found->notes.size())),
             "Open Piano Roll to edit notes" };
}

InspectorTrackViewState InspectorViewStateBuilder::makeTrack(const TrackDataModel& model, int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(model.getTrackCount()))
        return { false, false, "No track selected", "Input: --", "Output: Stereo Out" };

    const auto& track = model.getTrack(static_cast<size_t>(trackIndex));
    return { true,
             track.type == TrackType::audio,
             track.name,
             trackInputName(track),
             outputName(model, track.outputBus) };
}
