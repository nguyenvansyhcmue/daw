#include "MainComponent.h"
#include "../Midi/MidiFileImporter.h"
#include "../Midi/MidiCore.h"
#include "../Project/ProjectSerializer.h"
#include "../Project/ProjectState.h"
#include "StudioForgeDialog.h"

#include <algorithm>

MainComponent::MainComponent()
{
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(controlBar);
    addAndMakeVisible(arrangeWindow);
    addChildComponent(inspectorPane);
    addChildComponent(assetBrowser);
    addChildComponent(mixerPane);
    addChildComponent(pianoRoll);
    addAndMakeVisible(startupWorkflow);
    // The companion panel is optional. Starting its local-only bridge here
    // never touches the audio thread and does not block DAW startup.
    if (const auto result = performanceSurfaceBridge.start(); result.failed())
        DBG("Performance surface unavailable: " + result.getErrorMessage());

    // Keep the selected channel's controls in view by default, matching the
    // arrangement-first workflow rather than starting with a blank left rail.
    setWorkspacePanelsVisible(true, false, false, false);

    workspaceModeController = std::make_unique<WorkspaceModeController>(audioEngine,
        [this](const WorkspaceProfile& profile) { applyWorkspaceMode(profile); });

    mixerPane.onHeightChangeRequested = [this] (int height)
    {
        juce::ignoreUnused(height);
        resized();
    };

    controlBar.onMixerToggle = [this]
    {
        setMixerPanelVisible(! mixerPane.isVisible());
    };
    controlBar.onInspectorToggle = [this]
    {
        setInspectorPanelVisible(! inspectorPane.isVisible());
    };
    controlBar.onPianoRollToggle = [this]
    {
        setPianoRollPanelVisible(! pianoRoll.isVisible());
    };
    controlBar.onBrowserToggle = [this]
    {
        setBrowserPanelVisible(! assetBrowser.isVisible());
    };
    controlBar.onAutoKeyRequested = [this]
    {
        requestTuneAiUrlAnalysis();
    };
    controlBar.onCreateTrack = [this](TrackType)
    {
        configureTrackCreationDialog();
        startupWorkflow.setVisible(true);
        startupWorkflow.showTrackCreation(true);
    };
    controlBar.onRecordRequested = [this] { toggleRecording(); };
    controlBar.onWorkspaceModeChanged = [this](WorkspaceMode mode)
    {
        if (! suppressWorkspaceModeCallback)
            requestWorkspaceMode(mode);
    };
    controlBar.onCountInChanged = [this](bool enabled) { countInEnabled = enabled; };
    controlBar.onCountInBarsChanged = [this](int bars) { countInBars = juce::jlimit(1, 2, bars); };
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
    startupWorkflow.onAudioDeviceSettingsRequested = [this]
    {
        if (onAudioDeviceSettingsRequested)
            onAudioDeviceSettingsRequested();
    };
    startupWorkflow.onAudioInputChannelSelected = [this](int channel) { selectAudioInputChannel(channel); };
    startupWorkflow.onAudioOutputChannelSelected = [this](int channel) { selectAudioOutputChannel(channel); };
    startupWorkflow.onPerformanceConfigured = [this](const LiveSetupConfig& setup)
    {
        audioEngine.setPlaybackState(false);
        enableAvailablePerformanceInputs();
        const auto result = trackDataModel.applyProjectState(ProjectTemplates::createLiveSetup(setup));
        if (result.failed())
            return;

        repairAudioTrackInputAssignments();

        // A Performance Room is immediately playable: every configured source
        // is armed, while audio sources monitor only when armed.
        const auto availableInputs = audioEngine.getActiveAudioInputChannels();
        auto nextAudioInput = 0;
        auto hasExternalMidi = false;
        for (size_t index = 0; index < trackDataModel.getTrackCount(); ++index)
        {
            const auto& track = trackDataModel.getTrack(index);
            if (track.type == TrackType::audio)
            {
                const auto input = availableInputs.isEmpty() ? 0
                    : availableInputs[(preferredStartupInputChannel + nextAudioInput++) % availableInputs.size()];
                trackDataModel.setTrackInputConfiguration(index, true, false,
                                                          input, 1);
            }
            hasExternalMidi = hasExternalMidi || track.type == TrackType::externalMidi;
            trackDataModel.setTrackArmed(index, true);
        }

        audioEngine.prepareActiveEffects();
        selectTrack(0);
        mixerPane.refreshFromModel();
        arrangeWindow.repaint();
        pianoRoll.repaint();
        markProjectSaved();
        showWorkspace();
        if (hasExternalMidi && audioEngine.getMidiOutputDeviceIdentifier().isEmpty())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                                   "MIDI Hardware needs an output",
                                                   "Your hardware MIDI tracks are ready. Choose the target device in Device Settings > MIDI OUTPUT.");
    };
    arrangeWindow.onTrackSelected = [this](int track) { selectTrack(track); };
    arrangeWindow.onAudioFileImported = [this](int track, const juce::File&)
    {
        beginBeatKeyLearning(track);
    };
    arrangeWindow.onPerformanceSetupRequested = [this]
    {
        configureTrackCreationDialog();
        startupWorkflow.setVisible(true);
        startupWorkflow.showTrackCreation(true);
    };
    arrangeWindow.onLiveModeRequested = [this] (bool enabled)
    {
        requestWorkspaceMode(enabled ? WorkspaceMode::stream : WorkspaceMode::studio);
    };
    arrangeWindow.onAudioClipSelected = [this](ClipId) {};
    arrangeWindow.onAudioClipOpenRequested = [this](ClipId)
    {
        // The inspector remains the audio-region editor until the dedicated
        // waveform editor pane is introduced; opening is deliberate, unlike selection.
        setInspectorPanelVisible(true);
    };
    arrangeWindow.onMidiClipSelected = [this](MidiClipId) {};
    arrangeWindow.onMidiClipOpenRequested = [this](MidiClipId clip)
    {
        pianoRoll.setActiveClip(clip);
        setPianoRollPanelVisible(true);
    };
    assetBrowser.onAudioFileActivated = [this](const juce::File& file)
    {
        importAudioFile(file);
    };

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

