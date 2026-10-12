#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <map>
#include <set>
#include <vector>

#include "../Models/TrackDataModel.h"
#include "../Media/Import/AudioImportSupport.h"
#include "../Media/WaveformThumbnailCache.h"
#include "AudioRegionInteraction.h"
#include "ClipInteraction/ArrangementSelectionModel.h"
#include "TimelineNavigation/TimelineViewportController.h"
#include "TimelineNavigation/TimelineZoomPresetStore.h"

class AudioEngine;

class TrackHeaderPanel final : public juce::Component, private juce::Timer
{
public:
    explicit TrackHeaderPanel(TrackDataModel* model = nullptr, AudioEngine* engine = nullptr);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void setVerticalScrollOffset(int offset) noexcept { verticalScrollOffset = juce::jmax(0, offset); resized(); repaint(); }
    std::function<void(int)> onTrackSelected;
    void setSelectedTrack(int track) noexcept { selectedTrack = track; repaint(); }

    void setTrackModel(TrackDataModel* model) noexcept { trackModel = model; }
    void setAudioFileDropTarget(int trackIndex) noexcept
    {
        if (audioFileDropTarget == trackIndex) return;
        audioFileDropTarget = trackIndex;
        repaint();
    }

private:
    void timerCallback() override;

    TrackDataModel* trackModel = nullptr;
    AudioEngine* audioEngine = nullptr;
    int selectedTrack = 0;
    std::array<juce::Slider, TrackDataModel::maxTracks> volumeControls;
    std::array<juce::Slider, TrackDataModel::maxTracks> panControls;
    size_t laidOutTrackCount = 0;
    int laidOutTrackHeight = -1;
    int pressedTrack = -1;
    int reorderPreviewTrack = -1;
    int dragStartTrackHeight = 0;
    float dragStartY = 0.0f;
    bool draggingTrack = false;
    bool resizingTrackHeight = false;
    int verticalScrollOffset = 0;
    int audioFileDropTarget = -1;
};

class TimelineGrid final : public juce::Component, private juce::Timer
{
public:
    explicit TimelineGrid(TrackDataModel* model = nullptr);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override;
    bool keyPressed(const juce::KeyPress& key) override;
    std::function<void(int)> onTrackSelected;
    std::function<void(ClipId)> onAudioClipSelected;
    std::function<void(ClipId)> onAudioClipOpenRequested;
    std::function<void(MidiClipId)> onMidiClipSelected;
    std::function<void(MidiClipId)> onMidiClipOpenRequested;

    void setSelectedTrack(int track) noexcept
    {
        // Track selection is echoed through ArrangeWindow/MainComponent while
        // a region press is in progress.  Do not clear the region selection
        // for that same-track notification: doing so turns a locked trim into
        // a subsequent MOVE gesture in mouseDrag().
        if (selectedTrack != track)
        {
            selectedTrack = track;
            selectedClip = -1;
            selectedClipId = {};
            selectedMidiClipId = {};
        }
        repaint();
    }
    void setViewStartSample(double sample) noexcept;
    void setVerticalScrollOffset(int offset) noexcept { verticalScrollOffset = juce::jmax(0, offset); repaint(); }
    void setDisplayPlayheadSample(double sample) noexcept { displayPlayheadSample = sample; repaint(); }
    void setRecordingPreview(int trackIndex, double startSample) noexcept;
    void followPlaybackPosition(double samplePosition);
    double getViewStartSample() const noexcept { return viewStartSample; }
    int getVerticalScrollOffset() const noexcept { return verticalScrollOffset; }
    void resized() override {}

    void setTrackModel(TrackDataModel* model) noexcept { trackModel = model; }
    void setAudioEngine(AudioEngine* engine) noexcept { audioEngine = engine; }
    void setWaveformCache(const WaveformThumbnailCache* cache) noexcept { waveformCache = cache; }
    void setAudioFileDropTarget(int trackIndex) noexcept { audioFileDropTarget = trackIndex; repaint(); }
    std::function<void(double, float)> onViewChanged;
    std::function<void(float)> onWaveformZoomChanged;
    std::function<void(int)> onVerticalScrollChanged;
    void fitAllToWindow();
    void toggleSelectedTrackZoom();
    void setTrackHeight(int height);
    void setWaveformZoom(float zoom);
    void zoomHorizontalBy(float multiplier, float pivotX);
    void zoomVerticalBy(int delta);
    void zoomVerticalByFactor(float multiplier);
    void saveZoomPreset(int slot) noexcept;
    bool recallZoomPreset(int slot);
    bool navigateZoomHistory(bool forward);
    bool isSnapEnabled() const noexcept { return snapEnabled; }
    void setSnapEnabled(bool enabled) noexcept { snapEnabled = enabled; repaint(); }
    // File-drag events are delivered by the OS rather than as mouse drags.
    // Keep its scroll policy here so it matches ordinary clip dragging.
    bool autoScrollForExternalFileDrag(juce::Point<float> localPosition);

private:
    void timerCallback() override;

