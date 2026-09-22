#include "MainWindow.h"
#include "UI/DeviceSettingsPanel.h"
#include "UI/ProjectSettingsPanel.h"
#include "UI/StudioForgeDialog.h"

MainWindow::MainWindow(const juce::String& name)
    : juce::DocumentWindow(name,
          juce::Colour(0xff1d1f21),
          juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(false);
    setTitleBarHeight(28);
    setColour(juce::DocumentWindow::backgroundColourId, juce::Colour(0xff1d1f21));
    setColour(juce::DocumentWindow::textColourId, juce::Colours::white.withAlpha(0.92f));
    setResizable(true, true);
    setResizeLimits(1200, 720, 3000, 1800);
    setContentOwned(new MainComponent(), true);

    mainComponent = dynamic_cast<MainComponent*>(getContentComponent());
    if (mainComponent != nullptr)
    {
        mainComponent->onOpenProjectRequested = [this] { chooseProjectToOpen(); };
        mainComponent->onAudioDeviceSettingsRequested = [this] { showDeviceSettings(); };
        mainComponent->onOpenRecentProjectRequested = [this](const juce::File& file)
        {
            confirmDiscardChanges([safeWindow = juce::Component::SafePointer<MainWindow>(this), file]
            {
                if (safeWindow != nullptr) safeWindow->loadProjectFromFile(file);
            });
        };
        refreshRecentProjects();
        autosaveService = std::make_unique<AutosaveService>(mainComponent->getTrackDataModel());
    }
    setMenuBar(this, 24);
    centreWithSize(1600, 920);
    setVisible(true);

}

MainWindow::~MainWindow()
{
    setMenuBar(nullptr);
    projectFileChooser.reset();
}

void MainWindow::closeButtonPressed()
{
    confirmDiscardChanges([]
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    });
}

bool MainWindow::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown())
    {
        if (key.getKeyCode() == 'n' || key.getKeyCode() == 'N')
        {
            menuItemSelected(newProject, 0);
            return true;
        }
        if (key.getKeyCode() == 'o' || key.getKeyCode() == 'O')
        {
            chooseProjectToOpen();
            return true;
        }
        if (key.getKeyCode() == 's' || key.getKeyCode() == 'S')
        {
            if (key.getModifiers().isShiftDown())
                chooseProjectToSave();
            else
                saveCurrentProject();
            return true;
        }
        if (key.getKeyCode() == 'i' || key.getKeyCode() == 'I')
        {
            chooseAudioToImport();
            return true;
        }
    }

    if (mainComponent != nullptr && mainComponent->handleGlobalKeyPress(key))
        return true;

    return juce::DocumentWindow::keyPressed(key);
}

juce::StringArray MainWindow::getMenuBarNames()
{
    return { "File", "Edit", "Functions", "View" };
}