void MainComponent::requestWorkspaceMode(WorkspaceMode mode)
{
    const auto restoreActiveMode = [this]
    {
        suppressWorkspaceModeCallback = true;
        controlBar.setWorkspaceMode(workspaceModeController->getActiveMode());
        suppressWorkspaceModeCallback = false;
    };

    if (mode == WorkspaceMode::studio || ! audioEngine.needsLiveBufferOptimisation())
    {
        if (const auto result = workspaceModeController->requestMode(mode); result.failed())
        {
            restoreActiveMode();
            StudioForgeDialog::showWarning("LIVE is not ready", result.getErrorMessage());
        }
        return;
    }

    auto* device = audioEngine.getAudioDeviceManager().getCurrentAudioDevice();
    const auto current = device != nullptr ? device->getCurrentBufferSizeSamples() : 0;
    const auto recommended = audioEngine.getRecommendedLiveBufferSize();
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
                                       "Ready for LIVE?",
                                       "Your audio buffer is set to " + juce::String(current)
                                           + " samples, which may feel delayed while singing.\n\n"
                                           + "Switch to " + juce::String(recommended)
                                           + " samples for responsive live monitoring?",
                                       "Switch & enter LIVE", "Keep current setup", nullptr,
                                       juce::ModalCallbackFunction::create([safeThis, mode] (int choice)
    {
        if (safeThis == nullptr) return;
        if (choice == 0)
        {
            safeThis->suppressWorkspaceModeCallback = true;
            safeThis->controlBar.setWorkspaceMode(safeThis->workspaceModeController->getActiveMode());
            safeThis->suppressWorkspaceModeCallback = false;
            return;
        }
        if (const auto optimisation = safeThis->audioEngine.optimiseDeviceForLivePerformance(); optimisation.failed())
        {
            safeThis->suppressWorkspaceModeCallback = true;
            safeThis->controlBar.setWorkspaceMode(safeThis->workspaceModeController->getActiveMode());
            safeThis->suppressWorkspaceModeCallback = false;
            StudioForgeDialog::showWarning("Could not prepare LIVE", optimisation.getErrorMessage());
            return;
        }
        if (const auto result = safeThis->workspaceModeController->requestMode(mode); result.failed())
        {
            safeThis->suppressWorkspaceModeCallback = true;
            safeThis->controlBar.setWorkspaceMode(safeThis->workspaceModeController->getActiveMode());
            safeThis->suppressWorkspaceModeCallback = false;
            StudioForgeDialog::showWarning("LIVE is not ready", result.getErrorMessage());
        }
    }));
}

void MainComponent::requestTuneAiUrlAnalysis()
{
    const auto hostBounds = getTopLevelComponent() != nullptr ? getTopLevelComponent()->getScreenBounds()
                                                               : getScreenBounds();
    if (const auto result = tuneAiAnalysis.launchStandaloneApp(hostBounds); result.failed())
        StudioForgeDialog::showWarning("Could not open Auto Key", result.getErrorMessage());
}

juce::String MainComponent::applyTuneAiKey(const MusicalKey& key, bool useMic1, bool useMic2)
{
    juce::StringArray applied;
    juce::StringArray errors;
    if (useMic1)
    {
        const auto result = pitchCorrection.applyKey(ControlTarget::mic1, key);
        result.succeeded ? applied.add("Mic 1") : errors.add("Mic 1: " + result.error);
    }
    if (useMic2)
    {
        const auto result = pitchCorrection.applyKey(ControlTarget::mic2, key);
        result.succeeded ? applied.add("Mic 2") : errors.add("Mic 2: " + result.error);
    }
    if (! applied.isEmpty())
    {
        // An explicit Tune-AI result owns the key until the user imports a new
        // beat (which starts a new learn pass). Live detection must not chase
        // the singer and rewrite the scale during performance.
        autoKeyState = AutoKeyState::locked;
        return key.displayName() + " applied to " + applied.joinIntoString(" + ")
            + (errors.isEmpty() ? juce::String {} : " • " + errors.joinIntoString(" • "));
    }
    return errors.isEmpty() ? "No compatible pitch-correction target is configured."
                            : errors.joinIntoString(" • ");
}

void MainComponent::consumeTuneAiKeyBridge()
{
    // Tune-AI is a separately packaged app. It publishes only confirmed
    // results through an atomic JSON hand-off; all plug-in mutation remains
    // on StudioForge's control thread and therefore goes through the normal
    // routing/parameter readback path.
    const auto resultFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("studioforge-autokey-result.json");
    if (! resultFile.existsAsFile())
        return;

    const auto parsed = juce::JSON::parse(resultFile);
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr || static_cast<int>(object->getProperty("schema")) != 1)
        return;

    const auto publishedAt = static_cast<double>(object->getProperty("published_at"));
    if (! std::isfinite(publishedAt) || publishedAt <= consumedTuneAiKeyPublishedAt)
        return;

    const auto keyText = object->getProperty("key").toString().trim();
    const auto scaleText = object->getProperty("scale").toString().trim();
    const auto confidence = static_cast<float>(object->getProperty("confidence"));
    const auto key = MusicalKey::parse(keyText, scaleText);
    if (! key.has_value() || ! std::isfinite(confidence) || confidence < 0.60f)
        return;

    const auto summary = applyTuneAiKey(*key, true, true);
    consumedTuneAiKeyPublishedAt = publishedAt;
    DBG("Auto-Key bridge: " + key->displayName() + " • " + summary);
}

