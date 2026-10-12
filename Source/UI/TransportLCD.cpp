#include "TransportLCD.h"

#include "../Models/TrackDataModel.h"
#include "Theme/StudioForgeLookAndFeel.h"

#include <array>
#include <cmath>

namespace
{
const auto ledOn = juce::Colour(0xff36d9ed);
// A visible teal inactive segment reads as a duplicate digit at normal
// viewing distance. Keep it just perceptible inside the LCD well.
const auto ledOff = juce::Colour(0xff061316);

std::array<bool, 7> segmentsFor(char character)
{
    switch (character)
    {
        case '0': return { true, true, true, true, true, true, false };
        case '1': return { false, true, true, false, false, false, false };
        case '2': return { true, true, false, true, true, false, true };
        case '3': return { true, true, true, true, false, false, true };
        case '4': return { false, true, true, false, false, true, true };
        case '5': return { true, false, true, true, false, true, true };
        case '6': return { true, false, true, true, true, true, true };
        case '7': return { true, true, true, false, false, false, false };
        case '8': return { true, true, true, true, true, true, true };
        case '9': return { true, true, true, true, false, true, true };
        default:  return { false, false, false, false, false, false, false };
    }
}

void drawLedCharacter(juce::Graphics& g, char character, juce::Rectangle<float> bounds)
{
    if (character == '.')
    {
        g.setColour(ledOn);
        g.fillRoundedRectangle(bounds.getRight() - bounds.getHeight() * 0.17f,
                               bounds.getBottom() - bounds.getHeight() * 0.17f,
                               bounds.getHeight() * 0.14f, bounds.getHeight() * 0.14f, 0.7f);
        return;
    }
    if (character == '/')
    {
        g.setColour(ledOn.withAlpha(0.90f));
        g.drawLine(bounds.getX() + bounds.getWidth() * 0.18f, bounds.getBottom() - 1.0f,
                   bounds.getRight() - bounds.getWidth() * 0.18f, bounds.getY() + 1.0f,
                   juce::jmax(1.0f, bounds.getWidth() * 0.13f));
        return;
    }

    const auto segments = segmentsFor(character);
    const auto thickness = juce::jmax(1.0f, bounds.getHeight() * 0.105f);
    const auto halfHeight = bounds.getHeight() * 0.5f;
    const auto horizontal = bounds.getWidth() - thickness * 1.65f;
    const auto vertical = halfHeight - thickness * 1.28f;
    const std::array<juce::Rectangle<float>, 7> segmentBounds
    {{
        { bounds.getX() + thickness * 0.82f, bounds.getY(), horizontal, thickness },
        { bounds.getRight() - thickness, bounds.getY() + thickness * 0.55f, thickness, vertical },
        { bounds.getRight() - thickness, bounds.getCentreY() + thickness * 0.30f, thickness, vertical },
        { bounds.getX() + thickness * 0.82f, bounds.getBottom() - thickness, horizontal, thickness },
        { bounds.getX(), bounds.getCentreY() + thickness * 0.30f, thickness, vertical },
        { bounds.getX(), bounds.getY() + thickness * 0.55f, thickness, vertical },
        { bounds.getX() + thickness * 0.82f, bounds.getCentreY() - thickness * 0.5f, horizontal, thickness }
    }};
    for (size_t index = 0; index < segmentBounds.size(); ++index)
    {
        g.setColour(segments[index] ? ledOn : ledOff);
        g.fillRoundedRectangle(segmentBounds[index], thickness * 0.32f);
    }
}

void drawLedText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> bounds, float heightRatio = 0.78f)
{
    const auto digitHeight = juce::jmax(8.0f, bounds.getHeight() * heightRatio);
    const auto digitWidth = digitHeight * 0.48f;
    auto totalWidth = 0.0f;
    for (const auto character : text)
        totalWidth += character == '.' ? digitWidth * 0.34f : digitWidth;
    totalWidth += juce::jmax(0, text.length() - 1) * digitWidth * 0.16f;

    auto x = bounds.getCentreX() - totalWidth * 0.5f;
    const auto y = bounds.getCentreY() - digitHeight * 0.5f;
    for (const auto character : text)
    {
        const auto width = character == '.' ? digitWidth * 0.34f : digitWidth;
        drawLedCharacter(g, character, { x, y, width, digitHeight });
        x += width + digitWidth * 0.16f;
    }
}
}

TransportLCD::TransportLCD(TrackDataModel* model) : trackModel(model)
{
    startTimerHz(30);
}