juce::PopupMenu MainWindow::getMenuForIndex(int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;
    const auto addItem = [&menu](int id, const juce::String& text, const juce::String& shortcut)
    {
        juce::PopupMenu::Item item(text);
        item.itemID = id;
        item.shortcutKeyDescription = shortcut;
        menu.addItem(std::move(item));
    };
    if (topLevelMenuIndex == 0)
    {
        juce::PopupMenu newMenu;
        newMenu.addItem(newProject, "Empty Project", true);
        newMenu.addSeparator();
        newMenu.addItem(newAudioRecordingTemplate, "Audio Recording Template");
        newMenu.addItem(newMidiProductionTemplate, "MIDI Production Template");
        const auto userTemplates = projectTemplates.load();
        if (! userTemplates.isEmpty())
        {
            newMenu.addSeparator();
            juce::PopupMenu userTemplateMenu;
            for (int index = 0; index < userTemplates.size(); ++index)
                userTemplateMenu.addItem(userTemplateBase + index,
                                         juce::File(userTemplates[index]).getFileNameWithoutExtension());
            newMenu.addSubMenu("My Templates", userTemplateMenu);
        }
        menu.addSubMenu("New", newMenu);

        addItem(openProject, "Open...", "Ctrl+O");
        juce::PopupMenu recentMenu;
        const auto recent = recentProjects.load();
        if (recent.isEmpty())
            recentMenu.addItem(recentProjectBase - 1, "No Recent Projects", false, false);
        else
            for (int index = 0; index < recent.size(); ++index)
                recentMenu.addItem(recentProjectBase + index, juce::File(recent[index]).getFileNameWithoutExtension());
        menu.addSubMenu("Open Recent", recentMenu);

        menu.addSeparator();
        menu.addItem(closeProject, "Close Project");
        menu.addSeparator();
        addItem(saveProject, "Save", "Ctrl+S");
        addItem(saveProjectAs, "Save As...", "Ctrl+Shift+S");
        menu.addItem(saveProjectCopy, "Save a Copy As...");
        menu.addItem(saveProjectTemplate, "Save as Template...");

        juce::PopupMenu alternativesMenu;
        alternativesMenu.addItem(newProjectAlternative, "New Alternative...", projectRootFile.existsAsFile());
        const auto alternatives = projectAlternatives.load(projectRootFile);
        if (! alternatives.isEmpty())
        {
            alternativesMenu.addSeparator();
            for (int index = 0; index < alternatives.size(); ++index)
                alternativesMenu.addItem(alternativeBase + index,
                                         juce::File(alternatives[index]).getFileNameWithoutExtension());
        }
        menu.addSubMenu("Project Alternatives", alternativesMenu, projectRootFile.existsAsFile());

        juce::PopupMenu importMenu;
        importMenu.addItem(importAudio, "Audio File...", "Ctrl+I");
        importMenu.addItem(importMidi, "MIDI File...");
        menu.addSubMenu("Import", importMenu);

        juce::PopupMenu bounceMenu;
        bounceMenu.addItem(bounceProject, "Project Mix...");
        menu.addSubMenu("Bounce", bounceMenu);

        menu.addSeparator();
        menu.addItem(projectSettings, "Project Settings...");
        addItem(deviceSettings, "Audio Device Settings...", "");
        menu.addSeparator(); menu.addItem(quitApplication, "Quit");
    }
    else if (topLevelMenuIndex == 1)
    {
        menu.addItem(undoEdit, "Undo", mainComponent != nullptr && mainComponent->getTrackDataModel().canUndo());
        menu.addItem(redoEdit, "Redo", mainComponent != nullptr && mainComponent->getTrackDataModel().canRedo());
    }
    else if (topLevelMenuIndex == 2)
    {
        menu.addItem(addTrack, "Create Track", mainComponent != nullptr
                     && mainComponent->getTrackDataModel().getTrackCount() < TrackDataModel::maxTracks);
        menu.addSeparator();
        menu.addItem(compactTracks, "Track Height: Compact", true,
                     mainComponent != nullptr && mainComponent->getTrackDataModel().getTrackHeight() == 40);
        menu.addItem(normalTracks, "Track Height: Standard", true,
                     mainComponent != nullptr && mainComponent->getTrackDataModel().getTrackHeight() == 60);
    }
    else if (topLevelMenuIndex == 3)
    {
        menu.addItem(toggleInspector, "Inspector", true, mainComponent != nullptr && mainComponent->isInspectorPanelVisible());
        menu.addItem(toggleBrowser, "Loops & Media", true, mainComponent != nullptr && mainComponent->isBrowserPanelVisible());
        menu.addItem(toggleMixer, "Mixer", true, mainComponent != nullptr && mainComponent->isMixerPanelVisible());
    }
    return menu;
}

