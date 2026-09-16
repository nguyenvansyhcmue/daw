#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace StudioForgeTheme
{
inline const juce::Colour workspaceBackground { 0xff252526 };
inline const juce::Colour panelBackground { 0xff343434 };
inline const juce::Colour accentBlue { 0xff6f88a8 };
inline const juce::Colour accentCyan { 0xffa7c5df };
}

class StudioForgeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    StudioForgeLookAndFeel();

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPosition,
                          float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPosition,
                          float minimumPosition, float maximumPosition, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};
