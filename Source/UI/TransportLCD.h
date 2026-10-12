#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class TrackDataModel;

class TransportLCD final : public juce::Component, private juce::Timer
{
public:
    explicit TransportLCD(TrackDataModel* model = nullptr);
    void setTrackModel(TrackDataModel* model) noexcept;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;

private:
    enum class EditableField { none, bar, beat, tick, tempo, meter };

    void timerCallback() override;
    EditableField fieldAt(juce::Point<float> position) const noexcept;
    void showMeterMenu();

    TrackDataModel* trackModel = nullptr;
    juce::String positionText { "001 : 01 : 000" };
    juce::String tempoText { "120.00" };
    juce::String meterText { "4/4" };
    EditableField dragField = EditableField::none;
    juce::Point<float> dragStart;
    double dragStartTempo = 120.0;
    int dragStartBar = 0;
    int dragStartBeat = 0;
    int dragStartTick = 0;
    bool dragChanged = false;
};
