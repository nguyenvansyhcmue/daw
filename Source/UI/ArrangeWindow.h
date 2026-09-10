#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Models/TrackDataModel.h"

class TrackHeaderPanel final : public juce::Component
{
public:
    explicit TrackHeaderPanel(TrackDataModel* model = nullptr);

    void paint(juce::Graphics& g) override;
    void resized() override {}

    void setTrackModel(TrackDataModel* model) noexcept { trackModel = model; }

private:
    TrackDataModel* trackModel = nullptr;
};

class TimelineGrid final : public juce::Component
{
public:
    explicit TimelineGrid(TrackDataModel* model = nullptr);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void resized() override {}

    void setTrackModel(TrackDataModel* model) noexcept { trackModel = model; }

private:
    TrackDataModel* trackModel = nullptr;
    double viewStartSample = 0.0;
    double zoomFactor = 1.0;
    int draggedClipIndex = -1;
    double dragOffsetSamples = 0.0;
    int selectedTrack = -1;
    int selectedClip = -1;
    bool trimmingLeft = false;
    bool trimmingRight = false;
};

class TimelineRuler final : public juce::Component
{
public:
    explicit TimelineRuler(TrackDataModel* model = nullptr) : trackModel(model) {}
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }
private:
    TrackDataModel* trackModel = nullptr;
    bool dragging = false;
    double dragStart = 0.0;
};

class ArrangeWindow final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit ArrangeWindow(TrackDataModel* model = nullptr);

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    juce::ThreadPool loader { 2 };
    TrackHeaderPanel trackHeaderPanel;
    TimelineRuler timelineRuler;
    TimelineGrid timelineGrid;
    juce::Slider horizontalZoomSlider;
    juce::Slider trackHeightSlider;
    TrackDataModel* trackModel = nullptr;
    bool isDraggingOver = false;
    int draggedTrack = 0;
};
