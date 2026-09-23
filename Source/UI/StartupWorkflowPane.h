#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Project/ProjectTemplates.h"
#include "PerformanceRoomModel.h"

// Owns only the startup chooser and the Performance Room modal. Track
// creation happens by applying a LiveSetupConfig, never through UI counters.
class StartupWorkflowPane final : public juce::Component, private juce::Timer
{
public:
    enum class Step { splash, chooseProject, performanceRoom };

    StartupWorkflowPane();

    std::function<void(ProjectTemplate)> onTemplateSelected;
    std::function<void()> onOpenProject;
    std::function<void(const juce::File&)> onOpenRecentProject;
    std::function<void(const LiveSetupConfig&)> onPerformanceConfigured;
    std::function<void()> onAudioDeviceSettingsRequested;
    std::function<void(int)> onAudioInputChannelSelected;
    std::function<void(int)> onAudioOutputChannelSelected;

    void showTrackCreation(bool returnToWorkspaceOnCancel = false);
    void setRecentProjects(juce::StringArray recentProjectPaths);
    void setAudioDeviceChannels(int inputChannels, int outputChannels);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    enum class ProjectPage { newProject, history };

    void timerCallback() override;
    void dismissToWorkspace();
    void showProjectPage(ProjectPage page);
    void showPerformancePreset(PerformancePreset preset);
    void synchronisePerformanceControls();
    void beginFadeIn();
    juce::Rectangle<int> getDialogBounds() const;

    Step step = Step::chooseProject;
    bool returnToWorkspaceOnCancel = false;
    ProjectPage projectPage = ProjectPage::newProject;
    int audioInputChannels = 0;
    int audioOutputChannels = 0;
    int splashFramesRemaining = 27;
    float currentAlpha = 0.0f;
    PerformanceRoomModel performanceRoom;

    juce::Label title;
    juce::TextButton newProjectNavigation { "New Project" };
    juce::TextButton historyNavigation { "History" };
    juce::TextButton newProjectCard;
    juce::TextButton openProject { "Open an existing project..." };
    std::array<juce::TextButton, 6> recentProjectButtons;
    juce::StringArray recentProjects;

    std::array<juce::TextButton, 4> performancePresets {
        juce::TextButton { "Solo Vocal" }, juce::TextButton { "Duet" },
        juce::TextButton { "Vocal + Band" }, juce::TextButton { "Custom" }
    };
    std::array<juce::Label, PerformanceRoomModel::memberCount> setupLabels;
    std::array<juce::Label, PerformanceRoomModel::memberCount> setupQuantities;
    std::array<juce::TextButton, PerformanceRoomModel::memberCount> setupMinus;
    std::array<juce::TextButton, PerformanceRoomModel::memberCount> setupPlus;
    juce::ComboBox audioInputSelector;
    juce::ComboBox audioOutputSelector;
    juce::TextButton configureAudioDevice { "Configure audio device..." };
    juce::TextButton enterPerformance { "Enter Performance" };
    juce::TextButton cancel { "Cancel" };
    juce::Label progress { {}, "Initializing audio device and project services..." };
};