    TrackDataModel* trackModel = nullptr;
    AudioEngine* audioEngine = nullptr;
    const WaveformThumbnailCache* waveformCache = nullptr;
    TimelineViewportController viewportController;
    TimelineZoomPresetStore zoomPresetStore;
    double viewStartSample = 0.0;
    double displayPlayheadSample = 0.0;
    double zoomFactor = 1.0;
    int selectedTrack = -1;
    int selectedClip = -1;
    ClipId selectedClipId;
    MidiClipId selectedMidiClipId;
    ArrangementSelectionModel selection;
    ClipId hoveredAudioClipId;
    AudioRegionMouseZone hoveredAudioRegionZone = AudioRegionMouseZone::none;
    AudioRegionMouseZone audioRegionZone = AudioRegionMouseZone::none;
    AudioRegionInteractionPhase audioRegionPhase = AudioRegionInteractionPhase::idle;
    juce::Point<float> audioRegionPressPosition;
    double clipDragGrabOffsetSamples = 0.0;
    double clipDragPreviewSample = 0.0;
    int clipDragPreviewTrack = -1;
    double midiClipTrimOriginalDurationSamples = 0.0;
    double midiClipTrimPreviewDurationSamples = 0.0;
    double midiClipTrimOriginalStartSample = 0.0;
    double midiClipTrimPreviewStartSample = 0.0;
    bool trimmingMidiClipLeft = false;
    bool trimmingMidiClipRight = false;
    bool draggingMidiClip = false;
    double midiClipDragGrabOffsetSamples = 0.0;
    double midiClipDragPreviewSample = 0.0;
    int midiClipDragPreviewTrack = -1;
    double clipTrimOriginalStartSample = 0.0;
    double clipTrimOriginalDurationSamples = 0.0;
    double clipTrimMaximumEndSample = 0.0;
    double clipTrimPreviewStartSample = 0.0;
    double clipTrimPreviewDurationSamples = 0.0;
    double clipFadeOriginalInSamples = 0.0;
    double clipFadeOriginalOutSamples = 0.0;
    double clipFadePreviewInSamples = 0.0;
    double clipFadePreviewOutSamples = 0.0;
    bool draggingClip = false;
    bool trimmingLeft = false;
    bool trimmingRight = false;
    bool adjustingFadeIn = false;
    bool adjustingFadeOut = false;
    bool panningTimeline = false;
    bool scrubbingPlayhead = false;
    int recordingPreviewTrack = -1;
    double recordingPreviewStartSample = 0.0;
    std::array<float, 512> recordingEnvelope {};
    size_t recordingEnvelopeWrite = 0;
    size_t recordingEnvelopeCount = 0;
    float panDragStartX = 0.0f;
    double panDragStartSample = 0.0;
    int verticalScrollOffset = 0;
    juce::Point<float> lastDragPosition;
    bool edgeAutoScrollActive = false;
    bool marqueeSelecting = false;
    bool zoomSelecting = false;
    juce::Point<float> zoomSelectionAnchor;
    juce::Point<float> zoomSelectionCurrent;
    bool marqueeSelectionValid = false;
    double marqueeAnchorSample = 0.0;
    double marqueeCurrentSample = 0.0;
    int marqueeAnchorTrack = -1;
    int marqueeCurrentTrack = -1;
    bool selectedTrackZoomed = false;
    int savedTrackHeight = 72;
    int savedVerticalScrollOffset = 0;
    double savedViewStartSample = 0.0;
    float savedHorizontalZoom = 1.0f;
    bool snapEnabled = false;
    float waveformZoom = 1.0f;
    int audioFileDropTarget = -1;

    double sampleAt(const juce::Point<float>& position) const noexcept;
    int trackAt(const juce::Point<float>& position) const noexcept;
    void showContextMenu(const juce::MouseEvent& event, int track, ClipId clip, double sampleAtMouse);
    void updateView(double startSample, float zoom);
    void updateVerticalScroll(int offset);
    void updateClipDragPreview(const juce::Point<float>& position, const juce::ModifierKeys& modifiers);
    void cancelAudioRegionInteraction() noexcept;
    void updateAudioRegionHover(ClipId clipId, AudioRegionMouseZone zone);
    void updateEdgeAutoScroll();
    double editSample(double rawSample, const juce::ModifierKeys& modifiers) const noexcept;
    TrackId createTrackAfter(TrackType type, int anchorTrack);
};