void MainComponent::beginBeatKeyLearning(int trackIndex)
{
    if (trackIndex < 0 || static_cast<size_t>(trackIndex) >= trackDataModel.getTrackCount())
        return;

    const auto& track = trackDataModel.getTrack(static_cast<size_t>(trackIndex));
    if (track.type != TrackType::audio)
        return;

    // Never learn harmony from the vocal that is being corrected. Imported
    // audio on any non-vocal route is treated as the rehearsal beat.
    const auto& profile = trackDataModel.getControlRoutingProfile();
    const auto isVocalRoute = std::any_of(profile.routes.begin(), profile.routes.end(), [&track] (const ControlRoute& route)
    {
        return route.effect == ControlEffect::pitchCorrection && route.track == track.id;
    });
    if (isVocalRoute)
        return;

    trackDataModel.setLiveKeyReferenceTrack(track.id);
    audioEngine.setLiveKeyReferenceTrack(track.id);
    audioEngine.resetLiveKeyDetection();
    appliedLiveKeyRevision = 0;
    autoKeyState = AutoKeyState::learning;

    // This is control-thread setup. It inserts VibeAutotune only on configured
    // vocal routes and leaves every subsequent EQ/comp/reverb insert intact.
    pitchCorrectionStatus = pitchCorrection.ensureVibeAutotuneForVocalLive();
}

void MainComponent::showInitialTrackCreation()
{
    configureTrackCreationDialog();
    startupWorkflow.setVisible(true);
    startupWorkflow.showTrackCreation(false);
}

void MainComponent::configureTrackCreationDialog()
{
    const auto* device = audioEngine.getAudioDeviceManager().getCurrentAudioDevice();
    const auto inputs = device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 0;
    const auto outputs = device != nullptr ? device->getActiveOutputChannels().countNumberOfSetBits() : 0;
    startupWorkflow.setAudioDeviceChannels(inputs, outputs);
}

void MainComponent::enableAvailablePerformanceInputs()
{
    if (audioEngine.enableAllAvailableInputChannels().wasOk())
        audioEngine.saveAudioDeviceState();
    repairAudioTrackInputAssignments();
    configureTrackCreationDialog();
}

void MainComponent::repairAudioTrackInputAssignments()
{
    const auto* device = audioEngine.getAudioDeviceManager().getCurrentAudioDevice();
    if (device == nullptr)
        return;

    const auto active = device->getActiveInputChannels();
    if (active.countNumberOfSetBits() == 0)
        return;

    juce::Array<int> availableInputs;
    for (int channel = active.findNextSetBit(0); channel >= 0; channel = active.findNextSetBit(channel + 1))
        availableInputs.add(channel);

    auto nextInput = 0;
    for (size_t index = 0; index < trackDataModel.getTrackCount(); ++index)
    {
        const auto& track = trackDataModel.getTrack(index);
        if (track.type != TrackType::audio)
            continue;

        const auto current = track.inputChannel.load(std::memory_order_relaxed);
        if (availableInputs.contains(current))
            continue;

        // Preserve the user's monitor choice. This only repairs source
        // indexes that vanished after connecting a smaller interface.
        trackDataModel.setTrackInputConfiguration(index,
            track.autoInputMonitoring.load(std::memory_order_relaxed),
            track.inputMonitoring.load(std::memory_order_relaxed),
            availableInputs[nextInput++ % availableInputs.size()], 1);
    }
}

void MainComponent::selectAudioInputChannel(int oneBasedChannel)
{
    preferredStartupInputChannel = juce::jmax(0, oneBasedChannel - 1);
    // Selecting Input 1 in the creation UI means "use Input 1 for the next
    // audio track".  It must not reconfigure a live interface to expose only
    // that channel; that tears down a Core Audio input session and is unlike
    // the track-input selectors in commercial DAWs.
    const auto selected = getSelectedTrackIndex();
    if (selected < 0 || static_cast<size_t>(selected) >= trackDataModel.getTrackCount())
        return;
    const auto& track = trackDataModel.getTrack(static_cast<size_t>(selected));
    if (track.type != TrackType::audio)
        return;
    trackDataModel.setTrackInputConfiguration(static_cast<size_t>(selected),
        track.autoInputMonitoring.load(std::memory_order_relaxed),
        track.inputMonitoring.load(std::memory_order_relaxed),
        preferredStartupInputChannel, track.inputChannelCount.load(std::memory_order_relaxed));
}

void MainComponent::selectAudioOutputChannel(int oneBasedChannel)
{
    if (audioEngine.selectStereoOutputPair(oneBasedChannel - 1).failed())
        return;
    audioEngine.saveAudioDeviceState();
    repairAudioTrackInputAssignments();
    configureTrackCreationDialog();
}

