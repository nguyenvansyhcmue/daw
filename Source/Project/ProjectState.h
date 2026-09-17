#pragma once

#include <juce_core/juce_core.h>

#include "../Models/ProjectIdentifiers.h"

#include <vector>
#include <array>

enum class TrackType : uint8_t { audio, instrument, externalMidi };

struct PersistedClipState
{
    ClipId id;
    TrackId trackId;
    juce::String sourcePath;
    juce::String clipName;
    double startSample = 0.0;
    double durationSamples = 0.0;
    double sourceOffsetSamples = 0.0;
    float gain = 1.0f;
    double fadeInSamples = 0.0;
    double fadeOutSamples = 0.0;
};

struct PersistedTrackState
{
    struct SendRoute { BusId targetBus; float level = 1.0f; bool preFader = false; };
    static constexpr size_t maxSends = 8;
    TrackId id;
    TrackType type = TrackType::audio;
    juce::String name;
    float volume = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
    bool inputMonitoring = false;
    int inputChannel = 0;
    BusId outputBus;
    std::array<SendRoute, maxSends> sends {};
    uint8_t activeSendCount = 0;
    std::vector<PersistedClipState> clips;
};

struct PersistedBusState
{
    BusId id;
    float gain = 1.0f;
    bool muted = false;
};

struct PersistedTempoEvent
{
    double samplePosition = 0.0;
    double bpm = 120.0;
};

struct PersistedMidiNoteState
{
    MidiEventId id;
    int pitch = 60;
    float velocity = 1.0f;
    double startSample = 0.0;
    double durationSamples = 1.0;
    int channel = 1;
};

struct PersistedMidiClipState
{
    MidiClipId id;
    TrackId trackId;
    double startSample = 0.0;
    std::vector<PersistedMidiNoteState> notes;
};

struct ProjectState
{
    static constexpr int formatVersion = 8;

    double bpm = 120.0;
    int timeSignatureNumerator = 4;
    bool cycleActive = false;
    double cycleStartSample = 0.0;
    double cycleEndSample = 0.0;
    std::vector<PersistedTempoEvent> tempoMap;
    std::vector<PersistedBusState> buses;
    std::vector<PersistedTrackState> tracks;
    std::vector<PersistedMidiClipState> midiClips;
};
