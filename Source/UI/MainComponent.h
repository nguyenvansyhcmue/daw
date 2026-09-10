#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Models/TrackDataModel.h"
#include "../AudioEngine/AudioEngine.h"
#include "ArrangeWindow.h"
#include "AudioPeakMeter.h"
#include "ControlBar.h"
#include "MixerPane.h"

class LogicProLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LogicProLookAndFeel();

    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
    void drawRotarySlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;
    void drawLinearSlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float minSliderPos,
                          float maxSliderPos,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

class MainComponent final : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    TrackDataModel& getTrackDataModel() noexcept { return trackDataModel; }

private:
    LogicProLookAndFeel lookAndFeel;
    TrackDataModel trackDataModel;
    AudioEngine audioEngine { &trackDataModel };
    ControlBar controlBar { &trackDataModel };
    ArrangeWindow arrangeWindow { &trackDataModel };
    MixerPane mixerPane { &trackDataModel, &audioEngine };
};
