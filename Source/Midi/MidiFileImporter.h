#pragma once

#include "MidiCore.h"

class MidiFileImporter final
{
public:
    [[nodiscard]] static juce::Result read(const juce::File& file,
                                           double targetSampleRate,
                                           double targetBpm,
                                           std::vector<ImportedMidiTrack>& destination);
};