MainComponent::~MainComponent()
{
    stopTimer();
    performanceSurfaceBridge.stop();
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == &lookAndFeel)
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void MainComponent::applyWorkspaceMode(const WorkspaceProfile& profile)
{
    const auto mode = profile.mode;
    if (workspaceMode == mode)
        return;

    // The project model is never touched here. Workspace modes only alter UI
    // priority and the global audio policy; per-track monitoring remains the
    // track's own explicit state.
    if (workspaceMode == WorkspaceMode::studio)
    {
        studioPanelLayout = panelLayout;
        studioTrackHeight = trackDataModel.getTrackHeight();
    }

    workspaceMode = mode;
    arrangeWindow.setLiveModeActive(mode == WorkspaceMode::stream);
    if (mode == WorkspaceMode::studio)
    {
        trackDataModel.setTrackHeight(studioTrackHeight);
        applyPanelLayout(studioPanelLayout, false);
    }
    else if (mode == WorkspaceMode::stage)
    {
        // Stage prioritises large strips and meters; no project or mixer
        // values are reset and returning to Studio restores the snapshot.
        // The reference Stage workspace shows five concise track headers
        // above the mixer. Larger rows starve the arrangement unnecessarily.
        trackDataModel.setTrackHeight(78);
        applyPanelLayout({ false, false, true, false }, false);
    }
    else // stream
    {
        // LIVE is surfaced inside Studio. Keep the user's Studio layout and
        // track density intact while the engine applies the separate live-safe
        // routing, monitoring and latency profile.
        trackDataModel.setTrackHeight(studioTrackHeight);
        applyPanelLayout(studioPanelLayout, false);
    }

    // Both Stage and Stream are live-vocal workflows. Preparation is
    // control-thread work: it may replace a legacy hosted Vibe with the
    // StudioForge-native live processor, but can never run from audio I/O.
    if (mode == WorkspaceMode::stage || mode == WorkspaceMode::stream)
        pitchCorrectionStatus = pitchCorrection.ensureVibeAutotuneForVocalLive();
    arrangeWindow.repaint();
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    return handleGlobalKeyPress(key);
}

