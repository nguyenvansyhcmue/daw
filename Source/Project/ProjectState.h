#pragma once

#include <juce_core/juce_core.h>

#include "../Models/ProjectIdentifiers.h"
#include "../Models/TakeFolderState.h"
#include "../Control/ControlRoutingProfile.h"

#include <vector>
#include <array>

enum class TrackType : uint8_t { audio, instrument, externalMidi };
// Persisted intent only. Runtime permission deliberately belongs to the live
// model and is never restored from a project file.
enum class MonitorDestination : uint8_t { off, cue, streamOnly, master };

struct PersistedClipState
{
    ClipId id;
    TrackId trackId;
    juce::String sourcePath;
    juce::String clipName;
    double startSample = 0.0;
    double durationSamples = 0.0;
    double sourceOffsetSamples = 0.0;
    bool loopEnabled = false;
    double loopLengthSamples = 0.0;
    float gain = 1.0f;
    double fadeInSamples = 0.0;
    double fadeOutSamples = 0.0;
    uint64_t takeGroupId = 0;
    uint64_t takeId = 0;
    bool takeActive = true;
    bool recordedTake = false;
};

struct PersistedTakeFolderState
{
    TakeFolderState state;
};

struct PersistedAutomationPoint
{
    double samplePosition = 0.0;
    float value = 0.0f;
};

struct PersistedAutomationLane
{
    int parameter = 0;
    int sendSlot = -1;
    int mode = 0;
    float defaultValue = 0.0f;
    std::vector<PersistedAutomationPoint> points;
};

struct PersistedTrackState
{
    struct SendRoute { BusId targetBus; float level = 1.0f; bool preFader = false; bool postFaderBeforePan = false; };
    struct FxSlot
    {
        juce::String persistentIdentifier;
        juce::MemoryBlock state;
        bool bypassed = false;
    };
    static constexpr size_t maxSends = 8;
    static constexpr size_t maxFxSlots = 8;
    TrackId id;
    TrackType type = TrackType::audio;
    juce::String name;
    float volume = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
    bool soloSafe = false;
    bool inputMonitoring = false;
    bool autoInputMonitoring = false;
    MonitorDestination monitorDestination = MonitorDestination::off;
    int inputChannel = 0;
    int inputChannelCount = 1;
    int midiInputChannel = 0;
    // Empty means All MIDI Inputs. A non-empty value is JUCE's stable device identifier.
    juce::String midiInputDeviceIdentifier;
    BusId outputBus;
    std::array<SendRoute, maxSends> sends {};
    uint8_t activeSendCount = 0;
    std::array<FxSlot, maxFxSlots> fxSlots {};
    std::vector<PersistedAutomationLane> automationLanes;
    std::vector<PersistedClipState> clips;
    std::vector<PersistedTakeFolderState> takeFolders;
};

struct PersistedBusState
{
    BusId id;
    juce::String name;
    float gain = 1.0f;
    bool muted = false;
    std::array<PersistedTrackState::FxSlot, PersistedTrackState::maxFxSlots> fxSlots {};
    float balance = 0.0f;
};

struct PersistedTempoEvent
{
    double samplePosition = 0.0;
    double bpm = 120.0;
};

struct PersistedTimeSignatureEvent
{
    double samplePosition = 0.0;
    int numerator = 4;
    int denominator = 4;
};

struct PersistedMarkerState
{
    double samplePosition = 0.0;
    juce::String name;
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
    double contentOffsetSamples = 0.0;
    double durationSamples = 0.0;
    std::vector<PersistedMidiNoteState> notes;
};

// Kept deliberately independent from a concrete plug-in object. A macro may
// only be rebound when its exact bus, rack slot and plug-in instance identity
// still resolve after the project has loaded.
struct PersistedPerformanceMacroTarget
{
    juce::String parameterIdentifier;
    juce::String displayName;
    int semantic = 0;
    float minimum = 0.0f;
    float maximum = 1.0f;
};

struct PersistedPerformanceMacroState
{
    int macroIndex = -1;
    int state = 0;
    float value = 0.0f;
    BusId bus;
    int pluginSlot = -1;
    juce::String displayName;
    juce::String pluginIdentifier;
    std::vector<PersistedPerformanceMacroTarget> targets;
};

struct ProjectState
{
    static constexpr int formatVersion = 34;

    double bpm = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;
    bool cycleActive = false;
    double cycleStartSample = 0.0;
    double cycleEndSample = 0.0;
    bool punchActive = false;
    double punchInSample = 0.0;
    double punchOutSample = 0.0;
    std::vector<PersistedTempoEvent> tempoMap;
    std::vector<PersistedTimeSignatureEvent> timeSignatureMap;
    std::vector<PersistedMarkerState> markers;
    std::vector<PersistedBusState> buses;
    std::vector<PersistedTrackState> tracks;
    std::vector<PersistedMidiClipState> midiClips;
    ControlRoutingProfile controlRoutingProfile;
    TrackId liveKeyReferenceTrack;
    std::vector<PersistedPerformanceMacroState> performanceMacros;
};
