#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <vector>

#include "../Models/TrackDataModel.h"
#include "../Media/WaveformThumbnailCache.h"

class TrackHeaderPanel final : public juce::Component, private juce::Timer
{
public:
    explicit TrackHeaderPanel(TrackDataModel* model = nullptr);

    void paint(juce::Graphics& g) override;
    void resized() override {}
    void mouseDown(const juce::MouseEvent&) override;
    std::function<void(int)> onTrackSelected;
    void setSelectedTrack(int track) noexcept { selectedTrack = track; repaint(); }

    void setTrackModel(TrackDataModel* model) noexcept { trackModel = model; }

private:
    void timerCallback() override;

    TrackDataModel* trackModel = nullptr;
    int selectedTrack = 0;
};

class TimelineGrid final : public juce::Component, private juce::Timer
{
public:
    explicit TimelineGrid(TrackDataModel* model = nullptr);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    std::function<void(int)> onTrackSelected;
    std::function<void(ClipId)> onAudioClipSelected;
    std::function<void(MidiClipId)> onMidiClipSelected;

    void setSelectedTrack(int track) noexcept { selectedTrack = track; selectedClip = -1; repaint(); }
    void resized() override {}

    void setTrackModel(TrackDataModel* model) noexcept { trackModel = model; }
    void setWaveformCache(const WaveformThumbnailCache* cache) noexcept { waveformCache = cache; }

private:
    void timerCallback() override;

    TrackDataModel* trackModel = nullptr;
    const WaveformThumbnailCache* waveformCache = nullptr;
    double viewStartSample = 0.0;
    double zoomFactor = 1.0;
    int selectedTrack = -1;
    int selectedClip = -1;
    ClipId selectedClipId;
    double clipDragStartSample = 0.0;
    double clipDragPreviewSample = 0.0;
    int clipDragPreviewTrack = -1;
    bool draggingClip = false;
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

    std::function<void(int)> onTrackSelected;
    std::function<void(ClipId)> onAudioClipSelected;
    std::function<void(MidiClipId)> onMidiClipSelected;

    // Selection ownership stays above this view; this method only synchronises
    // its two visual subviews with the resolved display index.
    void setSelectedTrack(int trackIndex) noexcept;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void importAudioFile(const juce::File& file, int trackIndex, double startSample);
    void requestWaveformPreparation();

private:
    struct PendingAudioImport
    {
        TrackId trackId;
        double startSample = 0.0;
    };

    void completeAudioImport(const juce::File& sourceFile,
                             std::shared_ptr<juce::AudioBuffer<float>> decodedBuffer);
    int resolveAudioImportTrack(int requestedTrackIndex);

    WaveformThumbnailCache waveformCache;
    juce::ThreadPool loader { 2 };
    TrackHeaderPanel trackHeaderPanel;
    TimelineRuler timelineRuler;
    TimelineGrid timelineGrid;
    juce::Slider horizontalZoomSlider;
    juce::Slider trackHeightSlider;
    TrackDataModel* trackModel = nullptr;
    bool isDraggingOver = false;
    int draggedTrack = 0;
    std::map<juce::String, std::vector<PendingAudioImport>> pendingImportsBySource;
};
