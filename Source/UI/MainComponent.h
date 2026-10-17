#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Models/TrackDataModel.h"
#include "../AudioEngine/AudioEngine.h"
#include "ArrangeWindow.h"
#include "AudioPeakMeter.h"
#include "ControlBar.h"
#include "MixerPane.h"
#include "PianoRoll.h"
#include "InspectorPane.h"
#include "AssetBrowserPane.h"
#include "StartupWorkflowPane.h"
#include "Theme/StudioForgeLookAndFeel.h"
#include "../Plugins/PluginHostService.h"
#include "../Project/ProjectTemplates.h"
#include "../Services/TuneAiAnalysisService.h"
#include "../Control/StudioForgeControlBridge.h"
#include "../Control/PerformanceMacroService.h"
#include "../Control/PerformanceSurfaceBridge.h"
#include "../Control/PitchCorrectionIntegrationService.h"
#include "../Core/WorkspaceModeController.h"

class MainComponent final : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool handleGlobalKeyPress(const juce::KeyPress& key);

    juce::Result saveProject(const juce::File& file);
    juce::Result saveProjectCopy(const juce::File& file) const;
    juce::Result saveProjectTemplate(const juce::File& file) const;
    juce::Result loadProject(const juce::File& file, juce::StringArray* missingMediaReferences = nullptr);
    juce::Result createNewProject();
    juce::Result createProjectFromTemplate(ProjectTemplate projectTemplate);
    bool isProjectDirty() const noexcept;
    void markProjectSaved() noexcept;
    void markProjectRecovered() noexcept;
    bool undoEdit();
    bool redoEdit();
    void requestRecordToggle();
    void setInspectorPanelVisible(bool visible);
    void setBrowserPanelVisible(bool visible);
    void setMixerPanelVisible(bool visible);
    bool addTrackFromCommand(TrackType type = TrackType::audio);
    void importAudioFile(const juce::File& file);
    juce::Result importMidiFile(const juce::File& file);
    void requestProjectBounce();
    void requestProjectExport();
    void requestStemExport();
    void requestSelectedTrackExport();
    void requestMidiExport();
    void requestTuneAiUrlAnalysis();
    void setWorkspaceTrackHeight(int height);
    // Deterministic support entry point. It is deliberately invoked only by
    // the explicit command-line switch, never by ordinary launch.
    void startAutomatedVibeTrace();

    bool isInspectorPanelVisible() const noexcept { return panelLayout.inspector; }
    bool isBrowserPanelVisible() const noexcept { return panelLayout.browser; }
    bool isMixerPanelVisible() const noexcept { return panelLayout.mixer; }
    bool isStartupWorkflowVisible() const noexcept { return startupWorkflow.isVisible(); }
    void showWorkspace() noexcept { startupWorkflow.setVisible(false); }
    void showInitialTrackCreation();
    void setRecentProjects(juce::StringArray paths) { startupWorkflow.setRecentProjects(std::move(paths)); }
    std::function<void()> onOpenProjectRequested;
    std::function<void(const juce::File&)> onOpenRecentProjectRequested;
    std::function<void()> onAudioDeviceSettingsRequested;

    TrackDataModel& getTrackDataModel() noexcept { return trackDataModel; }
    AudioEngine& getAudioEngine() noexcept { return audioEngine; }
    PerformanceMacroService& getPerformanceMacros() noexcept { return performanceMacros; }

