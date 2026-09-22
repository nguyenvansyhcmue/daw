#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PerformanceRoomModel.h"

// SVG-backed icons for the Performance Room. The modal does not need to know
// how assets are parsed or cached.
class PerformanceRoomIconLibrary final
{
public:
    static void draw(juce::Graphics& graphics,
                     juce::Rectangle<float> bounds,
                     PerformanceRole role);

private:
    PerformanceRoomIconLibrary() = delete;
};
