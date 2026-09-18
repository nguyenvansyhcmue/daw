#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace StudioForgeTheme
{
inline const juce::Colour workspaceBackground { 0xff1d1f21 };
inline const juce::Colour panelBackground { 0xff2a2d30 };
inline const juce::Colour raisedSurface { 0xff34373a };
inline const juce::Colour recessedSurface { 0xff17191b };
inline const juce::Colour separator { 0xff45494d };
inline const juce::Colour primaryText { 0xffe2e5e8 };
inline const juce::Colour secondaryText { 0xffa8afb6 };
inline const juce::Colour accentBlue { 0xff6f8fae };
inline const juce::Colour accentCyan { 0xffb7c9d8 };
inline const juce::Colour muteAmber { 0xffc99b45 };
inline const juce::Colour soloYellow { 0xffd4bb50 };
inline const juce::Colour recordRed { 0xffbd5b56 };

struct UIMetrics
{
    static constexpr int compactControlHeight = 20;
    static constexpr int trackHeaderHeight = 42;
    static constexpr float cornerRadius = 3.0f;
    static constexpr int compactGap = 3;
};
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