void TransportLCD::setTrackModel(TrackDataModel* model) noexcept
{
    trackModel = model;
    timerCallback();
}

void TransportLCD::timerCallback()
{
    if (trackModel == nullptr)
        return;

    const auto barBeat = trackModel->getBarBeatInfo(trackModel->getPlayheadPosition());
    const auto newPosition = juce::String::formatted("%03d:%02d:%03d",
                                                      barBeat.bars + 1, barBeat.beats + 1, barBeat.pulses);
    const auto newTempo = juce::String(trackModel->getTempoAtSample(trackModel->getPlayheadPosition()), 2);
    const auto signature = trackModel->getTimeSignatureAtSample(trackModel->getPlayheadPosition());
    const auto newMeter = juce::String(signature.numerator) + "/" + juce::String(signature.denominator);
    if (newPosition != positionText || newTempo != tempoText || newMeter != meterText)
    {
        positionText = newPosition;
        tempoText = newTempo;
        meterText = newMeter;
        repaint();
    }
}

void TransportLCD::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().reduced(1);
    g.setColour(StudioForgeTheme::lcdBackground);
    g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
    // Keep the cyan reserved for live values; the enclosure itself stays
    // charcoal so the transport reads as one restrained dark console.
    g.setColour(juce::Colour(0xff26343a));
    g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);

    auto contentArea = bounds.reduced(12, 7);
    auto positionArea = contentArea.removeFromLeft(contentArea.getWidth() * 62 / 100);
    auto statisticsArea = contentArea.reduced(5, 0);
    const juce::String monoName { "Consolas" };
    g.setColour(juce::Colour(0xff26343a));
    g.drawVerticalLine(positionArea.getRight() + 2, static_cast<float>(contentArea.getY()), static_cast<float>(contentArea.getBottom()));
    const auto labelHeight = juce::jlimit(10, 13, contentArea.getHeight() / 4);
    auto positionLabelArea = positionArea.removeFromTop(labelHeight);
    auto statisticsLabelArea = statisticsArea.removeFromTop(labelHeight);
    const auto barLabelArea = positionLabelArea.getProportion(juce::Rectangle<float>(0.00f, 0.0f, 0.30f, 1.0f));
    const auto beatLabelArea = positionLabelArea.getProportion(juce::Rectangle<float>(0.34f, 0.0f, 0.26f, 1.0f));
    const auto tickLabelArea = positionLabelArea.getProportion(juce::Rectangle<float>(0.70f, 0.0f, 0.30f, 1.0f));
    const auto tempoLabelArea = statisticsLabelArea.getProportion(juce::Rectangle<float>(0.00f, 0.0f, 0.65f, 1.0f));
    const auto meterLabelArea = statisticsLabelArea.getProportion(juce::Rectangle<float>(0.65f, 0.0f, 0.35f, 1.0f));
    g.setFont(juce::Font(juce::FontOptions(monoName, 9.0f, juce::Font::bold)));
    g.setColour(StudioForgeTheme::lcdCyan.withAlpha(0.72f));
    g.drawText("BAR", barLabelArea, juce::Justification::centred, false);
    g.drawText("BEAT", beatLabelArea, juce::Justification::centred, false);
    g.drawText("TICK", tickLabelArea, juce::Justification::centred, false);
    g.drawText("TEMPO", tempoLabelArea, juce::Justification::centred, false);
    g.drawText("METER", meterLabelArea, juce::Justification::centred, false);
    const auto positionValues = positionArea.reduced(0, 1).toFloat();
    const auto statisticsValues = statisticsArea.reduced(0, 1).toFloat();
    drawLedText(g, positionText.substring(0, 3),
                positionValues.getProportion(juce::Rectangle<float>(0.00f, 0.0f, 0.30f, 1.0f)), 0.90f);
    drawLedText(g, positionText.substring(4, 6),
                positionValues.getProportion(juce::Rectangle<float>(0.34f, 0.0f, 0.26f, 1.0f)), 0.90f);
    drawLedText(g, positionText.substring(7, 10),
                positionValues.getProportion(juce::Rectangle<float>(0.70f, 0.0f, 0.30f, 1.0f)), 0.90f);
    drawLedText(g, tempoText,
                statisticsValues.getProportion(juce::Rectangle<float>(0.00f, 0.0f, 0.65f, 1.0f)), 0.72f);
    drawLedText(g, meterText,
                statisticsValues.getProportion(juce::Rectangle<float>(0.65f, 0.0f, 0.35f, 1.0f)), 0.72f);
}