void MainWindow::menuItemSelected(int menuItemID, int)
{
    switch (menuItemID)
    {
        case newProject:
        {
            const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
            confirmDiscardChanges([safeWindow]
            {
                if (safeWindow != nullptr && safeWindow->mainComponent != nullptr)
                {
                    const auto result = safeWindow->mainComponent->createNewProject();
                    if (result.failed()) safeWindow->showProjectError("New Project Failed", result);
                    else
                    {
                        safeWindow->currentProjectFile = {};
                        safeWindow->projectRootFile = {};
                        if (safeWindow->autosaveService != nullptr)
                        {
                            safeWindow->autosaveService->markProjectSaved();
                            safeWindow->autosaveService->setActiveProject({});
                        }
                        safeWindow->setName("StudioForge DAW — Untitled");
                    }
                }
            });
            break;
        }
        case newAudioRecordingTemplate: createProjectFromTemplate(ProjectTemplate::audioRecording); break;
        case newMidiProductionTemplate: createProjectFromTemplate(ProjectTemplate::midiProduction); break;
        case closeProject: closeCurrentProject(); break;
        case openProject:
        {
            const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
            confirmDiscardChanges([safeWindow]
            {
                if (safeWindow != nullptr)
                    safeWindow->chooseProjectToOpen();
            });
            break;
        }
        case saveProject: saveCurrentProject(); break;
        case saveProjectAs: chooseProjectToSave(); break;
        case saveProjectCopy: chooseProjectCopyToSave(); break;
        case saveProjectTemplate: chooseProjectTemplateToSave(); break;
        case newProjectAlternative: chooseProjectAlternativeToSave(); break;
        case importAudio: chooseAudioToImport(); break;
        case importMidi: chooseMidiToImport(); break;
        case bounceProject: if (mainComponent != nullptr) mainComponent->requestProjectBounce(); break;
        case projectSettings: showProjectSettings(); break;
        case deviceSettings: showDeviceSettings(); break;
        case quitApplication: closeButtonPressed(); break;
        case undoEdit: if (mainComponent != nullptr) mainComponent->undoEdit(); break;
        case redoEdit: if (mainComponent != nullptr) mainComponent->redoEdit(); break;
        case addTrack: if (mainComponent != nullptr) mainComponent->addTrackFromCommand(); break;
        case compactTracks: if (mainComponent != nullptr) mainComponent->setWorkspaceTrackHeight(40); break;
        case normalTracks: if (mainComponent != nullptr) mainComponent->setWorkspaceTrackHeight(60); break;
        case toggleInspector: if (mainComponent != nullptr) mainComponent->setInspectorPanelVisible(!mainComponent->isInspectorPanelVisible()); break;
        case toggleBrowser: if (mainComponent != nullptr) mainComponent->setBrowserPanelVisible(!mainComponent->isBrowserPanelVisible()); break;
        case toggleMixer: if (mainComponent != nullptr) mainComponent->setMixerPanelVisible(!mainComponent->isMixerPanelVisible()); break;
        default:
            if (menuItemID >= alternativeBase)
            {
                const auto alternatives = projectAlternatives.load(projectRootFile);
                const auto index = menuItemID - alternativeBase;
                if (juce::isPositiveAndBelow(index, alternatives.size()))
                    loadProjectAlternative(juce::File(alternatives[index]));
            }
            else if (menuItemID >= userTemplateBase)
            {
                const auto templates = projectTemplates.load();
                const auto index = menuItemID - userTemplateBase;
                if (juce::isPositiveAndBelow(index, templates.size()))
                    createProjectFromUserTemplate(juce::File(templates[index]));
            }
            else if (menuItemID >= recentProjectBase)
            {
                const auto recent = recentProjects.load();
                const auto index = menuItemID - recentProjectBase;
                if (juce::isPositiveAndBelow(index, recent.size()))
                {
                    const auto file = juce::File(recent[index]);
                    confirmDiscardChanges([safeWindow = juce::Component::SafePointer<MainWindow>(this), file]
                    {
                        if (safeWindow != nullptr)
                            safeWindow->loadProjectFromFile(file);
                    });
                }
            }
            break;
    }
}

void MainWindow::createProjectFromTemplate(ProjectTemplate projectTemplate)
{
    const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
    confirmDiscardChanges([safeWindow, projectTemplate]
    {
        if (safeWindow == nullptr || safeWindow->mainComponent == nullptr)
            return;

        const auto result = safeWindow->mainComponent->createProjectFromTemplate(projectTemplate);
        if (result.failed())
        {
            safeWindow->showProjectError("New Project Failed", result);
            return;
        }

        safeWindow->currentProjectFile = {};
        safeWindow->projectRootFile = {};
        if (safeWindow->autosaveService != nullptr)
        {
            safeWindow->autosaveService->markProjectSaved();
            safeWindow->autosaveService->setActiveProject({});
        }
        safeWindow->setName("StudioForge DAW - Untitled");
    });
}

