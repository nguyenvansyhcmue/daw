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
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    MidiClipId ensureActiveClip();
    int pitchForY(float y) const noexcept;
    double sampleForX(float x) const noexcept;
    TrackDataModel* trackModel = nullptr;
    MidiClipId activeClip;
    MidiEventId selectedNote;
    MidiNoteEvent copiedNote;
    bool hasCopiedNote = false;
    int noteDragOriginalPitch = 60;
    int noteDragPreviewPitch = 60;
    double noteDragOriginalStartSample = 0.0;
    double noteDragPreviewStartSample = 0.0;
    double noteDragOriginalDurationSamples = 0.0;
    double noteDragPreviewDurationSamples = 0.0;
    double noteDragGrabOffsetSamples = 0.0;
    bool draggingNote = false;
    bool resizingNote = false;
    bool editingVelocity = false;
    float velocityOriginal = 0.0f;
    float velocityPreview = 0.0f;
    int topPitch = 84;
    static constexpr float keyHeight = 14.0f;
    static constexpr float velocityLaneHeight = 68.0f;
    float noteEditorHeight() const noexcept { return juce::jmax(0.0f, getHeight() - velocityLaneHeight); }
};
