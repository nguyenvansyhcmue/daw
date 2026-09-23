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
    const auto newPosition = juce::String::formatted("%03d:%02d:%03d",
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
    auto bounds = getLocalBounds().reduced(1);
    g.setColour(StudioForgeTheme::lcdBackground);
    g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
    // Keep the cyan reserved for live values; the enclosure itself stays
    // charcoal so the transport reads as one restrained dark console.
    g.setColour(juce::Colour(0xff26343a));
    g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);

    auto contentArea = bounds.reduced(9, 6);
    auto positionArea = contentArea.removeFromLeft(contentArea.getWidth() * 62 / 100);
    auto statisticsArea = contentArea.reduced(5, 0);
    const juce::String monoName { "Consolas" };
    g.setColour(juce::Colour(0xff26343a));
    g.drawVerticalLine(positionArea.getRight() + 2, static_cast<float>(contentArea.getY()), static_cast<float>(contentArea.getBottom()));
    auto positionLabelArea = positionArea.removeFromTop(10);
    auto statisticsLabelArea = statisticsArea.removeFromTop(10);
    const auto barLabelArea = positionLabelArea.getProportion(juce::Rectangle<float>(0.00f, 0.0f, 0.30f, 1.0f));
    const auto beatLabelArea = positionLabelArea.getProportion(juce::Rectangle<float>(0.34f, 0.0f, 0.26f, 1.0f));
    const auto tickLabelArea = positionLabelArea.getProportion(juce::Rectangle<float>(0.70f, 0.0f, 0.30f, 1.0f));
    const auto tempoLabelArea = statisticsLabelArea.getProportion(juce::Rectangle<float>(0.00f, 0.0f, 0.65f, 1.0f));
    const auto meterLabelArea = statisticsLabelArea.getProportion(juce::Rectangle<float>(0.65f, 0.0f, 0.35f, 1.0f));
    g.setFont(juce::Font(juce::FontOptions(monoName, 8.0f, juce::Font::bold)));
    g.setColour(StudioForgeTheme::lcdCyan.withAlpha(0.72f));
    g.drawText("BAR", barLabelArea, juce::Justification::centred, false);
    g.drawText("BEAT", beatLabelArea, juce::Justification::centred, false);
    g.drawText("TICK", tickLabelArea, juce::Justification::centred, false);
    g.drawText("TEMPO", tempoLabelArea, juce::Justification::centred, false);
    g.drawText("METER", meterLabelArea, juce::Justification::centred, false);
    g.setFont(juce::Font(juce::FontOptions(monoName, 19.0f, juce::Font::bold)));
    g.setColour(StudioForgeTheme::lcdCyan);
    g.drawText(positionText.substring(0, 3), barLabelArea.withY(positionArea.getY()).withHeight(positionArea.getHeight()), juce::Justification::centred, false);
    g.drawText(positionText.substring(4, 6), beatLabelArea.withY(positionArea.getY()).withHeight(positionArea.getHeight()), juce::Justification::centred, false);
    g.drawText(positionText.substring(7, 10), tickLabelArea.withY(positionArea.getY()).withHeight(positionArea.getHeight()), juce::Justification::centred, false);
    g.setFont(juce::Font(juce::FontOptions(monoName, 13.0f, juce::Font::bold)));
    g.drawText(tempoText, tempoLabelArea.withY(statisticsArea.getY()).withHeight(statisticsArea.getHeight()), juce::Justification::centred, false);
    g.drawText(meterText, meterLabelArea.withY(statisticsArea.getY()).withHeight(statisticsArea.getHeight()), juce::Justification::centred, false);
}
