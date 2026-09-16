#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>

#include "UI/MainComponent.h"
#include "Project/RecentProjectsStore.h"

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
        openProject,
        saveProject,
        saveProjectAs,
        quitApplication,
        undoEdit,
        redoEdit,
        addTrack,
        compactTracks,
        normalTracks,
        toggleInspector,
        toggleBrowser,
        toggleMixer
    };

    void chooseProjectToOpen();
    void chooseProjectToSave();
    void saveCurrentProject();
    void loadProjectFromFile(const juce::File& file);
    void refreshRecentProjects();
    void confirmDiscardChanges(std::function<void()> continuation);
    void showProjectError(const juce::String& title, const juce::Result& result) const;

    std::unique_ptr<juce::OpenGLContext> openGLContext;
    std::unique_ptr<juce::FileChooser> projectFileChooser;
    MainComponent* mainComponent = nullptr;
    juce::File currentProjectFile;
    RecentProjectsStore recentProjects;
};
