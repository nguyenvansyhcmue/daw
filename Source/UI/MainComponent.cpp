#include "MainComponent.h"

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
const auto darkBackground = juce::Colour(0xff1a1a1a);
const auto panelBackground = juce::Colour(0xff2b2b2b);
const auto accentBlue = juce::Colour(0xff00a8ff);
const auto accentCyan = juce::Colour(0xff00ffe0);
}

LogicProLookAndFeel::LogicProLookAndFeel()
{
    setColour(juce::TextButton::buttonOnColourId, accentCyan);
    setColour(juce::ComboBox::backgroundColourId, panelBackground);
    setColour(juce::Slider::backgroundColourId, panelBackground);
    setColour(juce::Slider::trackColourId, accentBlue);
    setColour(juce::Slider::thumbColourId, accentCyan);
}

void LogicProLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                               juce::Button& button,
                                               const juce::Colour& backgroundColour,
                                               bool shouldDrawButtonAsHighlighted,
                                               bool shouldDrawButtonAsDown)
{
    const auto base = button.getToggleState() ? accentCyan.withAlpha(0.85f) : backgroundColour;
    const auto bounds = button.getLocalBounds().toFloat();
    auto gradient = juce::ColourGradient(base, bounds.getX(), bounds.getY(), accentBlue, bounds.getRight(), bounds.getBottom(), false);

    g.setGradientFill(gradient);
    g.fillRoundedRectangle(bounds.reduced(2.0f), 8.0f);

}

void LogicProLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                          int x,
                                          int y,
                                          int width,
                                          int height,
                                          float sliderPos,
                                          float rotaryStartAngle,
                                          float rotaryEndAngle,
                                          juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 10.0f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * sliderPos;

    g.setColour(juce::Colour(0xff2b2b2b));
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0, rotaryStartAngle, angle, true);
    g.setColour(accentCyan);
    g.strokePath(arc, juce::PathStrokeType(3.0f));

    const auto knobCentre = juce::Point<float>(centre.x + std::cos(angle) * (radius * 0.75f), centre.y + std::sin(angle) * (radius * 0.75f));
    g.setColour(juce::Colour(0xff00a8ff));
    g.fillEllipse(knobCentre.x - 5.0f, knobCentre.y - 5.0f, 10.0f, 10.0f);
}

void LogicProLookAndFeel::drawLinearSlider(juce::Graphics& g,
                                          int x,
                                          int y,
                                          int width,
                                          int height,
                                          float sliderPos,
                                          float minSliderPos,
                                          float maxSliderPos,
                                          juce::Slider::SliderStyle style,
                                          juce::Slider& slider)
{
    auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    g.setColour(panelBackground);
    g.fillRoundedRectangle(area.reduced(2.0f), 6.0f);

    g.setColour(accentBlue);
    const float trackWidth = area.getWidth() * 0.75f;
    const float trackX = area.getX() + area.getWidth() * 0.125f;
    const float trackY = area.getCentreY() - 2.0f;
    g.fillRect(trackX, trackY, trackWidth, 4.0f);

    const float knobX = juce::jlimit(trackX, trackX + trackWidth, trackX + trackWidth * sliderPos);
    g.setColour(accentCyan);
    g.fillEllipse(knobX - 8.0f, area.getCentreY() - 8.0f, 16.0f, 16.0f);
}

void LogicProLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const auto area = button.getLocalBounds().toFloat();
    g.setColour(button.getToggleState() ? accentCyan : juce::Colour(0xff565656));
    g.fillRoundedRectangle(area.reduced(2.0f), 6.0f);
    g.setColour(juce::Colours::white);
    g.drawText(button.getButtonText(), area.reduced(4.0f), juce::Justification::centred, true);
}

MainComponent::MainComponent()
{
    audioEngine.initialise();
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(controlBar);
    addAndMakeVisible(arrangeWindow);
    addAndMakeVisible(mixerPane);

    trackDataModel.ensureTrackCount(8);
    trackDataModel.getTrack(0).volume = 0.9f;
    trackDataModel.getTrack(1).volume = 1.0f;
    trackDataModel.getTrack(2).volume = 0.8f;
    trackDataModel.getTrack(3).volume = 0.7f;
    trackDataModel.getTrack(4).volume = 0.9f;
    trackDataModel.getTrack(5).volume = 1.0f;
    trackDataModel.getTrack(6).volume = 0.8f;
    trackDataModel.getTrack(7).volume = 0.9f;
}

MainComponent::~MainComponent()
{
    setLookAndFeel(nullptr);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(darkBackground);
    auto glow = juce::ColourGradient(accentBlue, 0.0f, 0.0f, accentCyan, getWidth(), 0.0f, false);
    g.setGradientFill(glow);
    g.fillRect(0.0f, 0.0f, static_cast<float>(getWidth()), 2.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    controlBar.setBounds(area.removeFromTop(72));
    mixerPane.setBounds(area.removeFromBottom(160));
    arrangeWindow.setBounds(area.reduced(0, 0));
}
