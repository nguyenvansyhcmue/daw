#include "MainComponent.h"
#include "../Midi/MidiFileImporter.h"
#include "../Project/ProjectSerializer.h"
#include "../Project/ProjectState.h"

MainComponent::MainComponent()
{
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
    controlBar.onCountInChanged = [this](bool enabled) { countInEnabled = enabled; };
    controlBar.onPunchChanged = [this](bool enabled)
    {
        if (! enabled)
        {
            trackDataModel.clearPunchRange();
            return;
        }

        if (trackDataModel.isCycleActive())
        {
            trackDataModel.setPunchRange(trackDataModel.getCycleStartSample(), trackDataModel.getCycleEndSample());
            return;
        }

        const auto samplesPerBeat = trackDataModel.getSampleRate() * 60.0 / trackDataModel.getBpm();
        const auto start = trackDataModel.getPlayheadPosition();
        trackDataModel.setPunchRange(start, start + samplesPerBeat * trackDataModel.getTimeSignatureNumerator());
    };
    startupWorkflow.onTemplateSelected = [this] (ProjectTemplate projectTemplate)
    {
        createProjectFromTemplate(projectTemplate);
    };
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
    arrangeWindow.onAudioClipSelected = [this](ClipId) {};
    arrangeWindow.onMidiClipSelected = [this](MidiClipId clip)
    {
        pianoRoll.setActiveClip(clip);
        pianoRoll.setVisible(true);
        controlBar.setPianoRollVisible(true);
        resized();
    };
    assetBrowser.onAudioFileActivated = [this](const juce::File& file)
    {
        importAudioFile(file);
    };
    performanceFooter.onBounceRequested = [this] { requestProjectBounce(); };

    // A project starts empty; tracks are created through the explicit New Track workflow.
    markProjectSaved();
    startTimerHz(30);

    const juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::Timer::callAfterDelay(1, [safeThis]
    {
        if (safeThis != nullptr)
            safeThis->audioEngine.initialise();
    });
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
    stopTimer();
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
    if (key.getKeyCode() == 'c' || key.getKeyCode() == 'C')
    {
        controlBar.toggleCycle();
        return true;
    }
    if (key.getKeyCode() == 'k' || key.getKeyCode() == 'K')
    {
        audioEngine.setMetronomeEnabled(! audioEngine.isMetronomeEnabled());
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

juce::Result MainComponent::saveProjectCopy(const juce::File& file) const
{
    return ProjectSerializer::save(trackDataModel, file);
}

juce::Result MainComponent::saveProjectTemplate(const juce::File& file) const
{
    return ProjectSerializer::save(trackDataModel, file);
}

juce::Result MainComponent::loadProject(const juce::File& file, juce::StringArray* missingMediaReferences)
{
    audioEngine.setPlaybackState(false);
    const auto result = ProjectSerializer::load(trackDataModel, pluginHost, file, missingMediaReferences);
    if (result.wasOk())
    {
        audioEngine.prepareActiveEffects();
        selectTrack(0);
        mixerPane.refreshFromModel();
        arrangeWindow.repaint();
        arrangeWindow.requestWaveformPreparation();
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
    return createProjectFromTemplate(ProjectTemplate::empty);
}

juce::Result MainComponent::createProjectFromTemplate(ProjectTemplate projectTemplate)
{
    audioEngine.setPlaybackState(false);
    const auto result = trackDataModel.applyProjectState(ProjectTemplates::create(projectTemplate));
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

bool MainComponent::isProjectDirty() const noexcept
{
    return recoveredProjectNeedsSave || trackDataModel.getProjectRevision() != savedProjectRevision;
}

void MainComponent::markProjectSaved() noexcept
{
    savedProjectRevision = trackDataModel.getProjectRevision();
    recoveredProjectNeedsSave = false;
}

void MainComponent::markProjectRecovered() noexcept
{
    recoveredProjectNeedsSave = true;
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

void MainComponent::importAudioFile(const juce::File& file)
{
    arrangeWindow.importAudioFile(file, getSelectedTrackIndex(), trackDataModel.getPlayheadPosition());
}

juce::Result MainComponent::importMidiFile(const juce::File& file)
{
    std::vector<ImportedMidiTrack> importedTracks;
    const auto parseResult = MidiFileImporter::read(file, trackDataModel.getSampleRate(),
                                                     trackDataModel.getBpm(), importedTracks);
    if (parseResult.failed())
        return parseResult;

    const auto result = trackDataModel.importMidiTracks(importedTracks, trackDataModel.getPlayheadPosition());
    if (result.failed())
        return result;

    selectTrack(static_cast<int>(trackDataModel.getTrackCount() - importedTracks.size()));
    mixerPane.refreshFromModel();
    arrangeWindow.repaint();
    return juce::Result::ok();
}

void MainComponent::requestProjectBounce()
{
    bounceFileChooser = std::make_unique<juce::FileChooser>("Bounce Project", juce::File {}, "*.wav;*.aif;*.aiff;*.flac");
    bounceFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                       | juce::FileBrowserComponent::canSelectFiles,
        [safeOwner = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser)
        {
            if (safeOwner == nullptr)
                return;

            auto output = chooser.getResult();
            safeOwner->bounceFileChooser.reset();
            if (output == juce::File {})
                return;

            if (output.getFileExtension().isEmpty())
                output = output.withFileExtension(".wav");

            AudioEngine::OfflineRenderOptions options;
            options.sampleRate = safeOwner->trackDataModel.getSampleRate();
            const auto result = safeOwner->audioEngine.renderOfflineAudio(output, options);
            if (result.failed())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "Bounce Failed", result.getErrorMessage());
        });
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
    if (audioEngine.isRecording() || audioEngine.isMidiRecording() || recordingCountdownActive)
    {
        if (audioEngine.isRecording())
            audioEngine.stopRecording();
        if (audioEngine.isMidiRecording())
            audioEngine.stopMidiRecording();
        audioEngine.clearRecordingCaptureRange();
        recordingCountdownActive = false;
        recordingPunchStopSample = -1.0;
        pendingRecordingTrack = -1;
        pendingRecordingDestination = {};
        controlBar.setRecordActive(false);
        controlBar.setRecordCountdown(false);
        arrangeWindow.repaint();
        return;
    }

    const auto armedTrack = trackDataModel.getFirstArmedTrackIndex();
    const auto target = armedTrack >= 0 ? armedTrack : getSelectedTrackIndex();
    if (target < 0 || target >= static_cast<int>(trackDataModel.getTrackCount()))
        return;

    const auto type = trackDataModel.getTrack(static_cast<size_t>(target)).type;
    if (type != TrackType::audio && type != TrackType::instrument && type != TrackType::externalMidi)
        return;
    const auto destination = type == TrackType::audio ? createRecordingDestination() : juce::File {};
    if (type == TrackType::audio && destination == juce::File {})
        return;
    if (type == TrackType::audio && trackDataModel.isPunchActive())
    {
        const auto punchIn = trackDataModel.getPunchInSample();
        const auto punchOut = trackDataModel.getPunchOutSample();
        audioEngine.setRecordingCaptureRange(punchIn, punchOut);
        recordingPunchStopSample = punchOut;
        audioEngine.setPlaybackState(true);
        startRecordingNow(target, destination, punchIn);
        return;
    }

    audioEngine.clearRecordingCaptureRange();
    if (! countInEnabled)
    {
        startRecordingNow(target, destination);
        return;
    }

    const auto samplesPerBeat = trackDataModel.getSampleRate() * 60.0 / trackDataModel.getBpm();
    pendingRecordingStartSample = trackDataModel.getPlayheadPosition()
        + samplesPerBeat * trackDataModel.getTimeSignatureNumerator();
    pendingRecordingTrack = target;
    pendingRecordingDestination = destination;
    recordingCountdownActive = true;
    audioEngine.setPlaybackState(true);
    controlBar.setRecordCountdown(true);
}

void MainComponent::startRecordingNow(int trackIndex, const juce::File& destination,
                                      double timelineStartSample)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(trackDataModel.getTrackCount()))
        return;
    const auto type = trackDataModel.getTrack(static_cast<size_t>(trackIndex)).type;
    const auto result = type == TrackType::audio
        ? audioEngine.startRecording(static_cast<size_t>(trackIndex), destination, timelineStartSample)
        : audioEngine.startMidiRecording(static_cast<size_t>(trackIndex));
    if (result.wasOk())
        controlBar.setRecordActive(true);
}

void MainComponent::timerCallback()
{
    if (audioEngine.isRecording() && recordingPunchStopSample >= 0.0
        && trackDataModel.getPlayheadPosition() >= recordingPunchStopSample)
    {
        audioEngine.stopRecording();
        recordingPunchStopSample = -1.0;
        controlBar.setRecordActive(false);
        arrangeWindow.repaint();
        return;
    }

    if (! recordingCountdownActive || trackDataModel.getPlayheadPosition() < pendingRecordingStartSample)
        return;

    const auto track = pendingRecordingTrack;
    const auto destination = pendingRecordingDestination;
    recordingCountdownActive = false;
    pendingRecordingTrack = -1;
    pendingRecordingDestination = {};
    controlBar.setRecordCountdown(false);
    if (track >= 0)
        startRecordingNow(track, destination);
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
