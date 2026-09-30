#include "ProjectTemplates.h"

namespace
{
PersistedTrackState makeTrack(uint64_t identifier, TrackType type, juce::String name, int inputChannel = 0,
                              int midiInputChannel = 0)
{
    PersistedTrackState track;
    track.id = { identifier };
    track.type = type;
    track.name = std::move(name);
    track.inputChannel = inputChannel;
    track.midiInputChannel = midiInputChannel;
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
                       uint64_t& nextId, int& nextInput, int midiInputChannel = 0)
{
    for (int index = 1; index <= juce::jmax(0, count); ++index)
    {
        const auto customName = index <= customNames.size() ? customNames[index - 1].trim() : juce::String {};
        const auto trackName = customName.isNotEmpty() ? customName : name + " " + juce::String(index);
        state.tracks.push_back(makeTrack(nextId++, type, trackName, nextInput, midiInputChannel));
        if (type == TrackType::audio)
            ++nextInput;
    }
}

void addPerformanceRoleTracks(ProjectState& state, const juce::String& name,
                              const juce::StringArray& names, int count, PerformanceInputSource source,
                              int midiChannel, uint64_t& nextId, int& nextInput)
{
    const auto type = source == PerformanceInputSource::audioInterface ? TrackType::audio
        : source == PerformanceInputSource::softwareInstrument ? TrackType::instrument : TrackType::externalMidi;
    addNumberedTracks(state, type, name, names, count, nextId, nextInput,
                      type == TrackType::audio ? 0 : midiChannel);
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

    const auto canUseSource = [includesAudio, includesMidi] (PerformanceInputSource source)
    {
        return source == PerformanceInputSource::audioInterface ? includesAudio : includesMidi;
    };
    if (canUseSource(setup.vocalSource))
        addPerformanceRoleTracks(state, "Vocal", setup.vocalNames, setup.vocalCount, setup.vocalSource, 1, nextId, nextInput);
    if (canUseSource(setup.guitarSource))
        addPerformanceRoleTracks(state, "Guitar", setup.guitarNames, setup.guitarCount, setup.guitarSource, 1, nextId, nextInput);
    if (canUseSource(setup.bassSource))
        addPerformanceRoleTracks(state, "Bass", setup.bassNames, setup.bassCount, setup.bassSource, 2, nextId, nextInput);
    if (canUseSource(setup.keyboardSource))
        addPerformanceRoleTracks(state, "Keys", setup.keyboardNames, setup.keyboardCount, setup.keyboardSource, 1, nextId, nextInput);
    if (canUseSource(setup.drumSource))
        addPerformanceRoleTracks(state, "Drums", setup.drumNames, setup.drumCount, setup.drumSource, 10, nextId, nextInput);

    if (setup.backingTrackCount > 0 && canUseSource(setup.backingTrackSource)
        && (setup.midiMode == MidiSessionMode::backingTrack || setup.midiMode == MidiSessionMode::instrumentsAndBackingTrack))
        addPerformanceRoleTracks(state, "Backing Track", setup.backingTrackNames, setup.backingTrackCount,
                                 setup.backingTrackSource, 1, nextId, nextInput);

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
