#pragma once

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
#include "PerformanceFooter.h"
#include "StartupWorkflowPane.h"
#include "Theme/StudioForgeLookAndFeel.h"
#include "../Plugins/PluginHostService.h"
#include "../Project/ProjectTemplates.h"

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
    void setInspectorPanelVisible(bool visible);
    void setBrowserPanelVisible(bool visible);
    void setMixerPanelVisible(bool visible);
    bool addTrackFromCommand(TrackType type = TrackType::audio);
    void importAudioFile(const juce::File& file);
    juce::Result importMidiFile(const juce::File& file);
    void requestProjectBounce();
    void setWorkspaceTrackHeight(int height);
    bool isInspectorPanelVisible() const noexcept { return inspectorPane.isVisible(); }
    bool isBrowserPanelVisible() const noexcept { return assetBrowser.isVisible(); }
    bool isMixerPanelVisible() const noexcept { return mixerPane.isVisible(); }
    void showWorkspace() noexcept { startupWorkflow.setVisible(false); }
    void showInitialTrackCreation();
    void setRecentProjects(juce::StringArray paths) { startupWorkflow.setRecentProjects(std::move(paths)); }
    std::function<void()> onOpenProjectRequested;
    std::function<void(const juce::File&)> onOpenRecentProjectRequested;

    TrackDataModel& getTrackDataModel() noexcept { return trackDataModel; }
    AudioEngine& getAudioEngine() noexcept { return audioEngine; }

private:
    void selectTrack(int trackIndex);
    int getSelectedTrackIndex() const noexcept;
    void configureTrackCreationDialog();
    void toggleRecording();
    void startRecordingNow(int trackIndex, const juce::File& destination,
                           double timelineStartSample = -1.0);
    void timerCallback() override;
    juce::File createRecordingDestination() const;

    StudioForgeLookAndFeel lookAndFeel;
    TrackDataModel trackDataModel;
    AudioEngine audioEngine { &trackDataModel };
    PluginHostService pluginHost;
    ControlBar controlBar { &trackDataModel, &audioEngine };
    ArrangeWindow arrangeWindow { &trackDataModel };
    InspectorPane inspectorPane { trackDataModel, audioEngine, pluginHost };
    AssetBrowserPane assetBrowser;
    PerformanceFooter performanceFooter { &audioEngine };
    MixerPane mixerPane { &trackDataModel, &audioEngine, &pluginHost };
    PianoRoll pianoRoll { &trackDataModel };
    StartupWorkflowPane startupWorkflow;
    std::unique_ptr<juce::FileChooser> bounceFileChooser;
    uint64_t savedProjectRevision = 0;
    bool recoveredProjectNeedsSave = false;
    bool countInEnabled = false;
    bool recordingCountdownActive = false;
    int pendingRecordingTrack = -1;
    double pendingRecordingStartSample = 0.0;
    juce::File pendingRecordingDestination;
    double recordingPunchStopSample = -1.0;
    TrackId selectedTrackId;
};
