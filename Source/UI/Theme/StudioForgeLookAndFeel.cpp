#include "StudioForgeLookAndFeel.h"

#include <cmath>

StudioForgeLookAndFeel::StudioForgeLookAndFeel()
{
    setColour(juce::TextButton::buttonOnColourId, StudioForgeTheme::accentBlue);
    setColour(juce::ComboBox::backgroundColourId, StudioForgeTheme::panelBackground);
    setColour(juce::Slider::backgroundColourId, StudioForgeTheme::panelBackground);
    setColour(juce::Slider::trackColourId, StudioForgeTheme::accentBlue);
    setColour(juce::Slider::thumbColourId, StudioForgeTheme::accentCyan);
    setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff26343a));
    setColour(juce::AlertWindow::outlineColourId, juce::Colours::white.withAlpha(0.13f));
    setColour(juce::AlertWindow::textColourId, StudioForgeTheme::primaryText);
}

void StudioForgeLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                   const juce::Colour& background, bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    if (down) bounds = bounds.translated(0.0f, 1.0f);
    if (background.getAlpha() == 0)
    {
        if (down) { g.setColour(StudioForgeTheme::accentBlue.withAlpha(0.22f)); g.fillRoundedRectangle(bounds, StudioForgeTheme::UIMetrics::cornerRadius); }
        else if (highlighted) { g.setColour(StudioForgeTheme::primaryText.withAlpha(0.30f)); g.drawRoundedRectangle(bounds, StudioForgeTheme::UIMetrics::cornerRadius, 1.0f); }
        return;
    }

    auto base = button.getToggleState() ? StudioForgeTheme::accentBlue : background;
    base = down ? base.darker(0.20f) : highlighted ? base.brighter(0.08f) : base;
    g.setGradientFill(juce::ColourGradient(base.brighter(down ? 0.0f : 0.06f), bounds.getX(), bounds.getY(),
                                            base.darker(0.12f), bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, StudioForgeTheme::UIMetrics::cornerRadius);
    g.setColour(down ? juce::Colours::black.withAlpha(0.55f) : juce::Colours::white.withAlpha(0.14f));
    g.drawRoundedRectangle(bounds, StudioForgeTheme::UIMetrics::cornerRadius, 1.0f);
}

void StudioForgeLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                               float sliderPosition, float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    const auto radius = juce::jmax(4.0f, juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 4.0f);
    const auto centre = bounds.getCentre();
    // Pan convention: centre is 12 o'clock; left and right move symmetrically
    // toward 10:30 and 1:30, respectively.
    const auto angle = -juce::MathConstants<float>::halfPi
                     + (sliderPosition - 0.5f) * juce::MathConstants<float>::halfPi;
    const auto socketRadius = radius + 2.0f;
    g.setColour(StudioForgeTheme::recessedSurface); g.fillEllipse(centre.x - socketRadius, centre.y - socketRadius, socketRadius * 2.0f, socketRadius * 2.0f);
    g.setColour(juce::Colours::black.withAlpha(0.70f)); g.drawEllipse(centre.x - socketRadius, centre.y - socketRadius, socketRadius * 2.0f, socketRadius * 2.0f, 1.0f);
    juce::ColourGradient body(juce::Colour(0xff53575a), centre.x - radius * 0.5f, centre.y - radius * 0.7f,
                              juce::Colour(0xff202326), centre.x + radius, centre.y + radius, true);
    g.setGradientFill(body); g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.18f)); g.drawEllipse(centre.x - radius + 0.5f, centre.y - radius + 0.5f, radius * 2.0f - 1.0f, radius * 2.0f - 1.0f, 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.55f)); g.drawEllipse(centre.x - radius - 0.5f, centre.y - radius - 0.5f, radius * 2.0f + 1.0f, radius * 2.0f + 1.0f, 1.0f);
    const auto notchEnd = juce::Point<float>(centre.x + std::cos(angle) * radius * 0.72f, centre.y + std::sin(angle) * radius * 0.72f);
    g.setColour(StudioForgeTheme::primaryText.withAlpha(0.88f));
    g.drawLine(centre.x, centre.y, notchEnd.x, notchEnd.y, 1.5f);
}

void StudioForgeLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                               float, float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    const auto proportion = static_cast<float>(slider.valueToProportionOfLength(slider.getValue()));
    g.setColour(StudioForgeTheme::recessedSurface); g.fillRoundedRectangle(area.reduced(1.0f), StudioForgeTheme::UIMetrics::cornerRadius);
    if (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical)
    {
        const auto trackHeight = area.getHeight() - 18.0f;
        const auto trackY = area.getY() + 9.0f;
        const auto trackX = area.getCentreX() - 2.0f;
        g.setColour(juce::Colour(0xff111315)); g.fillRoundedRectangle(trackX - 1.0f, trackY, 6.0f, trackHeight, 2.0f);
        g.setColour(juce::Colour(0xff4e6275)); g.fillRoundedRectangle(trackX, trackY + 1.0f, 4.0f, trackHeight - 2.0f, 2.0f);
        const auto knobY = juce::jlimit(trackY, trackY + trackHeight, trackY + trackHeight * (1.0f - proportion));
        const auto capWidth = juce::jlimit(14.0f, 26.0f, area.getWidth() * 0.42f);
        const auto capX = area.getCentreX() - capWidth * 0.5f;
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd5d8da), capX, knobY - 4.0f,
                                                juce::Colour(0xff5d6368), capX, knobY + 5.0f, false));
        g.fillRoundedRectangle(capX, knobY - 4.0f, capWidth, 8.0f, 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawRoundedRectangle(capX, knobY - 4.0f, capWidth, 8.0f, 2.0f, 1.0f);
        return;
    }
    const auto trackWidth = area.getWidth() * 0.75f;
    const auto trackX = area.getX() + area.getWidth() * 0.125f;
    g.setColour(juce::Colour(0xff53616d)); g.fillRect(trackX, area.getCentreY() - 1.0f, trackWidth, 2.0f);
    const auto knobX = juce::jlimit(trackX, trackX + trackWidth, trackX + trackWidth * proportion);
    g.setColour(juce::Colour(0xffc5cbd0)); g.fillEllipse(knobX - 5.0f, area.getCentreY() - 5.0f, 10.0f, 10.0f);
}

void StudioForgeLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    auto area = button.getLocalBounds().toFloat();
    if (down) area = area.translated(0.0f, 1.0f);
    auto colour = button.getToggleState() ? StudioForgeTheme::accentBlue : juce::Colour(0xff45484b);
    colour = down ? colour.darker(0.22f) : highlighted ? colour.brighter(0.18f) : colour;
    g.setColour(colour); g.fillRoundedRectangle(area.reduced(1.0f), StudioForgeTheme::UIMetrics::cornerRadius);
    g.setColour(down ? juce::Colours::black.withAlpha(0.55f) : juce::Colours::white.withAlpha(highlighted ? 0.28f : 0.12f));
    g.drawRoundedRectangle(area.reduced(1.0f), StudioForgeTheme::UIMetrics::cornerRadius, 1.0f);
    g.setColour(juce::Colours::white); g.drawText(button.getButtonText(), area.reduced(4.0f), juce::Justification::centred, true);
}

juce::AlertWindow* StudioForgeLookAndFeel::createAlertWindow(const juce::String& title, const juce::String& message,
                                                              const juce::String& button1, const juce::String& button2,
                                                              const juce::String& button3, juce::MessageBoxIconType iconType,
                                                              int numButtons, juce::Component* associatedComponent)
{
    return juce::LookAndFeel_V2::createAlertWindow(title, message, button1, button2, button3,
                                                    iconType, numButtons, associatedComponent);
}

void StudioForgeLookAndFeel::drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert,
                                          const juce::Rectangle<int>&, juce::TextLayout& textLayout)
{
    constexpr auto cornerRadius = 7.0f;
    auto bounds = alert.getLocalBounds().reduced(1);

    g.setColour(alert.findColour(juce::AlertWindow::backgroundColourId));
    g.fillRoundedRectangle(bounds.toFloat(), cornerRadius);
    g.setColour(alert.findColour(juce::AlertWindow::outlineColourId));
    g.drawRoundedRectangle(bounds.toFloat(), cornerRadius, 1.0f);

    auto content = bounds.reduced(24, 18);
    if (alert.getAlertType() == juce::MessageBoxIconType::WarningIcon)
    {
        const auto iconArea = juce::Rectangle<int>(content.getX(), content.getY() + 4, 20, 18);
        content.removeFromLeft(30);
        juce::Path icon;
        icon.addTriangle(iconArea.getCentreX(), static_cast<float>(iconArea.getY()),
                         static_cast<float>(iconArea.getRight()), static_cast<float>(iconArea.getBottom()),
                         static_cast<float>(iconArea.getX()), static_cast<float>(iconArea.getBottom()));
        icon = icon.createPathWithRoundedCorners(3.0f);
        g.setColour(StudioForgeTheme::recordRed.withAlpha(0.86f));
        g.fillPath(icon);
        g.setColour(juce::Colours::white.withAlpha(0.94f));
        g.setFont(13.0f);
        g.drawText("!", iconArea, juce::Justification::centred, true);
        content.removeFromLeft(4);
    }

    const auto textBounds = content.withTrimmedBottom(getAlertWindowButtonHeight() + 12);
    g.setColour(alert.findColour(juce::AlertWindow::textColourId));
    textLayout.draw(g, textBounds.toFloat());
}
