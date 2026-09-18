#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>

#include "UI/MainComponent.h"
#include "Project/RecentProjectsStore.h"
#include "Project/ProjectTemplateStore.h"
#include "Project/ProjectAlternativeStore.h"
#include "Project/AutosaveService.h"

class MainWindow final : public juce::DocumentWindow,
                         public juce::MenuBarModel,
                         private juce::OpenGLRenderer
{
public:
    explicit MainWindow(const juce::String& name);
    ~MainWindow() override;

    void closeButtonPressed() override;
    bool keyPressed(const juce::KeyPress& key) override;
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    void renderOpenGL() override;
    void newOpenGLContextCreated() override {}
    void openGLContextClosing() override {}

private:
    enum MenuItem
    {
        newProject = 1,
        newAudioRecordingTemplate,
        newMidiProductionTemplate,
        saveProjectTemplate,
        newProjectAlternative,
        openProject,
        closeProject,
        saveProject,
        saveProjectAs,
        saveProjectCopy,
        importAudio,
        importMidi,
        bounceProject,
        deviceSettings,
        projectSettings,
        quitApplication,
        undoEdit,
        redoEdit,
        addTrack,
        compactTracks,
        normalTracks,
        toggleInspector,
        toggleBrowser,
        toggleMixer,
        recentProjectBase = 1000,
        userTemplateBase = 1100
        , alternativeBase = 1200
    };

    void chooseProjectToOpen();
    void chooseProjectToSave();
    void chooseProjectCopyToSave();
    void chooseProjectTemplateToSave();
    void chooseProjectAlternativeToSave();
    void chooseAudioToImport();
    void chooseMidiToImport();
    void createProjectFromTemplate(ProjectTemplate projectTemplate);
    void createProjectFromUserTemplate(const juce::File& file);
    void loadProjectAlternative(const juce::File& file);
    void closeCurrentProject();
    void showDeviceSettings();
    void showProjectSettings();
    void saveCurrentProject();
    void loadProjectFromFile(const juce::File& file);
    void refreshRecentProjects();
    void confirmDiscardChanges(std::function<void()> continuation);
    void showProjectError(const juce::String& title, const juce::Result& result) const;

    std::unique_ptr<juce::OpenGLContext> openGLContext;
    std::unique_ptr<juce::FileChooser> projectFileChooser;
    std::unique_ptr<juce::FileChooser> audioFileChooser;
    std::unique_ptr<juce::FileChooser> midiFileChooser;
    MainComponent* mainComponent = nullptr;
    juce::File currentProjectFile;
    juce::File projectRootFile;
    RecentProjectsStore recentProjects;
    ProjectTemplateStore projectTemplates;
    ProjectAlternativeStore projectAlternatives;
    std::unique_ptr<AutosaveService> autosaveService;
};
