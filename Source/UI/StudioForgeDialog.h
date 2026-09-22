#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class StudioForgeDialog final
{
public:
    static juce::String fromUtf8(const char* text);
    static void showWarning(const juce::String& title, const juce::String& message);
};
