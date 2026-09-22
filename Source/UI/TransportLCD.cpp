#include "TransportLCD.h"

#include "../Models/TrackDataModel.h"
#include "Theme/StudioForgeLookAndFeel.h"

TransportLCD::TransportLCD(TrackDataModel* model) : trackModel(model)
{
    setInterceptsMouseClicks(false, false);
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
    const auto newPosition = juce::String::formatted("%03d : %02d : %03d",
                                                      barBeat.bars + 1, barBeat.beats + 1, barBeat.pulses);
    const auto newTempo = juce::String(trackModel->getTempoAtSample(trackModel->getPlayheadPosition()), 2);
    const auto newMeter = juce::String(trackModel->getTimeSignatureNumerator()) + "/4";
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
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(StudioForgeTheme::lcdBackground);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(StudioForgeTheme::lcdCyan.withAlpha(0.48f));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    auto positionArea = bounds.reduced(10.0f, 6.0f).withTrimmedRight(bounds.getWidth() * 0.38f);
    auto statsArea = bounds.withLeft(positionArea.getRight() + 8.0f).reduced(5.0f, 6.0f);
    g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
    g.setColour(StudioForgeTheme::lcdCyan.withAlpha(0.74f));
    g.drawText("BAR      BEAT      TICK", positionArea.removeFromTop(11.0f), juce::Justification::centredLeft, false);
    g.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::plain)));
    g.setColour(StudioForgeTheme::lcdCyan);
    g.drawText(positionText, positionArea, juce::Justification::centredLeft, false);

    g.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::plain)));
    g.drawText(tempoText, statsArea.removeFromTop(21.0f), juce::Justification::centredLeft, false);
    g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
    g.setColour(StudioForgeTheme::lcdCyan.withAlpha(0.72f));
    g.drawText("BPM", statsArea.removeFromTop(10.0f), juce::Justification::centredLeft, false);
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::plain)));
    g.setColour(StudioForgeTheme::lcdCyan);
    g.drawText(meterText + "  TIME", statsArea, juce::Justification::centredLeft, false);
}