private:
    void selectTrack(int trackIndex);
    void applyWorkspaceMode(const WorkspaceProfile& profile);
    int getSelectedTrackIndex() const noexcept;
    void configureTrackCreationDialog();
    void enableAvailablePerformanceInputs();
    void repairAudioTrackInputAssignments();
    void selectAudioInputChannel(int oneBasedChannel);
    void selectAudioOutputChannel(int oneBasedChannel);
    void handleSpaceTransport();
    void toggleRecording();
    void startRecordingNow(int trackIndex, const juce::File& destination,
                           double timelineStartSample = -1.0);
    void startAudioRecordingsNow(const std::vector<AudioEngine::AudioRecordingTarget>& targets,
                                 double timelineStartSample = -1.0);
    void showRecordingFailure(const juce::Result& result) const;
    juce::String applyTuneAiKey(const MusicalKey&, bool mic1, bool mic2);
    void consumeTuneAiKeyBridge();
    void beginBeatKeyLearning(int trackIndex);
    void requestWorkspaceMode(WorkspaceMode mode);
    struct PanelLayoutState
    {
        bool inspector = false;
        bool browser = false;
        bool mixer = false;
        bool pianoRoll = false;
    };
    void applyPanelLayout(PanelLayoutState next, bool updateStudioSnapshot);
    void setWorkspacePanelsVisible(bool inspector, bool browser, bool mixer, bool pianoRoll);
    void setPianoRollPanelVisible(bool visible);
    void timerCallback() override;
    juce::File createRecordingDestination() const;
    void requestOfflineRender(const juce::String& chooserTitle, const juce::String& errorTitle);

    StudioForgeLookAndFeel lookAndFeel;
    TrackDataModel trackDataModel;
    AudioEngine audioEngine { &trackDataModel };
    StudioForgeControlBridge controlBridge { trackDataModel };
    PerformanceMacroService performanceMacros { trackDataModel };
    PerformanceSurfaceBridge performanceSurfaceBridge { trackDataModel, performanceMacros };
    PluginHostService pluginHost;
    PitchCorrectionIntegrationService pitchCorrection { trackDataModel, pluginHost, controlBridge };
    TuneAiAnalysisService tuneAiAnalysis;
    ControlBar controlBar { &trackDataModel, &audioEngine };
    ArrangeWindow arrangeWindow { &trackDataModel, &audioEngine };
    InspectorPane inspectorPane { trackDataModel, audioEngine, pluginHost };
    AssetBrowserPane assetBrowser;
    MixerPane mixerPane { &trackDataModel, &audioEngine, &pluginHost };
    PianoRoll pianoRoll { &trackDataModel };
    StartupWorkflowPane startupWorkflow;
    std::unique_ptr<juce::FileChooser> bounceFileChooser;
    uint64_t savedProjectRevision = 0;
    bool recoveredProjectNeedsSave = false;
    bool countInEnabled = false;
    int countInBars = 1;
    bool recordingCountdownActive = false;
    // A startup choice is a track-source preference, never an instruction to
    // disable the remaining physical inputs of the Core Audio device.
    int preferredStartupInputChannel = 0;
    int pendingRecordingTrack = -1;
    double pendingRecordingStartSample = 0.0;
    juce::File pendingRecordingDestination;
    std::vector<AudioEngine::AudioRecordingTarget> pendingAudioRecordingTargets;
    double recordingPunchStopSample = -1.0;
    TrackId selectedTrackId;
    bool spaceStopPendingReturn = false;
    enum class AutoKeyState : uint8_t { idle, learning, locked };
    AutoKeyState autoKeyState = AutoKeyState::idle;
    uint64_t appliedLiveKeyRevision = 0;
    double consumedTuneAiKeyPublishedAt = 0.0;
    PitchCorrectionIntegrationService::Status pitchCorrectionStatus;
    int pitchCorrectionSetupTicks = 0;
    int midiDeviceRefreshTicks = 0;
    WorkspaceMode workspaceMode = WorkspaceMode::studio;
    std::unique_ptr<WorkspaceModeController> workspaceModeController;
    // This is the single source of truth for dock visibility. Components are
    // projections of this value; reading their incidental visible flags while
    // workspace transitions are in progress caused stale View-menu ticks.
    PanelLayoutState panelLayout { true, false, false, false };
    PanelLayoutState studioPanelLayout { true, false, false, false };
    bool suppressWorkspaceModeCallback = false;
    bool automatedVibeTraceStarted = false;
    int studioTrackHeight = 72;
};
