#include "MainWindow.h"

MainWindow::MainWindow(const juce::String& name)
    : juce::DocumentWindow(name,
          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
          juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(1200, 720, 3000, 1800);
    setContentOwned(new MainComponent(), true);

    mainComponent = dynamic_cast<MainComponent*>(getContentComponent());
    if (mainComponent != nullptr)
    {
        mainComponent->onOpenProjectRequested = [this] { chooseProjectToOpen(); };
        mainComponent->onOpenRecentProjectRequested = [this](const juce::File& file)
        {
            confirmDiscardChanges([safeWindow = juce::Component::SafePointer<MainWindow>(this), file]
            {
                if (safeWindow != nullptr) safeWindow->loadProjectFromFile(file);
            });
        };
        refreshRecentProjects();
    }
    setMenuBar(this, 24);
    centreWithSize(1600, 920);
    setVisible(true);

    openGLContext = std::make_unique<juce::OpenGLContext>();
    openGLContext->setRenderer(this);
    openGLContext->attachTo(*this);
}

MainWindow::~MainWindow()
{
    setMenuBar(nullptr);
    projectFileChooser.reset();
    if (openGLContext != nullptr)
    {
        openGLContext->detach();
    }
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
        addItem(newProject, "New Project", "Ctrl+N"); addItem(openProject, "Open...", "Ctrl+O");
        menu.addSeparator(); addItem(importAudio, "Import Audio...", "Ctrl+I");
        menu.addSeparator(); addItem(saveProject, "Save", "Ctrl+S"); addItem(saveProjectAs, "Save As...", "Ctrl+Shift+S");
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
                        safeWindow->setName("StudioForge DAW — Untitled");
                    }
                }
            });
            break;
        }
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
        case importAudio: chooseAudioToImport(); break;
        case quitApplication: closeButtonPressed(); break;
        case undoEdit: if (mainComponent != nullptr) mainComponent->undoEdit(); break;
        case redoEdit: if (mainComponent != nullptr) mainComponent->redoEdit(); break;
        case addTrack: if (mainComponent != nullptr) mainComponent->addTrackFromCommand(); break;
        case compactTracks: if (mainComponent != nullptr) mainComponent->setWorkspaceTrackHeight(40); break;
        case normalTracks: if (mainComponent != nullptr) mainComponent->setWorkspaceTrackHeight(60); break;
        case toggleInspector: if (mainComponent != nullptr) mainComponent->setInspectorPanelVisible(!mainComponent->isInspectorPanelVisible()); break;
        case toggleBrowser: if (mainComponent != nullptr) mainComponent->setBrowserPanelVisible(!mainComponent->isBrowserPanelVisible()); break;
        case toggleMixer: if (mainComponent != nullptr) mainComponent->setMixerPanelVisible(!mainComponent->isMixerPanelVisible()); break;
        default: break;
    }
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
                safeWindow->recentProjects.add(selected);
                safeWindow->refreshRecentProjects();
                safeWindow->setName("StudioForge DAW — " + selected.getFileNameWithoutExtension());
            }
        }
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
    recentProjects.add(file);
    refreshRecentProjects();
    setName("StudioForge DAW — " + file.getFileNameWithoutExtension());
    if (! missingMedia.isEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               "Project Opened with Missing Media",
                                               "Some audio files could not be found:\n" + missingMedia.joinIntoString("\n"));
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
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, title, result.getErrorMessage());
}

void MainWindow::renderOpenGL()
{
    juce::OpenGLHelpers::clear(juce::Colour(0xff1a1a1a));
}
