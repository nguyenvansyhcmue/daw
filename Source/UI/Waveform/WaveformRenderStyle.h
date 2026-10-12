#pragma once

#include <juce_graphics/juce_graphics.h>

struct WaveformRenderStyle
{
    juce::Colour clipBackground { 0xff1f3141 };
    juce::Colour clipOutline { 0xff416176 };
    juce::Colour waveformBody { 0xff8dc6df };
    juce::Colour waveformCore { 0xffc5e5f2 };
    juce::Colour centreLine { 0xffa5c5d4 };
    juce::Colour selectionOutline { 0xffc9ddf0 };
    juce::Colour mutedWaveform { 0xff6d7e88 };
    juce::Colour unavailableText { 0xffffc06a };
    juce::Colour regionText { 0xffe8f2f8 };
};
