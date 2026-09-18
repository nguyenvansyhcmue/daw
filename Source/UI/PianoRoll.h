#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Models/TrackDataModel.h"

class PianoRoll final : public juce::Component
{
public:
    explicit PianoRoll(TrackDataModel* model = nullptr);
    void setActiveClip(MidiClipId clip) noexcept { activeClip = clip; repaint(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { selectedNote = {}; }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    MidiClipId ensureActiveClip();
    int pitchForY(float y) const noexcept;
    double sampleForX(float x) const noexcept;
    TrackDataModel* trackModel = nullptr;
    MidiClipId activeClip;
    MidiEventId selectedNote;
    int topPitch = 84;
    static constexpr float keyHeight = 14.0f;
};