void MainWindow::createProjectFromUserTemplate(const juce::File& file)
{
    const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
    confirmDiscardChanges([safeWindow, file]
    {
        if (safeWindow == nullptr || safeWindow->mainComponent == nullptr)
            return;

        juce::StringArray missingMedia;
        const auto result = safeWindow->mainComponent->loadProject(file, &missingMedia);
        if (result.failed())
        {
            safeWindow->showProjectError("New from Template Failed", result);
            return;
        }

        safeWindow->currentProjectFile = {};
        safeWindow->projectRootFile = {};
        if (safeWindow->autosaveService != nullptr)
        {
            safeWindow->autosaveService->markProjectSaved();
            safeWindow->autosaveService->setActiveProject({});
        }
        safeWindow->setName("StudioForge DAW - Untitled");
        if (! missingMedia.isEmpty())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                   "Template Opened with Missing Media",
                                                   missingMedia.joinIntoString("\n"));
    });
}

void MainWindow::loadProjectAlternative(const juce::File& file)
{
    const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
    confirmDiscardChanges([safeWindow, file]
    {
        if (safeWindow == nullptr || safeWindow->mainComponent == nullptr)
            return;

        juce::StringArray missingMedia;
        const auto result = safeWindow->mainComponent->loadProject(file, &missingMedia);
        if (result.failed())
        {
            safeWindow->showProjectError("Open Alternative Failed", result);
            return;
        }

        safeWindow->currentProjectFile = file;
        if (safeWindow->autosaveService != nullptr)
            safeWindow->autosaveService->setActiveProject(file);
        safeWindow->setName("StudioForge DAW - " + file.getFileNameWithoutExtension());
        if (! missingMedia.isEmpty())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                   "Alternative Opened with Missing Media",
                                                   missingMedia.joinIntoString("\n"));
    });
}

void MainWindow::chooseProjectToOpen()
{
    projectFileChooser = std::make_unique<juce::FileChooser>("Open StudioForge Project", currentProjectFile, "*.studioforge");
    const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
    projectFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                    [safeWindow](const juce::FileChooser& chooser)
    {
        if (safeWindow == nullptr)
            return;
        const auto selected = chooser.getResult();
        safeWindow->projectFileChooser.reset();
        if (selected.existsAsFile())
            safeWindow->loadProjectFromFile(selected);
    });
}

void MainWindow::chooseProjectToSave()
{
    const auto defaultFile = currentProjectFile.existsAsFile()
        ? currentProjectFile
        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Untitled.studioforge");
    projectFileChooser = std::make_unique<juce::FileChooser>("Save StudioForge Project", defaultFile, "*.studioforge");
    const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
    projectFileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                                    [safeWindow](const juce::FileChooser& chooser)
    {
        if (safeWindow == nullptr)
            return;
        auto selected = chooser.getResult();
        safeWindow->projectFileChooser.reset();
        if (selected == juce::File {})
            return;
        if (! selected.hasFileExtension("studioforge"))
            selected = selected.withFileExtension("studioforge");
        if (safeWindow->mainComponent != nullptr)
        {
            const auto result = safeWindow->mainComponent->saveProject(selected);
            if (result.failed()) safeWindow->showProjectError("Save Failed", result);
            else
            {
                safeWindow->currentProjectFile = selected;
                safeWindow->projectRootFile = selected;
                if (safeWindow->autosaveService != nullptr)
                {
                    safeWindow->autosaveService->setActiveProject(selected);
                    safeWindow->autosaveService->markProjectSaved();
                }
                safeWindow->recentProjects.add(selected);
                safeWindow->refreshRecentProjects();
                safeWindow->setName("StudioForge DAW — " + selected.getFileNameWithoutExtension());
            }
        }
    });
}

void MainWindow::chooseProjectCopyToSave()
{
    const auto defaultFile = currentProjectFile.existsAsFile()
        ? currentProjectFile.getSiblingFile(currentProjectFile.getFileNameWithoutExtension() + " Copy.studioforge")
        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Untitled Copy.studioforge");
    projectFileChooser = std::make_unique<juce::FileChooser>("Save Project Copy", defaultFile, "*.studioforge");
    const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
    projectFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                        | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::warnAboutOverwriting,
                                    [safeWindow](const juce::FileChooser& chooser)
    {
        if (safeWindow == nullptr)
            return;

        auto selected = chooser.getResult();
        safeWindow->projectFileChooser.reset();
        if (selected == juce::File {} || safeWindow->mainComponent == nullptr)
            return;
        if (! selected.hasFileExtension("studioforge"))
            selected = selected.withFileExtension("studioforge");

        const auto result = safeWindow->mainComponent->saveProjectCopy(selected);
        if (result.failed())
            safeWindow->showProjectError("Save Copy Failed", result);
    });
}

