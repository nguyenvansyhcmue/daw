#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Project/ProjectState.h"
#include "../Project/ProjectTemplates.h"

class StartupWorkflowPane final : public juce::Component, private juce::Timer
{
public:
    enum class Step { splash, chooseProject, createTrack };
    StartupWorkflowPane();
    std::function<void(ProjectTemplate)> onTemplateSelected;
    std::function<void()> onOpenProject;
    std::function<void(const juce::File&)> onOpenRecentProject;
    // Returns the number of tracks that were actually created. This keeps the
    // dialog open if a model-level capacity check rejects the request.
    std::function<int(TrackType, int)> onCreateTracks;
    // When invoked from the workspace, Cancel returns to that workspace rather
    // than incorrectly sending an existing project back to the project chooser.
    void showTrackCreation(bool returnToWorkspaceOnCancel = false);
    void setRecentProjects(juce::StringArray recentProjectPaths);
    void setAvailableTrackSlots(int slots);
    void setAudioDeviceChannels(int inputChannels, int outputChannels);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void setType(TrackType);
    void updateTrackCountDisplay();
    void beginFadeIn();
    juce::Rectangle<int> getDialogBounds() const;
    Step step = Step::chooseProject;
    bool returnToWorkspaceOnCancel = false;
    int availableTrackSlots = 14;
    int audioInputChannels = 0;
    int audioOutputChannels = 0;
    int splashFramesRemaining = 27;
    float currentAlpha = 0.0f;
    TrackType selectedType = TrackType::instrument;
    ProjectTemplate selectedProjectTemplate = ProjectTemplate::empty;
    juce::Label title;
    juce::TextButton emptyProject { "Empty Project" }, audioRecordingProject { "Audio Recording" }, midiProductionProject { "MIDI Production" }, openProject { "Open an existing project..." };
    juce::TextButton chooseProject { "Choose" };
    std::array<juce::TextButton, 5> recentProjectButtons;
    juce::StringArray recentProjects;
    // Two creation families are supported by the current engine.  The option
    // buttons express the concrete route inside each family without inventing
    // Pattern or Session Player track types that have no backend yet.
    juce::TextButton midi { "MIDI" }, audio { "Audio" };
    juce::TextButton softwareInstrument { "Software Instrument" }, externalMidi { "External MIDI" };
    juce::TextButton micOrLine { "Mic or Line" }, guitarOrBass { "Guitar or Bass" };
    bool guitarInputSelected = false;
    juce::Slider count;
    juce::TextButton decreaseTrackCount { "-" }, trackCountDisplay { "1" }, increaseTrackCount { "+" };
    juce::TextButton create { "Create" }, cancel { "Cancel" };
    juce::Label progress { {}, "Initializing audio device and project services…" };
};