bool MainComponent::handleGlobalKeyPress(const juce::KeyPress& key)
{
    if (key.getModifiers().isAltDown() && key.getKeyCode() == '\'')
    {
        arrangeWindow.addMarkerAtPlayhead();
        return true;
    }
    if ((key.getKeyCode() == 'z' || key.getKeyCode() == 'Z')
        && ! key.getModifiers().isCommandDown() && ! key.getModifiers().isCtrlDown())
    {
        if (key.getModifiers().isShiftDown())
            arrangeWindow.fitTracksToWindow();
        else if (! key.getModifiers().isAnyModifierKeyDown())
            arrangeWindow.toggleSelectedTrackZoom();
        else
            return false;
        return true;
    }
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;

    if (key.isKeyCode(juce::KeyPress::spaceKey))
    {
        handleSpaceTransport();
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

void MainComponent::handleSpaceTransport()
{
    if (audioEngine.isPlaying())
    {
        controlBar.stopPlayback();
        spaceStopPendingReturn = true;
    }
    else if (spaceStopPendingReturn)
    {
        trackDataModel.setPlayheadPosition(0.0);
        spaceStopPendingReturn = false;
    }
    else
    {
        controlBar.togglePlayback();
    }
}

juce::Result MainComponent::saveProject(const juce::File& file)
{
    const auto result = ProjectSerializer::save(trackDataModel, file, &performanceMacros);
    if (result.wasOk())
        markProjectSaved();
    return result;
}

juce::Result MainComponent::saveProjectCopy(const juce::File& file) const
{
    return ProjectSerializer::save(trackDataModel, file, &performanceMacros);
}

juce::Result MainComponent::saveProjectTemplate(const juce::File& file) const
{
    return ProjectSerializer::save(trackDataModel, file, &performanceMacros);
}

juce::Result MainComponent::loadProject(const juce::File& file, juce::StringArray* missingMediaReferences)
{
    audioEngine.setPlaybackState(false);
    const auto result = ProjectSerializer::load(trackDataModel, pluginHost, file, missingMediaReferences,
                                                &performanceMacros);
    if (result.wasOk())
    {
        audioEngine.setLiveKeyReferenceTrack(trackDataModel.getLiveKeyReferenceTrack());
        if (trackDataModel.getLiveKeyReferenceTrack().isValid())
        {
            audioEngine.resetLiveKeyDetection();
            autoKeyState = AutoKeyState::learning;
            appliedLiveKeyRevision = 0;
        }
        pitchCorrectionStatus = pitchCorrection.validate();
        if (missingMediaReferences != nullptr)
            missingMediaReferences->addArray(controlBridge.validateProfile());
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

void MainComponent::startAutomatedVibeTrace()
{
    // This is a support-only launch path. It creates no persistent project,
    // does not start transport, and never enables monitoring before a real
    // Core Audio device is available.
    if (automatedVibeTraceStarted
        || audioEngine.getAudioDeviceManager().getCurrentAudioDevice() == nullptr)
        return;

    if (const auto created = createProjectFromTemplate(ProjectTemplate::audioRecording); created.failed())
        return;

    enableAvailablePerformanceInputs();
    const auto activeInputs = audioEngine.getActiveAudioInputChannels();
    if (activeInputs.isEmpty())
        return;

    // The first track is the template's Vocal route. Its effect slot remains
    // owned by PitchCorrectionIntegrationService, so no GUI click or plug-in
    // browser timing is involved.
    trackDataModel.setTrackInputConfiguration(0, true, false, activeInputs[0], 1);
    trackDataModel.setTrackArmed(0, true);
    selectTrack(0);

    // STAGE applies the explicit low-latency device profile and grants the
    // configured vocal a post-FX master monitor route. If this fails (for
    // example no usable output), leave monitoring safely off.
    if (const auto live = workspaceModeController->requestMode(WorkspaceMode::stage); live.failed())
        return;

    pitchCorrectionStatus = pitchCorrection.ensureVibeAutotuneForVocalLive();
    if (pitchCorrectionStatus.ready())
        audioEngine.prepareActiveEffects();
    audioEngine.startAudioTrace();
    audioEngine.markAudioTraceIssue();
    automatedVibeTraceStarted = true;
    mixerPane.refreshFromModel();
    arrangeWindow.repaint();
}

juce::Result MainComponent::createProjectFromTemplate(ProjectTemplate projectTemplate)
{
    audioEngine.setPlaybackState(false);
    const auto result = trackDataModel.applyProjectState(ProjectTemplates::create(projectTemplate));
    if (result.wasOk())
    {
        audioEngine.setLiveKeyReferenceTrack(trackDataModel.getLiveKeyReferenceTrack());
        pitchCorrectionStatus = pitchCorrection.validate();
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

void MainComponent::requestRecordToggle()
{
    toggleRecording();
}

void MainComponent::setInspectorPanelVisible(bool visible)
{
    auto next = panelLayout;
    next.inspector = visible;
    applyPanelLayout(next, workspaceMode == WorkspaceMode::studio);
}

void MainComponent::setWorkspacePanelsVisible(bool inspector, bool browser, bool mixer, bool pianoRollVisible)
{
    applyPanelLayout({ inspector, browser, mixer, pianoRollVisible }, workspaceMode == WorkspaceMode::studio);
}

void MainComponent::applyPanelLayout(PanelLayoutState next, bool updateStudioSnapshot)
{
    panelLayout = next;
    if (updateStudioSnapshot)
        studioPanelLayout = next;

    inspectorPane.setVisible(next.inspector);
    assetBrowser.setVisible(next.browser);
    mixerPane.setVisible(next.mixer);
    pianoRoll.setVisible(next.pianoRoll);
    controlBar.setInspectorVisible(next.inspector);
    controlBar.setBrowserVisible(next.browser);
    controlBar.setMixerVisible(next.mixer);
    controlBar.setPianoRollVisible(next.pianoRoll);
    resized();
}

void MainComponent::setBrowserPanelVisible(bool visible)
{
    auto next = panelLayout;
    next.browser = visible;
    applyPanelLayout(next, workspaceMode == WorkspaceMode::studio);
    if (visible)
        assetBrowser.focusSearch();
}

void MainComponent::setMixerPanelVisible(bool visible)
{
    auto next = panelLayout;
    next.mixer = visible;
    applyPanelLayout(next, workspaceMode == WorkspaceMode::studio);
}

void MainComponent::setPianoRollPanelVisible(bool visible)
{
    auto next = panelLayout;
    next.pianoRoll = visible;
    applyPanelLayout(next, workspaceMode == WorkspaceMode::studio);
}

bool MainComponent::addTrackFromCommand(TrackType type)
{
    const auto id = trackDataModel.addTrack(type);
    if (! id.isValid())
        return false;
    repairAudioTrackInputAssignments();
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
    requestOfflineRender("Bounce Project", StudioForgeDialog::fromUtf8("Xuất audio chưa hoàn tất"));
}

void MainComponent::requestOfflineRender(const juce::String& chooserTitle, const juce::String& errorTitle)
{
    bounceFileChooser = std::make_unique<juce::FileChooser>(chooserTitle, juce::File {}, "*.wav;*.aif;*.aiff;*.flac");
    bounceFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                       | juce::FileBrowserComponent::canSelectFiles,
        [safeOwner = juce::Component::SafePointer<MainComponent>(this), errorTitle](const juce::FileChooser& chooser)
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
                StudioForgeDialog::showWarning(errorTitle, result.getErrorMessage());
        });
}

void MainComponent::requestProjectExport()
{
    requestOfflineRender("Export Audio File", "Export audio chưa hoàn tất");
}

void MainComponent::requestStemExport()
{
    bounceFileChooser = std::make_unique<juce::FileChooser>("Export All Stems", juce::File {}, "");
    bounceFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                       | juce::FileBrowserComponent::canSelectDirectories,
        [safeOwner = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser)
        {
            if (safeOwner == nullptr) return;
            const auto directory = chooser.getResult();
            safeOwner->bounceFileChooser.reset();
            if (directory == juce::File {}) return;
            AudioEngine::OfflineRenderOptions options;
            options.sampleRate = safeOwner->trackDataModel.getSampleRate();
            const auto result = safeOwner->audioEngine.renderOfflineStems(directory, options);
            if (result.failed())
                StudioForgeDialog::showWarning("Stem export chưa hoàn tất", result.getErrorMessage());
        });
}

void MainComponent::requestSelectedTrackExport()
{
    const auto selected = getSelectedTrackIndex();
    if (! juce::isPositiveAndBelow(selected, static_cast<int>(trackDataModel.getTrackCount())))
    {
        StudioForgeDialog::showWarning("Export Selected Track", "Hãy chọn một track trước khi export.");
        return;
    }
    bounceFileChooser = std::make_unique<juce::FileChooser>("Export Selected Track", juce::File {}, "");
    bounceFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                       | juce::FileBrowserComponent::canSelectDirectories,
        [safeOwner = juce::Component::SafePointer<MainComponent>(this), selected](const juce::FileChooser& chooser)
        {
            if (safeOwner == nullptr) return;
            const auto directory = chooser.getResult();
            safeOwner->bounceFileChooser.reset();
            if (directory == juce::File {}) return;
            AudioEngine::OfflineRenderOptions options;
            options.sampleRate = safeOwner->trackDataModel.getSampleRate();
            options.trackExportFilter = [selected](size_t index) { return static_cast<int>(index) == selected; };
            const auto result = safeOwner->audioEngine.renderOfflineStems(directory, options);
            if (result.failed())
                StudioForgeDialog::showWarning("Selected track export chưa hoàn tất", result.getErrorMessage());
        });
}

void MainComponent::requestMidiExport()
{
    bounceFileChooser = std::make_unique<juce::FileChooser>("Export MIDI File", juce::File {}, "*.mid;*.midi");
    bounceFileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safeOwner = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser)
        {
            if (safeOwner == nullptr) return;
            auto output = chooser.getResult(); safeOwner->bounceFileChooser.reset();
            if (output == juce::File {}) return;
            if (output.getFileExtension().isEmpty()) output = output.withFileExtension(".mid");
            const auto state = safeOwner->trackDataModel.createProjectState();
            constexpr int ppq = 960;
            const auto sampleRate = juce::jmax(1.0, safeOwner->trackDataModel.getSampleRate());
            auto tempoMap = state.tempoMap;
            if (tempoMap.empty() || tempoMap.front().samplePosition > 0.0)
                tempoMap.insert(tempoMap.begin(), { 0.0, state.bpm });
            std::sort(tempoMap.begin(), tempoMap.end(), [] (const auto& left, const auto& right)
            {
                return left.samplePosition < right.samplePosition;
            });
            const auto samplesToTicks = [&tempoMap, sampleRate, ppq] (double samplePosition)
            {
                auto remaining = juce::jmax(0.0, samplePosition);
                auto cursor = 0.0;
                auto bpm = tempoMap.front().bpm;
                auto quarters = 0.0;
                for (const auto& event : tempoMap)
                {
                    if (event.samplePosition <= cursor)
                    {
                        bpm = event.bpm;
                        continue;
                    }
                    const auto segmentEnd = juce::jmin(remaining, event.samplePosition);
                    quarters += (segmentEnd - cursor) * bpm / (sampleRate * 60.0);
                    if (remaining <= event.samplePosition)
                        return juce::roundToInt(quarters * ppq);
                    cursor = event.samplePosition;
                    bpm = event.bpm;
                }
                quarters += (remaining - cursor) * bpm / (sampleRate * 60.0);
                return juce::roundToInt(quarters * ppq);
            };

            juce::MidiMessageSequence conductor;
            for (const auto& event : tempoMap)
            {
                const auto microsecondsPerQuarter = juce::roundToInt(60000000.0 / juce::jmax(1.0, event.bpm));
                conductor.addEvent(juce::MidiMessage::tempoMetaEvent(microsecondsPerQuarter), samplesToTicks(event.samplePosition));
            }
            auto signatures = state.timeSignatureMap;
            if (signatures.empty() || signatures.front().samplePosition > 0.0)
                signatures.insert(signatures.begin(), { 0.0, state.timeSignatureNumerator, state.timeSignatureDenominator });
            std::sort(signatures.begin(), signatures.end(), [] (const auto& left, const auto& right)
            {
                return left.samplePosition < right.samplePosition;
            });
            for (const auto& event : signatures)
                conductor.addEvent(juce::MidiMessage::timeSignatureMetaEvent(
                    juce::jmax(1, event.numerator), juce::jmax(1, event.denominator)), samplesToTicks(event.samplePosition));
            auto markers = state.markers;
            std::sort(markers.begin(), markers.end(), [] (const auto& left, const auto& right)
            {
                return left.samplePosition < right.samplePosition;
            });
            for (const auto& marker : markers)
                conductor.addEvent(juce::MidiMessage::textMetaEvent(0x06, marker.name), samplesToTicks(marker.samplePosition));

            juce::MidiFile midi;
            midi.setTicksPerQuarterNote(ppq);
            midi.addTrack(conductor);

            // Type-1 MIDI keeps each StudioForge track independent so it can be
            // edited, routed, and named correctly after import into another DAW.
            for (const auto& track : state.tracks)
            {
                juce::MidiMessageSequence sequence;
                auto hasNotes = false;
                for (const auto& clip : state.midiClips)
                {
                    if (clip.trackId != track.id)
                        continue;

                    for (const auto& note : clip.notes)
                    {
                        const auto start = samplesToTicks(clip.startSample + note.startSample);
                        const auto end = samplesToTicks(clip.startSample + note.startSample + note.durationSamples);
                        const auto channel = juce::jlimit(1, 16, note.channel);
                        sequence.addEvent(juce::MidiMessage::noteOn(channel, note.pitch, note.velocity), start);
                        sequence.addEvent(juce::MidiMessage::noteOff(channel, note.pitch), end);
                        hasNotes = true;
                    }
                }

                if (! hasNotes)
                    continue;

                // 0x03 is the Standard MIDI File Track Name meta event.
                sequence.addEvent(juce::MidiMessage::textMetaEvent(0x03, track.name), 0.0);
                sequence.updateMatchedPairs();
                midi.addTrack(sequence);
            }
            auto stream = output.createOutputStream();
            if (stream == nullptr || ! midi.writeTo(*stream))
                StudioForgeDialog::showWarning("MIDI export chưa hoàn tất", "Không thể ghi file MIDI.");
        });
}

