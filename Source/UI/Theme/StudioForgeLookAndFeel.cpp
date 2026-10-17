#include "StudioForgeLookAndFeel.h"
#include "BinaryData.h"

#include <cmath>

namespace
{
const juce::Image& getAnalogPanKnobImage()
{
    static const auto image = juce::ImageFileFormat::loadFrom(BinaryData::analogpanknobv2_png,
                                                               BinaryData::analogpanknobv2_pngSize);
    return image;
}
}

StudioForgeLookAndFeel::StudioForgeLookAndFeel()
{
    setColour(juce::TextButton::buttonOnColourId, StudioForgeTheme::accentBlue);
    setColour(juce::ComboBox::backgroundColourId, StudioForgeTheme::panelBackground);
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff55707a).withAlpha(0.72f));
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
                                               float sliderPosition, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    const auto radius = juce::jmax(4.0f, juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f);
    const auto centre = bounds.getCentre();
    // Pan convention: centre is 12 o'clock; left and right move symmetrically
    // toward 10:30 and 1:30, respectively.
    // A broad 240-degree travel makes the entire -1..+1 PAN range legible.
    const auto angle = -juce::MathConstants<float>::halfPi
                     + (sliderPosition - 0.5f) * (juce::MathConstants<float>::pi * 1.333f);
    const auto& knobImage = getAnalogPanKnobImage();
    const auto knobBounds = juce::Rectangle<float>(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    if (knobImage.isValid())
        g.drawImageWithin(knobImage, knobBounds.getX(), knobBounds.getY(), knobBounds.getWidth(), knobBounds.getHeight(),
                          juce::RectanglePlacement::centred);
    else
    {
        g.setColour(juce::Colour(0xff182126));
        g.fillEllipse(knobBounds);
        g.setColour(juce::Colour(0xff7b8b94).withAlpha(0.65f));
        g.drawEllipse(knobBounds.reduced(1.0f), 1.0f);
    }

    // The physical body is an asset; this small illuminated index remains dynamic.
    const auto notchStart = juce::Point<float>(centre.x + std::cos(angle) * radius * 0.15f, centre.y + std::sin(angle) * radius * 0.15f);
    const auto notchEnd = juce::Point<float>(centre.x + std::cos(angle) * radius * 0.62f, centre.y + std::sin(angle) * radius * 0.62f);
    g.setColour(juce::Colour(0xffdffaff).withAlpha(0.96f));
    g.drawLine(notchStart.x, notchStart.y, notchEnd.x, notchEnd.y, juce::jmax(1.25f, radius * 0.075f));
    g.setColour(juce::Colours::black.withAlpha(0.7f));
    g.fillEllipse(centre.x - radius * 0.09f, centre.y - radius * 0.09f, radius * 0.18f, radius * 0.18f);
}

void StudioForgeLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                               float, float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    const auto proportion = static_cast<float>(slider.valueToProportionOfLength(slider.getValue()));
    if (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical)
    {
        g.setColour(StudioForgeTheme::recessedSurface); g.fillRoundedRectangle(area.reduced(1.0f), StudioForgeTheme::UIMetrics::cornerRadius);
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
    g.setColour(juce::Colour(0xff10181c));
    g.fillRoundedRectangle(trackX, area.getCentreY() - 1.5f, trackWidth, 3.0f, 1.5f);
    const auto knobX = juce::jlimit(trackX, trackX + trackWidth, trackX + trackWidth * proportion);
    g.setColour(juce::Colour(0xff4f9abc));
    g.fillRoundedRectangle(trackX, area.getCentreY() - 1.0f, juce::jmax(1.0f, knobX - trackX), 2.0f, 1.0f);
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
    g.setColour(juce::Colours::white);
    // Toggle labels are compact state abbreviations (R, I, M, S). Ellipsising
    // them turns a control into an ambiguous "..." button.
    g.drawText(button.getButtonText(), area.reduced(3.0f), juce::Justification::centred, false);
}

void StudioForgeLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                           int buttonX, int buttonY, int buttonW, int buttonH,
                                           juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f,
                                               static_cast<float>(height) - 1.0f);
    auto base = box.findColour(juce::ComboBox::backgroundColourId);
    if (isButtonDown) base = base.darker(0.13f);
    else if (box.isMouseOverOrDragging()) base = base.brighter(0.07f);
    g.setGradientFill(juce::ColourGradient(base.brighter(0.07f), bounds.getCentreX(), bounds.getY(),
                                            base.darker(0.10f), bounds.getCentreX(), bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, 4.0f);

    const auto focused = box.hasKeyboardFocus(true) || box.isMouseOverOrDragging();
    g.setColour(focused ? StudioForgeTheme::accentCyan.withAlpha(0.58f)
                        : juce::Colour(0xff59717a).withAlpha(0.68f));
    g.drawRoundedRectangle(bounds, 4.0f, focused ? 1.15f : 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.36f));
    g.drawLine(static_cast<float>(buttonX), static_cast<float>(buttonY + 3),
               static_cast<float>(buttonX), static_cast<float>(buttonY + buttonH - 3), 1.0f);

    const auto cx = static_cast<float>(buttonX + buttonW / 2);
    const auto cy = static_cast<float>(buttonY + buttonH / 2);
    juce::Path chevron;
    chevron.startNewSubPath(cx - 7.0f, cy - 3.0f);
    chevron.lineTo(cx, cy + 3.0f);
    chevron.lineTo(cx + 7.0f, cy - 3.0f);
    g.setColour(StudioForgeTheme::primaryText.withAlpha(0.88f));
    g.strokePath(chevron, juce::PathStrokeType(2.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void StudioForgeLookAndFeel::drawMenuBarBackground(juce::Graphics&, int, int, bool,
                                                    juce::MenuBarComponent&)
{
}

void StudioForgeLookAndFeel::drawMenuBarItem(juce::Graphics& g, int width, int height, int,
                                              const juce::String& itemText, bool isMouseOverItem,
                                              bool isMenuOpen, bool, juce::MenuBarComponent&)
{
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    if (isMouseOverItem || isMenuOpen)
    {
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.fillRoundedRectangle(bounds.reduced(2.0f, 3.0f), 3.0f);
    }
    g.setColour(StudioForgeTheme::primaryText.withAlpha(0.84f));
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(itemText, bounds.reduced(8.0f, 1.0f), juce::Justification::centred, false);
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
