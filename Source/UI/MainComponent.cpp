#include "MainComponent.h"
#include "../Project/ProjectSerializer.h"
#include "../Project/ProjectState.h"

MainComponent::MainComponent()
{
    audioEngine.initialise();
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(controlBar);
    addAndMakeVisible(arrangeWindow);
    addChildComponent(inspectorPane);
    addChildComponent(assetBrowser);
    addAndMakeVisible(performanceFooter);
    addChildComponent(mixerPane);
    addChildComponent(pianoRoll);
    addAndMakeVisible(startupWorkflow);

    // Keep the selected channel's controls in view by default, matching the
    // arrangement-first workflow rather than starting with a blank left rail.
    inspectorPane.setVisible(true);
    controlBar.setInspectorVisible(true);
    assetBrowser.setVisible(true);
    controlBar.setBrowserVisible(true);

    controlBar.onMixerToggle = [this]
    {
        const auto visible = ! mixerPane.isVisible();
        mixerPane.setVisible(visible);
        controlBar.setMixerVisible(visible);
        resized();
    };
    controlBar.onInspectorToggle = [this]
    {
        const auto visible = ! inspectorPane.isVisible();
        inspectorPane.setVisible(visible);
        controlBar.setInspectorVisible(visible);
        resized();
    };
    controlBar.onPianoRollToggle = [this]
    {
        const auto visible = ! pianoRoll.isVisible();
        pianoRoll.setVisible(visible);
        controlBar.setPianoRollVisible(visible);
        resized();
    };
    controlBar.onBrowserToggle = [this]
    {
        const auto visible = ! assetBrowser.isVisible();
        assetBrowser.setVisible(visible);
        controlBar.setBrowserVisible(visible);
        resized();
    };
    controlBar.onCreateTrack = [this](TrackType)
    {
        configureTrackCreationDialog();
        startupWorkflow.setVisible(true);
        startupWorkflow.showTrackCreation(true);
    };
    controlBar.onRecordRequested = [this] { toggleRecording(); };
    startupWorkflow.onEmptyProject = [this] { createNewProject(); };
    startupWorkflow.onOpenProject = [this]
    {
        if (onOpenProjectRequested != nullptr)
            onOpenProjectRequested();
    };
    startupWorkflow.onOpenRecentProject = [this](const juce::File& file)
    {
        if (onOpenRecentProjectRequested != nullptr)
            onOpenRecentProjectRequested(file);
    };
    startupWorkflow.onCreateTracks = [this](TrackType type, int count)
    {
        const auto firstNewTrack = static_cast<int>(trackDataModel.getTrackCount());
        auto created = 0;
        for (int index = 0; index < count; ++index)
        {
            if (! addTrackFromCommand(type))
                break;
            ++created;
        }
        configureTrackCreationDialog();
        if (created > 0)
            selectTrack(firstNewTrack);
        return created;
    };
    arrangeWindow.onTrackSelected = [this](int track) { selectTrack(track); };
    arrangeWindow.onAudioClipSelected = [this](ClipId clip) { inspectorPane.setSelectedAudioClip(clip); };
    arrangeWindow.onMidiClipSelected = [this](MidiClipId clip)
    {
        inspectorPane.setSelectedMidiClip(clip);
        pianoRoll.setActiveClip(clip);
        pianoRoll.setVisible(true);
        controlBar.setPianoRollVisible(true);
        resized();
    };
    assetBrowser.onAudioFileActivated = [this](const juce::File& file)
    {
        const auto trackIndex = getSelectedTrackIndex();
        if (trackIndex >= 0)
            arrangeWindow.importAudioFile(file, trackIndex, trackDataModel.getPlayheadPosition());
    };
    performanceFooter.onBounceRequested = [this]
    {
        bounceFileChooser = std::make_unique<juce::FileChooser>("Bounce mixdown", juce::File {}, "*.wav");
        bounceFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                           | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& chooser)
            {
                const auto output = chooser.getResult();
                if (output != juce::File {})
                    audioEngine.renderOfflineWav(output.withFileExtension(".wav"), trackDataModel.getSampleRate());
                bounceFileChooser.reset();
            });
    };

    // A project starts empty; tracks are created through the explicit New Track workflow.
    markProjectSaved();
}