TransportLCD::EditableField TransportLCD::fieldAt(juce::Point<float> position) const noexcept
{
    auto bounds = getLocalBounds().reduced(1).reduced(12, 7);
    const auto positionWidth = bounds.getWidth() * 62 / 100;
    const auto positionArea = bounds.removeFromLeft(positionWidth).toFloat();
    const auto statisticsArea = bounds.reduced(5, 0).toFloat();

    if (positionArea.contains(position))
    {
        const auto relativeX = (position.x - positionArea.getX()) / positionArea.getWidth();
        if (relativeX < 0.32f) return EditableField::bar;
        if (relativeX < 0.65f) return EditableField::beat;
        return EditableField::tick;
    }
    if (statisticsArea.contains(position))
    {
        const auto relativeX = (position.x - statisticsArea.getX()) / statisticsArea.getWidth();
        return relativeX < 0.65f ? EditableField::tempo : EditableField::meter;
    }
    return EditableField::none;
}

void TransportLCD::mouseMove(const juce::MouseEvent& event)
{
    setMouseCursor(fieldAt(event.position) == EditableField::none
                       ? juce::MouseCursor::NormalCursor
                       : juce::MouseCursor::UpDownResizeCursor);
}

void TransportLCD::mouseDown(const juce::MouseEvent& event)
{
    if (trackModel == nullptr)
        return;

    dragField = fieldAt(event.position);
    if (dragField == EditableField::meter)
    {
        showMeterMenu();
        dragField = EditableField::none;
        return;
    }

    dragStart = event.position;
    dragStartTempo = trackModel->getBpm();
    const auto position = trackModel->getBarBeatInfo(trackModel->getPlayheadPosition());
    dragStartBar = position.bars;
    dragStartBeat = position.beats;
    dragStartTick = position.pulses;
    dragChanged = false;
}

void TransportLCD::mouseDrag(const juce::MouseEvent& event)
{
    if (trackModel == nullptr || dragField == EditableField::none || dragField == EditableField::meter)
        return;

    const auto deltaY = dragStart.y - event.position.y;
    if (std::abs(deltaY) < 1.0f)
        return;
    dragChanged = true;

    if (dragField == EditableField::tempo)
    {
        const auto precision = event.mods.isShiftDown() ? 0.01 : 0.10;
        trackModel->setBpm(dragStartTempo + deltaY * precision);
        return;
    }

    const auto steps = static_cast<int>(std::round(deltaY / 8.0f));
    auto bars = dragStartBar;
    auto beats = dragStartBeat;
    auto ticks = dragStartTick;
    if (dragField == EditableField::bar) bars = juce::jmax(0, bars + steps);
    else if (dragField == EditableField::beat)
        beats = juce::jlimit(0, trackModel->getTimeSignatureNumerator() - 1, beats + steps);
    else
        ticks = juce::jlimit(0, 959, ticks + steps * 24);
    trackModel->setPlayheadPosition(trackModel->barBeatToSamples(bars, beats, ticks));
}

void TransportLCD::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (fieldAt(event.position) == EditableField::meter)
        showMeterMenu();
}

void TransportLCD::showMeterMenu()
{
    if (trackModel == nullptr)
        return;

    juce::PopupMenu menu;
    const std::array<std::pair<int, int>, 8> signatures
    {{ { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 } }};
    const auto currentNumerator = trackModel->getTimeSignatureNumerator();
    const auto currentDenominator = trackModel->getTimeSignatureDenominator();
    for (size_t index = 0; index < signatures.size(); ++index)
    {
        const auto [numerator, denominator] = signatures[index];
        menu.addItem(static_cast<int>(index) + 1, juce::String(numerator) + "/" + juce::String(denominator), true,
                     numerator == currentNumerator && denominator == currentDenominator);
    }

    juce::Component::SafePointer<TransportLCD> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                       [safeThis, signatures] (int result)
                       {
                           if (safeThis == nullptr || result < 1 || result > static_cast<int>(signatures.size()))
                               return;
                           const auto [numerator, denominator] = signatures[static_cast<size_t>(result - 1)];
                           if (safeThis->trackModel == nullptr)
                               return;
                           safeThis->trackModel->setTimeSignatureAtSample(
                               safeThis->trackModel->getPlayheadPosition(), numerator, denominator);
                       });
}
