#include "MidiCore.h"

#include <algorithm>
#include <cmath>

bool MidiRealtimeEvent::fromMessage(const juce::MidiMessage& message, MidiRealtimeEvent& destination) noexcept
{
    destination.channel = juce::jlimit(1, 16, message.getChannel());
    if (message.isNoteOn()) { destination = { MidiMessageType::noteOn, destination.channel, message.getNoteNumber(), juce::roundToInt(message.getFloatVelocity() * 127.0f) }; return true; }
    if (message.isNoteOff()) { destination = { MidiMessageType::noteOff, destination.channel, message.getNoteNumber(), 0 }; return true; }
    if (message.isController()) { destination = { MidiMessageType::controller, destination.channel, message.getControllerNumber(), message.getControllerValue() }; return true; }
    if (message.isPitchWheel()) { destination = { MidiMessageType::pitchBend, destination.channel, message.getPitchWheelValue(), 0 }; return true; }
    if (message.isChannelPressure()) { destination = { MidiMessageType::channelPressure, destination.channel, message.getChannelPressureValue(), 0 }; return true; }
    if (message.isProgramChange()) { destination = { MidiMessageType::programChange, destination.channel, message.getProgramChangeNumber(), 0 }; return true; }
    return false;
}

juce::MidiMessage MidiRealtimeEvent::toMessage() const noexcept
{
    const auto safeChannel = juce::jlimit(1, 16, channel);
    switch (type)
    {
        case MidiMessageType::noteOn: return juce::MidiMessage::noteOn(safeChannel, juce::jlimit(0, 127, data1),
                                                                         static_cast<juce::uint8>(juce::jlimit(0, 127, data2)));
        case MidiMessageType::noteOff: return juce::MidiMessage::noteOff(safeChannel, juce::jlimit(0, 127, data1));
        case MidiMessageType::controller: return juce::MidiMessage::controllerEvent(safeChannel, juce::jlimit(0, 127, data1), juce::jlimit(0, 127, data2));
        case MidiMessageType::pitchBend: return juce::MidiMessage::pitchWheel(safeChannel, juce::jlimit(0, 16383, data1));
        case MidiMessageType::channelPressure: return juce::MidiMessage::channelPressureChange(safeChannel, juce::jlimit(0, 127, data1));
        case MidiMessageType::programChange: return juce::MidiMessage::programChange(safeChannel, juce::jlimit(0, 127, data1));
    }
    return {};
}

void MidiScheduler::scheduleBlock(const std::vector<MidiClipState>& clips, double blockStart,
                                  int numSamples, MidiEventBuffer& destination) noexcept
{
    destination.clear();
    const auto blockEnd = blockStart + numSamples;
    for (const auto& clip : clips)
        for (const auto& note : clip.notes)
        {
            const auto on = clip.startSample + note.startSample;
            const auto off = on + note.durationSamples;
            if (on >= blockStart && on < blockEnd)
                destination.add({ clip.trackId, static_cast<int>(on - blockStart), note.pitch, note.velocity, note.channel, true });
            if (off >= blockStart && off < blockEnd)
                destination.add({ clip.trackId, static_cast<int>(off - blockStart), note.pitch, 0.0f, note.channel, false });
        }
}

double MidiScheduler::quantizeSample(double sample, double gridSamples) noexcept
{
    return gridSamples > 0.0 ? std::round(sample / gridSamples) * gridSamples : sample;
}