void MainWindow::chooseProjectAlternativeToSave()
{
    if (! projectRootFile.existsAsFile())
        return;

    const auto defaultFile = projectAlternatives.makeAlternativeFile(projectRootFile, "Alternative");
    projectFileChooser = std::make_unique<juce::FileChooser>("Create Project Alternative", defaultFile,
                                                              "*.studioforge");
    const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
    projectFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                        | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::warnAboutOverwriting,
                                    [safeWindow](const juce::FileChooser& chooser)
    {
        if (safeWindow == nullptr || safeWindow->mainComponent == nullptr)
            return;

        const auto selected = chooser.getResult();
        safeWindow->projectFileChooser.reset();
        if (selected == juce::File {})
            return;

        const auto destination = safeWindow->projectAlternatives.makeAlternativeFile(
            safeWindow->projectRootFile, selected.getFileNameWithoutExtension());
        const auto result = safeWindow->mainComponent->saveProjectCopy(destination);
        if (result.failed())
        {
            safeWindow->showProjectError("Create Alternative Failed", result);
            return;
        }

        safeWindow->currentProjectFile = destination;
        safeWindow->mainComponent->markProjectSaved();
        if (safeWindow->autosaveService != nullptr)
        {
            safeWindow->autosaveService->setActiveProject(destination);
            safeWindow->autosaveService->markProjectSaved();
        }
        safeWindow->setName("StudioForge DAW - " + destination.getFileNameWithoutExtension());
    });
}

void MainWindow::chooseProjectTemplateToSave()
{
    const auto defaultFile = projectTemplates.makeTemplateFile(currentProjectFile.existsAsFile()
        ? currentProjectFile.getFileNameWithoutExtension()
        : "Untitled Template");
    projectFileChooser = std::make_unique<juce::FileChooser>("Save Project Template", defaultFile,
                                                              "*.studioforge-template");
    const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
    projectFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                        | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::warnAboutOverwriting,
                                    [safeWindow](const juce::FileChooser& chooser)
    {
        if (safeWindow == nullptr)
            return;

        auto selected = chooser.getResult();
        safeWindow->projectFileChooser.reset();
        if (selected == juce::File {} || safeWindow->mainComponent == nullptr)
            return;
        if (! selected.hasFileExtension("studioforge-template"))
            selected = selected.withFileExtension("studioforge-template");

        const auto result = safeWindow->mainComponent->saveProjectTemplate(selected);
        if (result.failed())
            safeWindow->showProjectError("Save Template Failed", result);
    });
}

void MainWindow::closeCurrentProject()
{
    const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
    confirmDiscardChanges([safeWindow]
    {
        if (safeWindow == nullptr || safeWindow->mainComponent == nullptr)
            return;

        const auto result = safeWindow->mainComponent->createNewProject();
        if (result.failed())
        {
            safeWindow->showProjectError("Close Project Failed", result);
            return;
        }

        safeWindow->currentProjectFile = {};
        safeWindow->projectRootFile = {};
        if (safeWindow->autosaveService != nullptr)
        {
            safeWindow->autosaveService->markProjectSaved();
            safeWindow->autosaveService->setActiveProject({});
        }
        safeWindow->setName("StudioForge DAW - Untitled");
    });
}

void MainWindow::chooseAudioToImport()
{
    audioFileChooser = std::make_unique<juce::FileChooser>("Import Audio", juce::File {}, "*.wav;*.aif;*.aiff;*.mp3");
    const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
    audioFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safeWindow](const juce::FileChooser& chooser)
    {
        if (safeWindow == nullptr)
            return;
        const auto selected = chooser.getResult();
        safeWindow->audioFileChooser.reset();
        if (selected.existsAsFile() && safeWindow->mainComponent != nullptr)
            safeWindow->mainComponent->importAudioFile(selected);
    });
}

void MainWindow::chooseMidiToImport()
{
    midiFileChooser = std::make_unique<juce::FileChooser>("Import MIDI File", juce::File {}, "*.mid;*.midi");
    const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
    midiFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [safeWindow](const juce::FileChooser& chooser)
    {
        if (safeWindow == nullptr)
            return;

        const auto selected = chooser.getResult();
        safeWindow->midiFileChooser.reset();
        if (! selected.existsAsFile() || safeWindow->mainComponent == nullptr)
            return;

        const auto result = safeWindow->mainComponent->importMidiFile(selected);
        if (result.failed())
            safeWindow->showProjectError("MIDI Import Failed", result);
    });
}

