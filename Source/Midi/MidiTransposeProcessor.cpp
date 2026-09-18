#include "MidiTransposeProcessor.h"

#include <cstring>

MidiTransposeProcessor::MidiTransposeProcessor(int semitonesToUse) noexcept
{
    setSemitones(semitonesToUse);
}

void MidiTransposeProcessor::prepareToPlay(double, int, int)
{
    transformedMidi.ensureSize(8192);
}

void MidiTransposeProcessor::processBlock(juce::AudioBuffer<float>&)
{
}

void MidiTransposeProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer& midi)
{
    transformedMidi.clear();
    juce::MidiBuffer::Iterator iterator(midi);
    juce::MidiMessage message;
    int samplePosition = 0;
    while (iterator.getNextEvent(message, samplePosition))
    {
        if (message.isNoteOnOrOff())
            message.setNoteNumber(juce::jlimit(0, 127, message.getNoteNumber() + semitones));
        transformedMidi.addEvent(message, samplePosition);
    }
    midi.swapWith(transformedMidi);
}

void MidiTransposeProcessor::releaseResources()
{
    transformedMidi.clear();
}

juce::String MidiTransposeProcessor::getName() const
{
    return "MIDI Transpose";
}

juce::String MidiTransposeProcessor::getPersistentIdentifier() const
{
    return "studioforge.midi.transpose";
}

bool MidiTransposeProcessor::getState(juce::MemoryBlock& state) const
{
    state.setSize(sizeof(semitones));
    std::memcpy(state.getData(), &semitones, sizeof(semitones));
    return true;
}

bool MidiTransposeProcessor::setState(const void* data, size_t size)
{
    if (data == nullptr || size != sizeof(semitones))
        return false;

    int restored = 0;
    std::memcpy(&restored, data, sizeof(restored));
    setSemitones(restored);
    return true;
}

bool MidiTransposeProcessor::isMidiEffect() const
{
    return true;
}

void MidiTransposeProcessor::setSemitones(int value) noexcept
{
    semitones = juce::jlimit(-48, 48, value);
}

int MidiTransposeProcessor::getSemitones() const noexcept
{
    return semitones;
}
