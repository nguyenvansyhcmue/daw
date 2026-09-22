#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <map>
#include <vector>

#include "../Models/TrackDataModel.h"
#include "../Media/WaveformThumbnailCache.h"

class AudioEngine;

class TrackHeaderPanel final : public juce::Component, private juce::Timer
{
public:
    explicit TrackHeaderPanel(TrackDataModel* model = nullptr, AudioEngine* engine = nullptr);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    std::function<void(int)> onTrackSelected;
    void setSelectedTrack(int track) noexcept { selectedTrack = track; repaint(); }

    void setTrackModel(TrackDataModel* model) noexcept { trackModel = model; }

private:
    void timerCallback() override;

    TrackDataModel* trackModel = nullptr;
    AudioEngine* audioEngine = nullptr;
    int selectedTrack = 0;
    std::array<juce::Slider, TrackDataModel::maxTracks> volumeControls;
    std::array<juce::Slider, TrackDataModel::maxTracks> panControls;
    size_t laidOutTrackCount = 0;
    int laidOutTrackHeight = -1;
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
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    std::function<void(int)> onTrackSelected;
    std::function<void(ClipId)> onAudioClipSelected;
    std::function<void(MidiClipId)> onMidiClipSelected;

    void setSelectedTrack(int track) noexcept { selectedTrack = track; selectedClip = -1; repaint(); }
    void setViewStartSample(double sample) noexcept;
    void setDisplayPlayheadSample(double sample) noexcept { displayPlayheadSample = sample; repaint(); }
    double getViewStartSample() const noexcept { return viewStartSample; }
    void resized() override {}

    void setTrackModel(TrackDataModel* model) noexcept { trackModel = model; }
    void setWaveformCache(const WaveformThumbnailCache* cache) noexcept { waveformCache = cache; }
    std::function<void(double, float)> onViewChanged;

private:
    void timerCallback() override;

    TrackDataModel* trackModel = nullptr;
    const WaveformThumbnailCache* waveformCache = nullptr;
    double viewStartSample = 0.0;
    double displayPlayheadSample = 0.0;
    double zoomFactor = 1.0;
    int selectedTrack = -1;
    int selectedClip = -1;
    ClipId selectedClipId;
    double clipDragStartSample = 0.0;
    double clipDragGrabOffsetSamples = 0.0;
    double clipDragPreviewSample = 0.0;
    int clipDragPreviewTrack = -1;
    bool draggingClip = false;
    bool trimmingLeft = false;
    bool trimmingRight = false;
    bool adjustingFadeIn = false;
    bool adjustingFadeOut = false;
    bool panningTimeline = false;
    float panDragStartX = 0.0f;
    double panDragStartSample = 0.0;

    double sampleAt(const juce::Point<float>& position) const noexcept;
    int trackAt(const juce::Point<float>& position) const noexcept;
    void showContextMenu(const juce::MouseEvent& event, int track, ClipId clip, double sampleAtMouse);
    void updateView(double startSample, float zoom);
    TrackId createTrackAfter(TrackType type, int anchorTrack);
};

class TimelineRuler final : public juce::Component
{
public:
    explicit TimelineRuler(TrackDataModel* model = nullptr);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent&) override;
    void setViewStartSample(double sample) noexcept { viewStartSample = juce::jmax(0.0, sample); repaint(); }
    void setDisplayPlayheadSample(double sample) noexcept { displayPlayheadSample = sample; repaint(); }
    void setReservedTrailingWidth(int width) noexcept { reservedTrailingWidth = juce::jmax(0, width); repaint(); }
private:
    enum class DragMode { none, scrubPlayhead, selectCycle };

    double sampleAt(const juce::Point<float>& position) const noexcept;
    double snappedSampleAt(const juce::Point<float>& position) const noexcept;

    TrackDataModel* trackModel = nullptr;
    DragMode dragMode = DragMode::none;
    double dragAnchorSample = 0.0;
    double viewStartSample = 0.0;
    double displayPlayheadSample = 0.0;
    int reservedTrailingWidth = 0;
};

class ArrangeWindow final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit ArrangeWindow(TrackDataModel* model = nullptr, AudioEngine* engine = nullptr);

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
    void timerCallback() override;
    struct PendingAudioImport
    {
        TrackId trackId;
        double startSample = 0.0;
    };

    void completeAudioImport(const juce::File& sourceFile,
                             std::shared_ptr<juce::AudioBuffer<float>> decodedBuffer);
    int resolveAudioImportTrack(int requestedTrackIndex);
    void setTimelineView(double startSample, float zoom);

    WaveformThumbnailCache waveformCache;
    juce::ThreadPool loader { 2 };
    TrackHeaderPanel trackHeaderPanel;
    TimelineRuler timelineRuler;
    TimelineGrid timelineGrid;
    double lastDisplayPlayheadSample = -1.0;
    juce::Slider horizontalZoomSlider;
    juce::Slider trackHeightSlider;
    juce::Label zoomReadout;
    juce::Label trackHeightReadout;
    TrackDataModel* trackModel = nullptr;
    bool isDraggingOver = false;
    int draggedTrack = 0;
    std::map<juce::String, std::vector<PendingAudioImport>> pendingImportsBySource;
};
