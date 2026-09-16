#include "StudioForgeLookAndFeel.h"

#include <cmath>

StudioForgeLookAndFeel::StudioForgeLookAndFeel()
{
    setColour(juce::TextButton::buttonOnColourId, StudioForgeTheme::accentCyan);
    setColour(juce::ComboBox::backgroundColourId, StudioForgeTheme::panelBackground);
    setColour(juce::Slider::backgroundColourId, StudioForgeTheme::panelBackground);
    setColour(juce::Slider::trackColourId, StudioForgeTheme::accentBlue);
    setColour(juce::Slider::thumbColourId, StudioForgeTheme::accentCyan);
}

void StudioForgeLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                   const juce::Colour& background, bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);
    if (down) bounds = bounds.translated(0.0f, 1.0f);
    if (background.getAlpha() == 0)
    {
        if (down) { g.setColour(StudioForgeTheme::accentCyan.withAlpha(0.24f)); g.fillRoundedRectangle(bounds, 8.0f); }
        else if (highlighted) { g.setColour(StudioForgeTheme::accentCyan.withAlpha(0.92f)); g.drawRoundedRectangle(bounds, 8.0f, 2.0f); }
        return;
    }

    auto base = button.getToggleState() ? StudioForgeTheme::accentCyan.withAlpha(0.90f) : background;
    base = down ? base.darker(0.24f) : highlighted ? base.brighter(0.16f) : base;
    g.setGradientFill(juce::ColourGradient(base, bounds.getX(), bounds.getY(), base.darker(down ? 0.12f : 0.06f),
                                            bounds.getRight(), bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, 8.0f);
    if (highlighted) { g.setColour(juce::Colours::white.withAlpha(0.38f)); g.drawRoundedRectangle(bounds, 8.0f, 1.0f); }
}

void StudioForgeLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                               float sliderPosition, float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 10.0f;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + (endAngle - startAngle) * sliderPosition;
    g.setColour(juce::Colour(0xff2b2b2b)); g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    juce::Path arc; arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0, startAngle, angle, true);
    g.setColour(StudioForgeTheme::accentCyan); g.strokePath(arc, juce::PathStrokeType(3.0f));
    const auto knob = juce::Point<float>(centre.x + std::cos(angle) * radius * 0.75f, centre.y + std::sin(angle) * radius * 0.75f);
    g.setColour(juce::Colour(0xff00a8ff)); g.fillEllipse(knob.x - 5.0f, knob.y - 5.0f, 10.0f, 10.0f);
}

void StudioForgeLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                               float sliderPosition, float, float, juce::Slider::SliderStyle style, juce::Slider&)
{
    const auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    g.setColour(StudioForgeTheme::panelBackground); g.fillRoundedRectangle(area.reduced(2.0f), 5.0f);
    if (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical)
    {
        const auto trackHeight = area.getHeight() - 18.0f;
        const auto trackY = area.getY() + 9.0f;
        const auto trackX = area.getCentreX() - 2.0f;
        g.setColour(juce::Colour(0xff485d72));
        g.fillRoundedRectangle(trackX, trackY, 4.0f, trackHeight, 2.0f);
        const auto knobY = juce::jlimit(trackY, trackY + trackHeight, trackY + trackHeight * (1.0f - sliderPosition));
        g.setColour(StudioForgeTheme::accentCyan);
        g.fillRoundedRectangle(area.getX() + 5.0f, knobY - 4.0f, area.getWidth() - 10.0f, 8.0f, 3.0f);
        return;
    }
    const auto trackWidth = area.getWidth() * 0.75f;
    const auto trackX = area.getX() + area.getWidth() * 0.125f;
    g.setColour(StudioForgeTheme::accentBlue); g.fillRect(trackX, area.getCentreY() - 2.0f, trackWidth, 4.0f);
    const auto knobX = juce::jlimit(trackX, trackX + trackWidth, trackX + trackWidth * sliderPosition);
    g.setColour(StudioForgeTheme::accentCyan); g.fillEllipse(knobX - 8.0f, area.getCentreY() - 8.0f, 16.0f, 16.0f);
}

void StudioForgeLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    auto area = button.getLocalBounds().toFloat();
    if (down) area = area.translated(0.0f, 1.0f);
    auto colour = button.getToggleState() ? StudioForgeTheme::accentCyan : juce::Colour(0xff565656);
    colour = down ? colour.darker(0.22f) : highlighted ? colour.brighter(0.18f) : colour;
    g.setColour(colour); g.fillRoundedRectangle(area.reduced(2.0f), 6.0f);
    if (highlighted) { g.setColour(juce::Colours::white.withAlpha(0.32f)); g.drawRoundedRectangle(area.reduced(2.0f), 6.0f, 1.0f); }
    g.setColour(juce::Colours::white); g.drawText(button.getButtonText(), area.reduced(4.0f), juce::Justification::centred, true);
}
