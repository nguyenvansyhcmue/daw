#pragma once

#include "../Models/ProjectIdentifiers.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstddef>
#include <vector>

struct MidiNoteEvent
{
    MidiEventId id;
    int pitch = 60;
    float velocity = 1.0f;
    double startSample = 0.0;
    double durationSamples = 1.0;
    int channel = 1;
};

struct MidiClipState
{
    MidiClipId id;
    TrackId trackId;
    double startSample = 0.0;
    std::vector<MidiNoteEvent> notes;
};

struct ImportedMidiTrack
{
    juce::String name;
    std::vector<MidiNoteEvent> notes;
};

struct ScheduledMidiEvent
{
    TrackId trackId;
    int sampleOffset = 0;
    int pitch = 60;
    float velocity = 0.0f;
    int channel = 1;
    bool noteOn = true;
};

// Fixed-size event for lock-free callback queues. SysEx is deliberately not
// accepted here because variable-size messages are not realtime safe.
enum class MidiMessageType : uint8_t { noteOn, noteOff, controller, pitchBend, channelPressure, programChange };

struct MidiRealtimeEvent
{
    MidiMessageType type = MidiMessageType::noteOff;
    int channel = 1;
    int data1 = 0;
    int data2 = 0;

    static bool fromMessage(const juce::MidiMessage&, MidiRealtimeEvent&) noexcept;
    juce::MidiMessage toMessage() const noexcept;
};

class MidiEventBuffer final
{
public:
    static constexpr size_t capacity = 256;
    void clear() noexcept { count = 0; }
    bool add(ScheduledMidiEvent event) noexcept { if (count == capacity) return false; events[count++] = event; return true; }
    size_t size() const noexcept { return count; }
    const ScheduledMidiEvent& operator[](size_t index) const noexcept { return events[index]; }
private:
    std::array<ScheduledMidiEvent, capacity> events {};
    size_t count = 0;
};

class MidiScheduler final
{
public:
    static void scheduleBlock(const std::vector<MidiClipState>& clips, double blockStartSample,
                              int numSamples, MidiEventBuffer& destination) noexcept;
    static double quantizeSample(double sample, double gridSamples) noexcept;
};
