#include "MidiFileImporter.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace
{
double timestampToSamples(double timestamp, short timeFormat, double sampleRate, double bpm) noexcept
{
    if (timeFormat > 0)
        return timestamp * sampleRate * 60.0 / (bpm * static_cast<double>(timeFormat));

    const auto framesPerSecond = -(timeFormat >> 8);
    const auto ticksPerFrame = timeFormat & 0xff;
    return framesPerSecond > 0 && ticksPerFrame > 0
        ? timestamp * sampleRate / static_cast<double>(framesPerSecond * ticksPerFrame)
        : 0.0;
}
}

juce::Result MidiFileImporter::read(const juce::File& file,
                                    double targetSampleRate,
                                    double targetBpm,
                                    std::vector<ImportedMidiTrack>& destination)
{
    destination.clear();
    if (! file.existsAsFile())
        return juce::Result::fail("The selected MIDI file does not exist.");

    auto stream = file.createInputStream();
    if (stream == nullptr)
        return juce::Result::fail("StudioForge could not open the MIDI file.");

    juce::MidiFile midiFile;
    if (! midiFile.readFrom(*stream) || midiFile.getNumTracks() == 0)
        return juce::Result::fail("The selected file is not a readable Standard MIDI file.");

    const auto sampleRate = juce::jmax(1.0, targetSampleRate);
    const auto bpm = juce::jlimit(20.0, 400.0, targetBpm);
    const auto timeFormat = midiFile.getTimeFormat();

    for (int trackIndex = 0; trackIndex < midiFile.getNumTracks(); ++trackIndex)
    {
        const auto* sourceTrack = midiFile.getTrack(trackIndex);
        if (sourceTrack == nullptr)
            continue;

        juce::MidiMessageSequence sequence(*sourceTrack);
        sequence.updateMatchedPairs();
        ImportedMidiTrack imported;
        imported.name = "MIDI " + juce::String(trackIndex + 1);

        for (int eventIndex = 0; eventIndex < sequence.getNumEvents(); ++eventIndex)
        {
            const auto* event = sequence.getEventPointer(eventIndex);
            if (event == nullptr)
                continue;

            const auto& message = event->message;
            if (message.isTrackNameEvent())
            {
                const auto name = message.getTextFromTextMetaEvent().trim();
                if (name.isNotEmpty())
                    imported.name = name;
                continue;
            }

            if (! message.isNoteOn() || event->noteOffObject == nullptr)
                continue;

            const auto start = timestampToSamples(message.getTimeStamp(), timeFormat, sampleRate, bpm);
            const auto end = timestampToSamples(event->noteOffObject->message.getTimeStamp(), timeFormat, sampleRate, bpm);
            imported.notes.push_back({ {}, message.getNoteNumber(), message.getFloatVelocity(),
                                       juce::jmax(0.0, start), juce::jmax(1.0, end - start), message.getChannel() });
        }

        if (! imported.notes.empty())
            destination.push_back(std::move(imported));
    }

    return destination.empty()
        ? juce::Result::fail("The MIDI file contains no paired note-on and note-off events.")
        : juce::Result::ok();
}