void MainComponent::selectTrack(int trackIndex)
{
    const auto count = static_cast<int>(trackDataModel.getTrackCount());
    if (count <= 0)
    {
        selectedTrackId = {};
        audioEngine.setFocusedMonitorTrack({});
        arrangeWindow.setSelectedTrack(-1);
        return;
    }
    const auto resolvedIndex = juce::jlimit(0, count - 1, trackIndex);
    selectedTrackId = trackDataModel.getTrackId(static_cast<size_t>(resolvedIndex));
    audioEngine.setFocusedMonitorTrack(selectedTrackId);
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
        audioEngine.setCountInMetronomeActive(false);
        recordingCountdownActive = false;
        recordingPunchStopSample = -1.0;
        pendingRecordingTrack = -1;
        pendingRecordingDestination = juce::File {};
        pendingAudioRecordingTargets.clear();
        controlBar.setRecordActive(false);
        controlBar.setRecordCountdown(false);
        arrangeWindow.hideRecordingPreview();
        arrangeWindow.repaint();
        return;
    }

    // Record follows the selected channel. The previous "first armed track"
    // fallback made a take appear on an unrelated (often third) track when
    // several tracks were record-ready. Multi-track capture is only used
    // when no audio track is selected.
    const auto selectedTrack = getSelectedTrackIndex();
    const auto armedTrack = trackDataModel.getFirstArmedTrackIndex();
    const auto target = selectedTrack >= 0 ? selectedTrack : armedTrack;
    if (target < 0 || target >= static_cast<int>(trackDataModel.getTrackCount()))
        return;

    const auto type = trackDataModel.getTrack(static_cast<size_t>(target)).type;
    if (type != TrackType::audio && type != TrackType::instrument && type != TrackType::externalMidi)
        return;
    std::vector<AudioEngine::AudioRecordingTarget> audioTargets;
    if (type == TrackType::audio)
    {
        const auto destination = createRecordingDestination();
        if (destination == juce::File {})
            return;
        audioTargets.push_back({ static_cast<size_t>(target), destination });
    }

    if (! audioTargets.empty())
    {
        if (const auto validation = audioEngine.validateRecordingTargets(audioTargets); validation.failed())
        {
            showRecordingFailure(validation);
            return;
        }

        if (audioEngine.isPlaying())
        {
            audioEngine.clearRecordingCaptureRange();
            startAudioRecordingsNow(audioTargets, trackDataModel.getPlayheadPosition());
            return;
        }

        if (trackDataModel.isPunchActive())
        {
            const auto punchIn = trackDataModel.getPunchInSample();
            const auto punchOut = trackDataModel.getPunchOutSample();
            audioEngine.setRecordingCaptureRange(punchIn, punchOut);
            recordingPunchStopSample = punchOut;
            audioEngine.setPlaybackState(true);
            startAudioRecordingsNow(audioTargets, punchIn);
            return;
        }

        audioEngine.clearRecordingCaptureRange();
        if (! countInEnabled)
        {
            startAudioRecordingsNow(audioTargets);
            return;
        }

        const auto samplesPerBeat = trackDataModel.getSampleRate() * 60.0 / trackDataModel.getBpm();
        pendingRecordingStartSample = trackDataModel.getPlayheadPosition()
            + samplesPerBeat * trackDataModel.getTimeSignatureNumerator() * countInBars;
        pendingAudioRecordingTargets = std::move(audioTargets);
        recordingCountdownActive = true;
        audioEngine.setCountInMetronomeActive(true);
        audioEngine.setPlaybackState(true);
        controlBar.setRecordCountdown(true);
        return;
    }

    const auto destination = type == TrackType::audio ? createRecordingDestination() : juce::File {};
    if (type == TrackType::audio && destination == juce::File {})
        return;

    if (audioEngine.isPlaying())
    {
        audioEngine.clearRecordingCaptureRange();
        startRecordingNow(target, destination, trackDataModel.getPlayheadPosition());
        return;
    }

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
        + samplesPerBeat * trackDataModel.getTimeSignatureNumerator() * countInBars;
    pendingRecordingTrack = target;
    pendingRecordingDestination = destination;
    recordingCountdownActive = true;
    audioEngine.setCountInMetronomeActive(true);
    audioEngine.setPlaybackState(true);
    controlBar.setRecordCountdown(true);
}