void MainComponent::showInitialTrackCreation()
{
    configureTrackCreationDialog();
    startupWorkflow.setVisible(true);
    startupWorkflow.showTrackCreation(false);
}

void MainComponent::configureTrackCreationDialog()
{
    const auto remaining = static_cast<int>(TrackDataModel::maxTracks - trackDataModel.getTrackCount());
    startupWorkflow.setAvailableTrackSlots(remaining);

    const auto* device = audioEngine.getAudioDeviceManager().getCurrentAudioDevice();
    const auto inputs = device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 0;
    const auto outputs = device != nullptr ? device->getActiveOutputChannels().countNumberOfSetBits() : 0;
    startupWorkflow.setAudioDeviceChannels(inputs, outputs);
}

MainComponent::~MainComponent()
{
    setLookAndFeel(nullptr);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    return handleGlobalKeyPress(key);
}

bool MainComponent::handleGlobalKeyPress(const juce::KeyPress& key)
{
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;

    if (key.isKeyCode(juce::KeyPress::spaceKey))
    {
        controlBar.togglePlayback();
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::returnKey))
    {
        controlBar.stopPlayback();
        trackDataModel.setPlayheadPosition(0.0);
        return true;
    }
    if (key.getKeyCode() == 'r' || key.getKeyCode() == 'R')
    {
        controlBar.toggleRecording();
        return true;
    }

    return false;
}

juce::Result MainComponent::saveProject(const juce::File& file)
{
    const auto result = ProjectSerializer::save(trackDataModel, file);
    if (result.wasOk())
        markProjectSaved();
    return result;
}

juce::Result MainComponent::loadProject(const juce::File& file, juce::StringArray* missingMediaReferences)
{
    audioEngine.setPlaybackState(false);
    const auto result = ProjectSerializer::load(trackDataModel, file, missingMediaReferences);
    if (result.wasOk())
    {
        selectTrack(0);
        mixerPane.refreshFromModel();
        arrangeWindow.repaint();
        pianoRoll.repaint();
        markProjectSaved();
        if (trackDataModel.getTrackCount() == 0)
            showInitialTrackCreation();
        else
            showWorkspace();
    }
    return result;
}

juce::Result MainComponent::createNewProject()
{
    audioEngine.setPlaybackState(false);
    ProjectState state;
    state.tempoMap.push_back({ 0.0, 120.0 });

    const auto result = trackDataModel.applyProjectState(state);
    if (result.wasOk())
    {
        selectTrack(0);
        mixerPane.refreshFromModel();
        arrangeWindow.repaint();
        pianoRoll.repaint();
        markProjectSaved();
        showInitialTrackCreation();
    }
    return result;
}

bool MainComponent::isProjectDirty() const noexcept
{
    return trackDataModel.getProjectRevision() != savedProjectRevision;
}

void MainComponent::markProjectSaved() noexcept
{
    savedProjectRevision = trackDataModel.getProjectRevision();
}

bool MainComponent::undoEdit()
{
    const auto changed = trackDataModel.undo();
    if (changed) { mixerPane.refreshFromModel(); arrangeWindow.repaint(); }
    return changed;
}

bool MainComponent::redoEdit()
{
    const auto changed = trackDataModel.redo();
    if (changed) { mixerPane.refreshFromModel(); arrangeWindow.repaint(); }
    return changed;
}

void MainComponent::setInspectorPanelVisible(bool visible)
{
    inspectorPane.setVisible(visible); controlBar.setInspectorVisible(visible); resized();
}

void MainComponent::setBrowserPanelVisible(bool visible)
{
    assetBrowser.setVisible(visible); controlBar.setBrowserVisible(visible); resized();
}

void MainComponent::setMixerPanelVisible(bool visible)
{
    mixerPane.setVisible(visible); controlBar.setMixerVisible(visible); resized();
}

bool MainComponent::addTrackFromCommand(TrackType type)
{
    const auto id = trackDataModel.addTrack(type);
    if (! id.isValid())
        return false;
    selectTrack(trackDataModel.getTrackIndex(id));
    arrangeWindow.repaint();
    mixerPane.refreshFromModel();
    return true;
}

