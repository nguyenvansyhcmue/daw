#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class TrackDataModel;

class TransportLCD final : public juce::Component, private juce::Timer
{
public:
    explicit TransportLCD(TrackDataModel* model = nullptr);
    void setTrackModel(TrackDataModel* model) noexcept;

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    TrackDataModel* trackModel = nullptr;
    juce::String positionText { "001 : 01 : 000" };
    juce::String tempoText { "120.00" };
    juce::String meterText { "4/4" };
};