void MainComponent::startRecordingNow(int trackIndex, const juce::File& destination,
                                      double timelineStartSample)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(trackDataModel.getTrackCount()))
        return;
    const auto type = trackDataModel.getTrack(static_cast<size_t>(trackIndex)).type;
    const auto recordingStartSample = timelineStartSample >= 0.0
        ? timelineStartSample : trackDataModel.getPlayheadPosition();
    const auto result = type == TrackType::audio
        ? audioEngine.startRecording(static_cast<size_t>(trackIndex), destination, recordingStartSample)
        : audioEngine.startMidiRecording(static_cast<size_t>(trackIndex));
    if (result.wasOk())
    {
        audioEngine.setPlaybackState(true);
        controlBar.setRecordActive(true);
        arrangeWindow.showRecordingPreview(trackIndex, recordingStartSample);
    }
    else
    {
        showRecordingFailure(result);
    }
}

void MainComponent::startAudioRecordingsNow(const std::vector<AudioEngine::AudioRecordingTarget>& targets,
                                            double timelineStartSample)
{
    if (targets.empty())
        return;

    const auto recordingStartSample = timelineStartSample >= 0.0
        ? timelineStartSample : trackDataModel.getPlayheadPosition();
    const auto result = audioEngine.startRecordings(targets, recordingStartSample);
    if (result.wasOk())
    {
        audioEngine.setPlaybackState(true);
        controlBar.setRecordActive(true);
        arrangeWindow.showRecordingPreview(static_cast<int>(targets.front().trackIndex), recordingStartSample);
    }
    else
    {
        showRecordingFailure(result);
    }
}

void MainComponent::showRecordingFailure(const juce::Result& result) const
{
    StudioForgeDialog::showWarning("Recording unavailable",
                                   "Cannot start recording. " + result.getErrorMessage()
                                       + "\n\nCheck File > Audio Device Settings and each Track Input.");
}