class TimelineRuler final : public juce::Component, private juce::ChangeListener
{
public:
    explicit TimelineRuler(TrackDataModel* model = nullptr);
    ~TimelineRuler() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent&) override;
    void setViewStartSample(double sample) noexcept { viewStartSample = juce::jmax(0.0, sample); repaint(); }
    void setDisplayPlayheadSample(double sample) noexcept { displayPlayheadSample = sample; repaint(); }
    void setReservedTrailingWidth(int width) noexcept { reservedTrailingWidth = juce::jmax(0, width); repaint(); }
private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }
    enum class DragMode { none, scrubPlayhead, createCycle, moveCycle, resizeCycleStart, resizeCycleEnd };

    double sampleAt(const juce::Point<float>& position) const noexcept;
    double snappedSampleAt(const juce::Point<float>& position) const noexcept;
    void beginCycleEdit(double clickedSample, float pointerX);
    void updateCycleEdit(double currentSample, float pointerX);
    void finishCycleEdit();
    void createDefaultCycle(double clickedSample);

    TrackDataModel* trackModel = nullptr;
    DragMode dragMode = DragMode::none;
    double dragAnchorSample = 0.0;
    double originalCycleStartSample = 0.0;
    double originalCycleEndSample = 0.0;
    bool cycleClickPendingToggle = false;
    bool loopLocatorHovered = false;
    bool loopFocused = false;
    float cycleMouseDownX = 0.0f;
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
    std::function<void(ClipId)> onAudioClipOpenRequested;
    std::function<void(MidiClipId)> onMidiClipSelected;
    std::function<void(MidiClipId)> onMidiClipOpenRequested;
    std::function<void()> onPerformanceSetupRequested;
    // Announces the resolved destination immediately. The owner can arm a
    // first-pass key analysis without coupling import code to control logic.
    std::function<void(int, const juce::File&)> onAudioFileImported;

    // Selection ownership stays above this view; this method only synchronises
    // its two visual subviews with the resolved display index.
    void setSelectedTrack(int trackIndex) noexcept;
    void showRecordingPreview(int trackIndex, double startSample) noexcept;
    void hideRecordingPreview() noexcept;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void importAudioFile(const juce::File& file, int trackIndex, double startSample);
    void requestWaveformPreparation();
    void fitTracksToWindow() { timelineGrid.fitAllToWindow(); }
    void toggleSelectedTrackZoom() { timelineGrid.toggleSelectedTrackZoom(); }
    void addMarkerAtPlayhead();

private:
    void timerCallback() override;
    struct PendingAudioImport
    {
        ClipId clipId;
    };

    void completeAudioImport(const juce::File& sourceFile,
                             std::shared_ptr<juce::AudioBuffer<float>> decodedBuffer);
    void importAudioFileAt(const juce::File& file, int trackIndex, double startSample, int insertionIndex);
    void importDroppedAudioFiles(const juce::Array<juce::File>& files, int trackIndex,
                                 double startSample, int insertionIndex);
    void showMultipleAudioImportChoices(juce::Array<juce::File> files, int trackIndex,
                                        double startSample, int insertionIndex);
    void importEachFileOnNewTrack(const juce::Array<juce::File>& files, double startSample,
                                  int insertionIndex);
    int resolveAudioImportTrack(int requestedTrackIndex, int insertionIndex = -1);
    void clearFileDropPreview() noexcept;
    void queueWaveformPreparation(const AudioClipState& clip);
    void setTimelineView(double startSample, float zoom);
    void updateFileDropTarget(int x, int y);

    WaveformThumbnailCache waveformCache;
    juce::ThreadPool loader { 2 };
    TrackHeaderPanel trackHeaderPanel;
    TimelineRuler timelineRuler;
    TimelineGrid timelineGrid;
    double lastDisplayPlayheadSample = -1.0;
    juce::Slider horizontalZoomSlider;
    juce::Slider trackHeightSlider;
    juce::Slider waveformZoomSlider;
    juce::Label zoomReadout;
    juce::Label trackHeightReadout;
    juce::Label waveformZoomReadout;
    juce::TextButton addPerformanceButton { "+" };
    juce::TextButton snapButton { "SNAP OFF" };
    juce::TextButton markerButton { "+ MARKER" };
    TrackDataModel* trackModel = nullptr;
    bool isDraggingOver = false;
    int draggedTrack = -1;
    double draggedSample = 0.0;
    bool dropCreatesAudioTrack = false;
    int dropInsertionIndex = -1;
    juce::Array<juce::File> draggedAudioFiles;
    AudioImportSupport::AudioFileMetadata draggedAudioMetadata;
    juce::Point<float> fileDragPosition;
    std::map<juce::String, std::vector<PendingAudioImport>> pendingImportsBySource;
    std::set<juce::String> pendingWaveformPreparations;
    uint64_t waveformViewportGeneration = 0;
};
