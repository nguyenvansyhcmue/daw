#include "ProjectTemplates.h"

namespace
{
PersistedTrackState makeTrack(uint64_t identifier, TrackType type, juce::String name, int inputChannel = 0)
{
    PersistedTrackState track;
    track.id = { identifier };
    track.type = type;
    track.name = std::move(name);
    track.inputChannel = inputChannel;
    return track;
}

ProjectState makeBaseProject()
{
    ProjectState state;
    state.tempoMap.push_back({ 0.0, state.bpm });
    return state;
}
}

ProjectState ProjectTemplates::create(ProjectTemplate projectTemplate)
{
    auto state = makeBaseProject();

    switch (projectTemplate)
    {
        case ProjectTemplate::empty:
            break;

        case ProjectTemplate::audioRecording:
            state.tracks.push_back(makeTrack(1, TrackType::audio, "Vocal", 0));
            state.tracks.push_back(makeTrack(2, TrackType::audio, "Guitar", 2));
            state.tracks.push_back(makeTrack(3, TrackType::audio, "Audio 3", 4));
            state.tracks.push_back(makeTrack(4, TrackType::audio, "Audio 4", 6));
            break;

        case ProjectTemplate::midiProduction:
            state.tracks.push_back(makeTrack(1, TrackType::instrument, "Instrument 1"));
            state.tracks.push_back(makeTrack(2, TrackType::instrument, "Instrument 2"));
            state.tracks.push_back(makeTrack(3, TrackType::externalMidi, "External MIDI"));
            break;
    }

    return state;
}

juce::String ProjectTemplates::getName(ProjectTemplate projectTemplate)
{
    switch (projectTemplate)
    {
        case ProjectTemplate::empty:          return "Empty Project";
        case ProjectTemplate::audioRecording: return "Audio Recording";
        case ProjectTemplate::midiProduction: return "MIDI Production";
    }

    return {};
}

juce::String ProjectTemplates::getDescription(ProjectTemplate projectTemplate)
{
    switch (projectTemplate)
    {
        case ProjectTemplate::empty:          return "Create an empty project";
        case ProjectTemplate::audioRecording: return "Four audio inputs ready for recording";
        case ProjectTemplate::midiProduction: return "Two instruments and one external MIDI track";
    }

    return {};
}