void MainComponent::timerCallback()
{
    consumeTuneAiKeyBridge();
    // Device scans never run on the audio thread. A one-second refresh gives
    // USB MIDI controllers plug-and-play behaviour without polling per frame.
    if (++midiDeviceRefreshTicks >= 30)
    {
        midiDeviceRefreshTicks = 0;
        audioEngine.refreshMidiInputDevices();
    }

    // Registry scanning is asynchronous at startup. Retry setup only while it
    // is unresolved, on the UI timer, never as a side effect of detected keys.
    if ((workspaceMode == WorkspaceMode::stage || workspaceMode == WorkspaceMode::stream)
        && ! pitchCorrectionStatus.ready()
        && ++pitchCorrectionSetupTicks >= 30)
    {
        pitchCorrectionSetupTicks = 0;
        pitchCorrectionStatus = pitchCorrection.ensureVibeAutotuneForVocalLive();
    }

    if (audioEngine.consumeRecordingInterruptedByDeviceDisconnect())
    {
        // Never finalise a take from the device callback. The UI thread owns
        // the orderly close so unplugging an interface cannot leave a writer
        // live against a vanished input stream.
        audioEngine.stopRecording();
        recordingCountdownActive = false;
        recordingPunchStopSample = -1.0;
        pendingRecordingTrack = -1;
        pendingRecordingDestination = juce::File {};
        pendingAudioRecordingTargets.clear();
        controlBar.setRecordActive(false);
        controlBar.setRecordCountdown(false);
        arrangeWindow.hideRecordingPreview();
        arrangeWindow.repaint();
        StudioForgeDialog::showWarning("Audio interface disconnected",
                                       "Recording stopped safely. Reconnect the interface, verify track inputs, then record again.");
    }

    if (autoKeyState == AutoKeyState::learning)
    {
        const auto key = audioEngine.getLiveKeyResult();
        if (key.revision != 0 && key.revision != appliedLiveKeyRevision && key.confidence >= 0.60f)
        {
            const MusicalKey semanticKey { static_cast<PitchClass>(key.root), key.minor ? MusicalScale::minor : MusicalScale::major };
            const auto summary = applyTuneAiKey(semanticKey, true, true);
            appliedLiveKeyRevision = key.revision;
            if (autoKeyState == AutoKeyState::locked)
            {
                // End of rehearsal pass: return to the song start with the key
                // frozen. The next Play is the actual singing/performance pass.
                audioEngine.setPlaybackState(false);
                trackDataModel.setPlayheadPosition(0.0);
                StudioForgeDialog::showWarning("Key locked for performance",
                    semanticKey.displayName() + " • " + summary
                    + "\nRewound to the beginning. Press Play and sing; Auto-Key will not overwrite this setting.");
            }
        }
    }

    if (recordingCountdownActive && ! audioEngine.isPlaying())
    {
        recordingCountdownActive = false;
        pendingRecordingTrack = -1;
        pendingRecordingDestination = juce::File {};
        pendingAudioRecordingTargets.clear();
        audioEngine.setCountInMetronomeActive(false);
        controlBar.setRecordCountdown(false);
        return;
    }

    if (audioEngine.isRecording() && recordingPunchStopSample >= 0.0
        && trackDataModel.getPlayheadPosition() >= recordingPunchStopSample)
    {
        audioEngine.stopRecording();
        recordingPunchStopSample = -1.0;
        controlBar.setRecordActive(false);
        arrangeWindow.hideRecordingPreview();
        arrangeWindow.repaint();
        return;
    }

    if (! recordingCountdownActive || trackDataModel.getPlayheadPosition() < pendingRecordingStartSample)
        return;

    const auto track = pendingRecordingTrack;
    const auto destination = pendingRecordingDestination;
    auto audioTargets = std::move(pendingAudioRecordingTargets);
    recordingCountdownActive = false;
    audioEngine.setCountInMetronomeActive(false);
    pendingRecordingTrack = -1;
    pendingRecordingDestination = juce::File {};
    controlBar.setRecordCountdown(false);
    if (! audioTargets.empty())
        startAudioRecordingsNow(audioTargets);
    else if (track >= 0)
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
    controlBar.setBounds(area.removeFromTop(64));

    // Dock panes from a bounded budget so opening both lower editors never
    // collapses the arrange workspace or overlaps controls on smaller screens.
    const auto dockBudget = juce::jmax(0, area.getHeight() - 220);
    const auto requestedDockHeight = (mixerPane.isVisible() ? mixerPane.getPreferredHeight() : 0)
                                   + (pianoRoll.isVisible() ? 250 : 0);
    const auto dockScale = requestedDockHeight > 0
        ? juce::jmin(1.0f, static_cast<float>(dockBudget) / static_cast<float>(requestedDockHeight)) : 0.0f;
    if (mixerPane.isVisible())
        mixerPane.setBounds(area.removeFromBottom(juce::roundToInt(static_cast<float>(mixerPane.getPreferredHeight()) * dockScale)));
    if (pianoRoll.isVisible())
        pianoRoll.setBounds(area.removeFromBottom(juce::roundToInt(250.0f * dockScale)));

    const auto sideBudget = juce::jmax(0, area.getWidth() - 440);
    // Keep the Inspector's channel strips at a stable working width. A wide
    // display should reveal more arrangement space, not stretch plugin slots
    // and mixer controls into a different visual system.
    const auto inspectorTargetWidth = juce::jlimit(250, 310,
                                                    juce::roundToInt(static_cast<float>(getWidth()) * 0.175f));
    const auto browserTargetWidth = juce::jlimit(236, 266, juce::roundToInt(static_cast<float>(getWidth()) * 0.156f));
    const auto requestedSideWidth = (inspectorPane.isVisible() ? inspectorTargetWidth : 0)
                                  + (assetBrowser.isVisible() ? browserTargetWidth : 0);
    const auto sideScale = requestedSideWidth > 0
        ? juce::jmin(1.0f, static_cast<float>(sideBudget) / static_cast<float>(requestedSideWidth)) : 0.0f;
    if (inspectorPane.isVisible())
        inspectorPane.setBounds(area.removeFromLeft(juce::roundToInt(static_cast<float>(inspectorTargetWidth) * sideScale)));
    if (assetBrowser.isVisible())
        assetBrowser.setBounds(area.removeFromRight(juce::roundToInt(static_cast<float>(browserTargetWidth) * sideScale)));

    arrangeWindow.setBounds(area);
    startupWorkflow.setBounds(getLocalBounds());
}