void MainComponent::selectTrack(int trackIndex)
{
    const auto count = static_cast<int>(trackDataModel.getTrackCount());
    if (count <= 0)
    {
        selectedTrackId = {};
        arrangeWindow.setSelectedTrack(-1);
        return;
    }
    const auto resolvedIndex = juce::jlimit(0, count - 1, trackIndex);
    selectedTrackId = trackDataModel.getTrackId(static_cast<size_t>(resolvedIndex));
    inspectorPane.setSelectedTrack(resolvedIndex);
    arrangeWindow.setSelectedTrack(resolvedIndex);
}

int MainComponent::getSelectedTrackIndex() const noexcept
{
    return selectedTrackId.isValid() ? trackDataModel.getTrackIndex(selectedTrackId) : -1;
}

juce::File MainComponent::createRecordingDestination() const
{
    auto directory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("StudioForge Recordings");
    if (! directory.exists() && ! directory.createDirectory())
        return {};
    return directory.getNonexistentChildFile("Recording", ".wav", true);
}

void MainComponent::toggleRecording()
{
    if (audioEngine.isRecording())
    {
        audioEngine.stopRecording();
        controlBar.setRecordActive(false);
        arrangeWindow.repaint();
        return;
    }

    const auto armedTrack = trackDataModel.getFirstArmedTrackIndex();
    const auto target = armedTrack >= 0 ? armedTrack : getSelectedTrackIndex();
    if (target < 0 || target >= static_cast<int>(trackDataModel.getTrackCount())
        || trackDataModel.getTrack(static_cast<size_t>(target)).type != TrackType::audio)
        return;

    const auto destination = createRecordingDestination();
    if (destination == juce::File {})
        return;
    if (audioEngine.startRecording(static_cast<size_t>(target), destination).wasOk())
        controlBar.setRecordActive(true);
}

void MainComponent::setWorkspaceTrackHeight(int height)
{
    trackDataModel.setTrackHeight(height);
    arrangeWindow.repaint();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(StudioForgeTheme::workspaceBackground);
    auto glow = juce::ColourGradient(StudioForgeTheme::accentBlue, 0.0f, 0.0f,
                                     StudioForgeTheme::accentCyan, getWidth(), 0.0f, false);
    g.setGradientFill(glow);
    g.fillRect(0.0f, 0.0f, static_cast<float>(getWidth()), 2.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    controlBar.setBounds(area.removeFromTop(54));
    performanceFooter.setBounds(area.removeFromBottom(38));

    // Dock panes from a bounded budget so opening both lower editors never
    // collapses the arrange workspace or overlaps controls on smaller screens.
    const auto dockBudget = juce::jmax(0, area.getHeight() - 220);
    const auto requestedDockHeight = (mixerPane.isVisible() ? 210 : 0)
                                   + (pianoRoll.isVisible() ? 250 : 0);
    const auto dockScale = requestedDockHeight > 0
        ? juce::jmin(1.0f, static_cast<float>(dockBudget) / static_cast<float>(requestedDockHeight)) : 0.0f;
    if (mixerPane.isVisible())
        mixerPane.setBounds(area.removeFromBottom(juce::roundToInt(210.0f * dockScale)));
    if (pianoRoll.isVisible())
        pianoRoll.setBounds(area.removeFromBottom(juce::roundToInt(250.0f * dockScale)));

    const auto sideBudget = juce::jmax(0, area.getWidth() - 360);
    const auto requestedSideWidth = (inspectorPane.isVisible() ? 250 : 0)
                                  + (assetBrowser.isVisible() ? 250 : 0);
    const auto sideScale = requestedSideWidth > 0
        ? juce::jmin(1.0f, static_cast<float>(sideBudget) / static_cast<float>(requestedSideWidth)) : 0.0f;
    if (inspectorPane.isVisible())
        inspectorPane.setBounds(area.removeFromLeft(juce::roundToInt(250.0f * sideScale)));
    if (assetBrowser.isVisible())
        assetBrowser.setBounds(area.removeFromRight(juce::roundToInt(250.0f * sideScale)));

    arrangeWindow.setBounds(area);
    startupWorkflow.setBounds(getLocalBounds());
}