void MainWindow::showDeviceSettings()
{
    if (mainComponent == nullptr)
        return;

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new DeviceSettingsPanel(mainComponent->getAudioEngine()));
    options.dialogTitle = "Audio Device Settings";
    options.dialogBackgroundColour = juce::Colour(0xff25282d);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.content->setSize(560, 420);
    options.launchAsync();
}

void MainWindow::showProjectSettings()
{
    if (mainComponent == nullptr)
        return;

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new ProjectSettingsPanel(mainComponent->getTrackDataModel()));
    options.dialogTitle = "Project Settings";
    options.dialogBackgroundColour = juce::Colour(0xff25282d);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;
    options.content->setSize(320, 190);
    options.launchAsync();
}

void MainWindow::saveCurrentProject()
{
    if (! currentProjectFile.existsAsFile())
    {
        chooseProjectToSave();
        return;
    }
    if (mainComponent != nullptr)
    {
        const auto result = mainComponent->saveProject(currentProjectFile);
        if (result.failed()) showProjectError("Save Failed", result);
        else if (autosaveService != nullptr) autosaveService->markProjectSaved();
    }
}

void MainWindow::loadProjectFromFile(const juce::File& file)
{
    if (mainComponent == nullptr)
        return;

    juce::StringArray missingMedia;
    const auto result = mainComponent->loadProject(file, &missingMedia);
    if (result.failed())
    {
        showProjectError("Open Failed", result);
        return;
    }

    currentProjectFile = file;
    projectRootFile = file;
    if (autosaveService != nullptr)
        autosaveService->setActiveProject(file);
    recentProjects.add(file);
    refreshRecentProjects();
    setName("StudioForge DAW — " + file.getFileNameWithoutExtension());
    if (! missingMedia.isEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               "Project Opened with Missing Media",
                                               "Some audio files could not be found:\n" + missingMedia.joinIntoString("\n"));

    if (autosaveService != nullptr && autosaveService->hasNewerRecoverySnapshot())
    {
        const auto recoveryFile = autosaveService->getRecoveryFile();
        const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
        const auto options = juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Recovery Snapshot Available")
            .withMessage("StudioForge found a newer autosave snapshot for this project. Recover it?")
            .withButton("Recover")
            .withButton("Keep Saved Project")
            .withAssociatedComponent(this);
        juce::NativeMessageBox::showAsync(options, [safeWindow, recoveryFile](int result)
        {
            if (result != 1 || safeWindow == nullptr || safeWindow->mainComponent == nullptr)
                return;

            const auto recoveryResult = safeWindow->mainComponent->loadProject(recoveryFile);
            if (recoveryResult.failed())
            {
                safeWindow->showProjectError("Recovery Failed", recoveryResult);
                return;
            }

            safeWindow->mainComponent->markProjectRecovered();
            safeWindow->setName("StudioForge DAW — "
                                + safeWindow->currentProjectFile.getFileNameWithoutExtension()
                                + " (Recovered)");
        });
    }
}

void MainWindow::refreshRecentProjects()
{
    if (mainComponent != nullptr)
        mainComponent->setRecentProjects(recentProjects.load());
}

void MainWindow::confirmDiscardChanges(std::function<void()> continuation)
{
    if (mainComponent == nullptr || ! mainComponent->isProjectDirty())
    {
        continuation();
        return;
    }

    const auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
    const auto options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("Unsaved Changes")
        .withMessage("This project has unsaved changes. Discard them?")
        .withButton("Discard")
        .withButton("Cancel")
        .withAssociatedComponent(this);
    juce::NativeMessageBox::showAsync(options, [safeWindow, continuation = std::move(continuation)](int result)
    {
        if (result == 1 && safeWindow != nullptr)
            continuation();
    });
}

void MainWindow::showProjectError(const juce::String& title, const juce::Result& result) const
{
    StudioForgeDialog::showWarning(title, result.getErrorMessage());
}

void MainWindow::renderOpenGL()
{
    juce::OpenGLHelpers::clear(juce::Colour(0xff1a1a1a));
}
