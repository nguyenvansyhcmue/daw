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

void addNumberedTracks(ProjectState& state, TrackType type, const juce::String& name,
                       const juce::StringArray& customNames, int count,
                       uint64_t& nextId, int& nextInput)
{
    for (int index = 1; index <= juce::jmax(0, count); ++index)
    {
        const auto customName = index <= customNames.size() ? customNames[index - 1].trim() : juce::String {};
        const auto trackName = customName.isNotEmpty() ? customName : name + " " + juce::String(index);
        state.tracks.push_back(makeTrack(nextId++, type, trackName, nextInput++));
    }
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

ProjectState ProjectTemplates::createLiveSetup(const LiveSetupConfig& setup)
{
    auto state = makeBaseProject();
    auto nextId = uint64_t { 1 };
    auto nextInput = 0;
    const auto includesAudio = setup.sessionMode == LiveSessionMode::audio
                            || setup.sessionMode == LiveSessionMode::audioAndMidi;
    const auto includesMidi = setup.sessionMode == LiveSessionMode::midi
                           || setup.sessionMode == LiveSessionMode::audioAndMidi;

    if (includesAudio)
        addNumberedTracks(state, TrackType::audio, "Vocal", setup.vocalNames, setup.vocalCount, nextId, nextInput);

    if (includesMidi)
    {
        addNumberedTracks(state, TrackType::instrument, "Guitar", setup.guitarNames, setup.guitarCount, nextId, nextInput);
        addNumberedTracks(state, TrackType::instrument, "Bass", setup.bassNames, setup.bassCount, nextId, nextInput);
        addNumberedTracks(state, TrackType::instrument, "Keys", setup.keyboardNames, setup.keyboardCount, nextId, nextInput);
        addNumberedTracks(state, TrackType::instrument, "Drums", setup.drumNames, setup.drumCount, nextId, nextInput);
    }

    if (setup.backingTrackCount > 0
        && (setup.midiMode == MidiSessionMode::backingTrack
            || setup.midiMode == MidiSessionMode::instrumentsAndBackingTrack))
        addNumberedTracks(state, TrackType::audio, "Backing Track", setup.backingTrackNames,
                          setup.backingTrackCount, nextId, nextInput);

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
