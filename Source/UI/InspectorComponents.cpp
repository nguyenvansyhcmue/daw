#include "InspectorComponents.h"

#include "../AudioEngine/AudioEngine.h"
#include "../AudioEngine/GainUtilityProcessor.h"
#include "../Midi/MidiTransposeProcessor.h"
#include "../Plugins/PluginHostService.h"
#include "PluginBrowserPanel.h"
#include "Theme/StudioForgeLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int sectionHeaderHeight = 16;
constexpr int controlHeight = StudioForgeTheme::UIMetrics::compactControlHeight;
constexpr int stripPadding = 4;
constexpr float silentGainThreshold = 0.0001f;

struct PluginBrowserLayout final
{
    static constexpr int width = PluginBrowserPanel::preferredWidth;
    static constexpr int height = PluginBrowserPanel::preferredHeight;
    static constexpr int screenX = 150;
    static constexpr int screenY = 120;
};

void configureSectionLabel(juce::Label& label)
{
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, StudioForgeTheme::secondaryText);
    label.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
}

juce::String sendLevelText(float gain)
{
    if (gain <= silentGainThreshold)
        return "-inf dB";
    return juce::String(juce::Decibels::gainToDecibels(gain), 1) + " dB";
}

juce::String sendLevelPercent(float gain)
{
    return juce::String(juce::roundToInt(juce::jmax(0.0f, gain) * 100.0f)) + "%";
}

juce::String sendBusLabel(const TrackDataModel::BusState& bus, size_t busIndex)
{
    const auto ordinal = "Bus " + juce::String(static_cast<int>(busIndex + 1));
    // Keep the technical destination visible, but lead with the name the
    // musician assigned to the return (for example: "Vocal Verb").
    return bus.name.isNotEmpty() ? ordinal + " / " + bus.name : ordinal;
}

class PluginEditorHost final : public juce::Component
{
public:
    PluginEditorHost(std::shared_ptr<AudioEffectProcessor> ownerIn, std::unique_ptr<juce::Component> editorIn)
        : owner(std::move(ownerIn)), editor(std::move(editorIn))
    {
        if (editor != nullptr)
            addAndMakeVisible(*editor);
    }

    void resized() override
    {
        if (editor != nullptr)
            editor->setBounds(getLocalBounds());
    }
    void childBoundsChanged(juce::Component* child) override
    {
        if (child == editor.get() && (child->getWidth() != getWidth() || child->getHeight() != getHeight()))
            setSize(juce::jmax(1, child->getWidth()), juce::jmax(1, child->getHeight()));
    }

private:
    std::shared_ptr<AudioEffectProcessor> owner;
    std::unique_ptr<juce::Component> editor;
};

class FloatingPluginEditorWindow final : public juce::DocumentWindow
{
public:
    using CloseCallback = std::function<void(FloatingPluginEditorWindow*)>;

    FloatingPluginEditorWindow(const juce::String& title, juce::Component* content,
                               juce::Component* componentToCentreAround, CloseCallback closeCallbackIn)
        : juce::DocumentWindow(title, juce::Colour(0xff25282d), juce::DocumentWindow::closeButton, true),
          closeCallback(std::move(closeCallbackIn))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(content, true);
        setSize(content->getWidth(), content->getHeight());
        // The plug-in controls its preferred editor size. Letting both the
        // host window and the plug-in editor negotiate live resizing caused
        // unsafe repaint/resize churn on macOS compositor threads.
        setResizable(false, false);
        centreAroundComponent(componentToCentreAround, getWidth(), getHeight());
        setVisible(true);
        toFront(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);

        // Defer destruction until DocumentWindow has completed its close
        // callback. The strip owns this window, so no self-deletion occurs.
        if (closeCallback != nullptr)
            juce::MessageManager::callAsync([callback = closeCallback, window = this] { callback(window); });
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        // Plugin editors are separate native windows, so the DAW's main
        // component does not receive its transport shortcut while this
        // window is focused. Keep Space global at the host boundary without
        // stealing any other key from the plug-in editor.
        if (key.isKeyCode(juce::KeyPress::spaceKey) && ! key.getModifiers().isAnyModifierKeyDown())
        {
            if (togglePlayback != nullptr)
                togglePlayback();
            return true;
        }
        return juce::DocumentWindow::keyPressed(key);
    }

    std::function<void()> togglePlayback;

private:
    CloseCallback closeCallback;
};
}

RegionInspectorComponent::RegionInspectorComponent(TrackDataModel& model) : trackModel(model)
{
    configureSectionLabel(heading);
    context.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    context.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::plain)));
    for (auto* label : { &timelineDetails, &modifierDetails })
    {
        label->setColour(juce::Label::textColourId, StudioForgeTheme::secondaryText.withAlpha(0.82f));
        label->setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::plain)));
        label->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(*label);
    }
    addAndMakeVisible(heading);
    addAndMakeVisible(context);
    clearSelection();
}

void RegionInspectorComponent::setSelectedAudioClip(ClipId clip)
{
    selectedAudioClip = clip;
    selectedMidiClip = {};
    applyViewState(InspectorViewStateBuilder::makeAudioRegion(trackModel, clip));
}

void RegionInspectorComponent::setSelectedMidiClip(MidiClipId clip)
{
    selectedAudioClip = {};
    selectedMidiClip = clip;
    applyViewState(InspectorViewStateBuilder::makeMidiRegion(trackModel, clip));
}

void RegionInspectorComponent::clearSelection()
{
    selectedAudioClip = {};
    selectedMidiClip = {};
    applyViewState(InspectorViewStateBuilder::makeEmptyRegion());
}

void RegionInspectorComponent::applyViewState(const InspectorRegionViewState& state)
{
    context.setText(state.title, juce::dontSendNotification);
    timelineDetails.setText(state.timelineDetails, juce::dontSendNotification);
    modifierDetails.setText(state.modifierDetails, juce::dontSendNotification);
}

void RegionInspectorComponent::paint(juce::Graphics& g)
{
    g.setColour(StudioForgeTheme::panelBackground);
    g.fillRect(getLocalBounds());
    g.setColour(StudioForgeTheme::separator);
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
}

void RegionInspectorComponent::resized()
{
    auto bounds = getLocalBounds().reduced(stripPadding, 3);
    heading.setBounds(bounds.removeFromTop(sectionHeaderHeight));
    context.setBounds(bounds.removeFromTop(18));
    timelineDetails.setBounds(bounds.removeFromTop(16));
    modifierDetails.setBounds(bounds.removeFromTop(16));
}

TrackInspectorComponent::TrackInspectorComponent(TrackDataModel& model, AudioEngine& engine)
    : trackModel(model), audioEngine(engine)
{
    configureSectionLabel(heading);
    trackName.setEditable(true, true, false);
    trackName.setJustificationType(juce::Justification::centredLeft);
    trackName.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.92f));
    trackName.onTextChange = [this]
    {
        if (selectedTrack >= 0 && selectedTrack < static_cast<int>(trackModel.getTrackCount()))
            trackModel.setTrackName(static_cast<size_t>(selectedTrack), trackName.getText());
    };
    const std::array<std::pair<juce::ToggleButton*, const char*>, 4> buttons {{
        { &recordEnable, "Record enable" }, { &inputMonitoring, "Input monitoring" },
        { &mute, "Mute" }, { &solo, "Solo" }
    }};
    for (const auto& [button, tooltip] : buttons)
    {
        button->setTooltip(tooltip);
        addAndMakeVisible(*button);
    }
    // Keep the compact mute control unambiguous. Its one-character label must
    // never be replaced by a text ellipsis on macOS.
    mute.setButtonText("M");
    inputMonitoring.setButtonText("I");
    recordEnable.onClick = [this] { if (selectedTrack >= 0) trackModel.setTrackArmed(static_cast<size_t>(selectedTrack), recordEnable.getToggleState()); };
    inputMonitoring.onClick = [this]
    {
        if (selectedTrack < 0)
            return;
        const auto index = static_cast<size_t>(selectedTrack);
        const auto& track = trackModel.getTrack(index);
        const auto isDirectMonitor = track.inputMonitoring.load(std::memory_order_relaxed);
        if (isDirectMonitor)
        {
            // The compact I switch is a performer-facing direct monitor
            // control: it must never leave the user in an inaudible Auto
            // state while transport is stopped. Auto remains available in
            // Device Settings for users who explicitly want record-arm logic.
            trackModel.setTrackInputConfiguration(index, false, false,
                                                   track.inputChannel.load(std::memory_order_relaxed),
                                                   track.inputChannelCount.load(std::memory_order_relaxed));
            audioEngine.disableTrackMonitor(index);
        }
        else
        {
            trackModel.setTrackInputConfiguration(index, false, true,
                                                   track.inputChannel.load(std::memory_order_relaxed),
                                                   track.inputChannelCount.load(std::memory_order_relaxed));
            auto result = audioEngine.enableTrackMonitor(index, MonitorDestination::master);
            if (result.failed())
            {
                trackModel.setTrackMonitorPermission(index, false);
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Microphone monitor blocked", result.getErrorMessage());
            }
        }
        refresh();
    };
    monitorStatus.setJustificationType(juce::Justification::centredLeft);
    monitorStatus.setColour(juce::Label::textColourId, StudioForgeTheme::secondaryText);
    monitorStatus.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::plain)));
    addAndMakeVisible(monitorStatus);
    mute.onClick = [this] { if (selectedTrack >= 0) trackModel.setTrackMuted(static_cast<size_t>(selectedTrack), mute.getToggleState()); };
    solo.onClick = [this] { if (selectedTrack >= 0) trackModel.setTrackSolo(static_cast<size_t>(selectedTrack), solo.getToggleState()); };
    soloSafe.onClick = [this] { if (selectedTrack >= 0) trackModel.setTrackSoloSafe(static_cast<size_t>(selectedTrack), soloSafe.getToggleState()); };
    for (auto* label : { &inputDetails, &outputDetails })
    {
        label->setColour(juce::Label::textColourId, StudioForgeTheme::secondaryText);
        label->setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::plain)));
        label->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(*label);
    }
    addAndMakeVisible(heading);
    addAndMakeVisible(trackName);
    startTimerHz(30);
}

void TrackInspectorComponent::setSelectedTrack(int trackIndex)
{
    selectedTrack = trackIndex;
    refresh();
}

void TrackInspectorComponent::refresh()
{
    const auto state = InspectorViewStateBuilder::makeTrack(trackModel, selectedTrack);
    trackName.setEnabled(state.isSelected);
    for (auto* button : { &recordEnable, &inputMonitoring, &mute, &solo, &soloSafe }) button->setEnabled(state.isSelected);
    if (! state.isSelected)
    {
        applyViewState(state);
        return;
    }
    const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
    trackName.setText(track.name, juce::dontSendNotification);
    recordEnable.setToggleState(track.armed.load(), juce::dontSendNotification);
    inputMonitoring.setToggleState(track.inputMonitoring.load() || track.autoInputMonitoring.load(), juce::dontSendNotification);
    inputMonitoring.setEnabled(state.supportsInputMonitoring);
    const auto monitorTooltip = track.inputMonitoring.load() ? "Input monitoring: In → Master. Click to turn it Off."
        : track.autoInputMonitoring.load() ? "Input monitoring: Auto when armed. Click I for direct In → Master."
        : "Input monitoring: Off. Click I for direct In → Master.";
    inputMonitoring.setTooltip(monitorTooltip);
    const auto directMonitorReady = track.inputMonitoring.load(std::memory_order_relaxed)
        && track.monitorPermitted.load(std::memory_order_relaxed)
        && track.monitorDestination.load(std::memory_order_relaxed) == MonitorDestination::master
        && audioEngine.isSoftwareMonitoringEnabled();
    auto anotherTrackIsSoloed = false;
    for (size_t index = 0; index < trackModel.getTrackCount(); ++index)
        if (static_cast<int>(index) != selectedTrack
            && trackModel.getTrack(index).solo.load(std::memory_order_relaxed))
        {
            anotherTrackIsSoloed = true;
            break;
        }
    const auto assignedInput = track.inputChannel.load(std::memory_order_relaxed);
    const auto inputEnabled = audioEngine.getActiveAudioInputChannels().contains(assignedInput);
    const auto hasInputSignal = audioEngine.getTrackInputPeak(static_cast<size_t>(selectedTrack)) > 0.0015f;
    const auto monitoringText = ! directMonitorReady
        ? (track.inputMonitoring.load() ? "MIC: IN (ROUTE BLOCKED)"
           : track.autoInputMonitoring.load() ? "MIC: AUTO (WHEN ARMED)" : "MIC: OFF")
        : ! inputEnabled ? juce::String("MIC: INPUT ") + juce::String(assignedInput + 1) + " DISABLED"
        : track.muted.load(std::memory_order_relaxed) ? "MIC: IN (TRACK MUTED)"
        : anotherTrackIsSoloed && ! track.solo.load(std::memory_order_relaxed) ? "MIC: IN (SOLO BLOCKED)"
        : audioEngine.isMasterMuted() ? "MIC: IN (MASTER MUTED)"
        : audioEngine.isPanicMuteEnabled() ? "MIC: IN (PANIC MUTE)"
        : hasInputSignal ? "MIC: SIGNAL → MASTER" : "MIC: IN → MASTER (WAITING INPUT)";
    monitorStatus.setText(monitoringText, juce::dontSendNotification);
    mute.setToggleState(track.muted.load(), juce::dontSendNotification);
    solo.setToggleState(track.solo.load(), juce::dontSendNotification);
    soloSafe.setToggleState(track.soloSafe.load(), juce::dontSendNotification);
    applyViewState(state);
}

void TrackInspectorComponent::applyViewState(const InspectorTrackViewState& state)
{
    trackName.setText(state.name, juce::dontSendNotification);
    inputDetails.setText(state.inputDetails, juce::dontSendNotification);
    outputDetails.setText(state.outputDetails, juce::dontSendNotification);
}

void TrackInspectorComponent::timerCallback()
{
    // Other mixer surfaces write TrackState directly; this small selected view
    // mirrors that authoritative state without owning a parallel copy.
    refresh();
}

void TrackInspectorComponent::paint(juce::Graphics& g)
{
    g.setColour(StudioForgeTheme::panelBackground);
    g.fillRect(getLocalBounds());
    g.setColour(StudioForgeTheme::separator);
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
}

void TrackInspectorComponent::resized()
{
    auto bounds = getLocalBounds().reduced(stripPadding, 3);
    heading.setBounds(bounds.removeFromTop(sectionHeaderHeight));
    auto row = bounds.removeFromTop(controlHeight);
    auto buttons = row.removeFromRight(4 * controlHeight);
    trackName.setBounds(row.reduced(1, 0));
    for (auto* button : { &recordEnable, &inputMonitoring, &mute, &solo }) button->setBounds(buttons.removeFromLeft(controlHeight).reduced(1));
    inputDetails.setBounds(bounds.removeFromTop(16));
    outputDetails.setBounds(bounds.removeFromTop(16));
    monitorStatus.setBounds(bounds.removeFromTop(16));
}

ChannelStripComponent::ChannelStripComponent(ChannelRole role, TrackDataModel& model, AudioEngine& engine, PluginHostService& host)
    : channelRole(role), trackModel(model), audioEngine(engine), pluginHost(host)
{
    heading.setText(isSelectedTrackStrip() ? "CHANNEL STRIP" : "STEREO OUT", juce::dontSendNotification);
    fxHeading.setText(isSelectedTrackStrip() ? "AUDIO FX" : "MASTER FX", juce::dontSendNotification);
    configureSectionLabel(heading);
    configureSectionLabel(fxHeading);
    configureSectionLabel(sendsHeading);
    configureSectionLabel(routingHeading);
    configureSectionLabel(panLabel);
    configureSectionLabel(levelLabel);
    channelSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff202a30));
    channelSelector.setColour(juce::ComboBox::textColourId, StudioForgeTheme::primaryText);
    channelSelector.setTooltip("Choose Stereo Out or an Aux channel to inspect");
    addAndMakeVisible(channelSelector);
    channelSelector.setVisible(! isSelectedTrackStrip());
    channelSelector.onChange = [this]
    {
        const auto item = channelSelector.getSelectedId();
        const auto& buses = trackModel.getBuses();
        const auto index = item - 2;
        if (onRevealAuxBus != nullptr && item == 1) onRevealAuxBus({});
        else if (onRevealAuxBus != nullptr && index >= 0 && index < static_cast<int>(buses.size()))
            onRevealAuxBus(buses[static_cast<size_t>(index)].id);
    };
    sendSummary.setTooltip("Manage aux sends for the selected track");
    sendSummary.onClick = [this] { showSendMenu(); };
    addAndMakeVisible(sendSummary);
    for (size_t slot = 0; slot < audioFxSlots.size(); ++slot)
    {
        auto& button = audioFxSlots[slot];
        button.setTooltip("Click to insert a plug-in. On an occupied slot, click centre to open or hover left to bypass.");
        button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff303b42));
        button.setColour(juce::TextButton::textColourOffId, StudioForgeTheme::primaryText.withAlpha(0.88f));
        button.onContextMenu = [this, slot]
        {
            const auto rack = activeFxRack();
            if (rack != nullptr && rack->processors[slot] != nullptr)
                showAudioFxMenu(slot);
            else
                showPluginBrowser(slot);
        };
        button.onBypassClick = [this, slot]
        {
            const auto rack = activeFxRack();
            if (rack == nullptr || rack->processors[slot] == nullptr)
                return;
            if (isSelectedTrackStrip())
                audioEngine.setFxBypassed(static_cast<size_t>(selectedTrack), slot, ! rack->bypass[slot]);
            else if (isAuxBusStrip())
                trackModel.setBusFxBypassed(selectedBus, slot, ! rack->bypass[slot]);
            else
                audioEngine.setMasterFxBypassed(slot, ! rack->bypass[slot]);
            refresh();
        };
        button.onClick = [this, slot]
        {
            const auto rack = activeFxRack();
            const auto processor = rack != nullptr ? rack->processors[slot] : nullptr;
            if (processor == nullptr)
            {
                showPluginBrowser(slot);
                return;
            }
            showPluginEditor(slot);
        };
        addAndMakeVisible(button);
    }
    configureSectionLabel(sceneHeading);
    addAndMakeVisible(sceneHeading);
    for (size_t scene = 0; scene < sceneButtons.size(); ++scene)
    {
        sceneButtons[scene].onClick = [this, scene]
        {
            if (selectedTrack >= 0)
                trackModel.recallFxScene(static_cast<size_t>(selectedTrack), scene);
            refresh();
        };
        addAndMakeVisible(sceneButtons[scene]);
    }
    captureScene.onClick = [this]
    {
        if (selectedTrack >= 0)
            trackModel.captureFxScene(static_cast<size_t>(selectedTrack), {});
        refresh();
    };
    captureScene.setButtonText("SAVE +");
    captureScene.setTooltip("Save the current FX bypass state as a Scene");
    captureScene.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff283b42));
    addAndMakeVisible(captureScene);
    addAudioFx.onClick = [this]
    {
        const auto rack = activeFxRack();
        for (size_t slot = 0; rack != nullptr && slot < rack->processors.size(); ++slot)
            if (rack->processors[slot] == nullptr) { showPluginBrowser(slot); return; }
    };
    addAudioFx.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff283b42));
    addAndMakeVisible(addAudioFx);
    outputRoute.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff20252c));
    outputRoute.setColour(juce::ComboBox::textColourId, juce::Colours::white.withAlpha(0.86f));
    addAndMakeVisible(outputRoute);
    outputRoute.onChange = [this]
    {
        if (selectedTrack >= 0) trackModel.setTrackOutputBus(trackModel.getTrackId(static_cast<size_t>(selectedTrack)), busForMenuItem(outputRoute.getSelectedId()));
    };
    for (size_t slot = 0; slot < sendRoutes.size(); ++slot)
    {
        auto& route = sendRoutes[slot];
        auto& level = sendLevels[slot];
        auto& preFader = sendPreFader[slot];
        auto& clear = clearSendButtons[slot];
        route.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff20252c));
        route.setColour(juce::ComboBox::textColourId, juce::Colours::white.withAlpha(0.86f));
        route.setTextWhenNothingSelected("No Send");
        route.setTooltip("Click the name to view its Aux; click the arrow to change the destination.");
        route.onChange = [this, slot] { setSendRoute(slot); };
        route.onReveal = [this, slot]
        {
            const auto bus = busForMenuItem(sendRoutes[slot].getSelectedId());
            if (bus.isValid() && onRevealAuxBus != nullptr)
                onRevealAuxBus(bus);
        };
        level.setRange(0.0, TrackDataModel::maximumSendGain, 0.001);
        level.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        level.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        level.setDoubleClickReturnValue(true, 1.0);
        level.setMouseDragSensitivity(220);
        level.setSkewFactorFromMidPoint(1.0);
        level.setColour(juce::Slider::rotarySliderFillColourId, StudioForgeTheme::meterCyan);
        level.setTooltip("Send level: 0.0 dB (double-click resets)");
        level.onValueChange = [this, slot]
        {
            sendLevels[slot].setTooltip("Send level: " + sendLevelText(static_cast<float>(sendLevels[slot].getValue()))
                                        + " (double-click resets to 0.0 dB)");
            sendLevelReadouts[slot].setText(sendLevelPercent(static_cast<float>(sendLevels[slot].getValue())),
                                             juce::dontSendNotification);
            setSendRoute(slot);
        };
        auto& readout = sendLevelReadouts[slot];
        readout.setJustificationType(juce::Justification::centred);
        readout.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        readout.setColour(juce::Label::textColourId, StudioForgeTheme::meterCyan.withAlpha(0.92f));
        readout.setTooltip("Send gain as a percentage: 100% = 0 dB");
        preFader.setButtonText("POST");
        preFader.setTooltip("Choose send position");
        preFader.onClick = [this, slot]
        {
            if (selectedTrack < 0 || selectedTrack >= static_cast<int>(trackModel.getTrackCount())) return;
            const auto trackId = trackModel.getTrackId(static_cast<size_t>(selectedTrack));
            const auto& send = trackModel.getTrack(static_cast<size_t>(selectedTrack)).sends[slot];
            const auto bus = send.targetBus;
            if (! bus.isValid()) return;
            juce::PopupMenu menu;
            menu.addItem(1, "Pre Fader", true, send.preFader);
            menu.addItem(2, "Post Fader", true, send.postFaderBeforePan);
            menu.addItem(3, "Post Pan", true, ! send.preFader && ! send.postFaderBeforePan);
            const auto safe = juce::Component::SafePointer<ChannelStripComponent>(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&sendPreFader[slot]),
                [safe, trackId, bus, slot] (int choice)
            {
                if (safe == nullptr || choice < 1 || choice > 3) return;
                auto& model = safe->trackModel;
                const auto index = model.getTrackIndex(trackId);
                if (index < 0) return;
                const auto& current = model.getTrack(static_cast<size_t>(index)).sends[slot];
                if (current.targetBus != bus) return;
                if (choice == 1)
                    model.setTrackSendRoute(trackId, slot, bus, current.level, true);
                else
                    model.setTrackSendPostFaderBeforePan(trackId, slot, choice == 2);
                safe->refreshRouting();
            });
        };
        clear.setButtonText("X");
        clear.setTooltip("Remove send");
        clear.onClick = [this, slot] { clearSendRoute(slot); };
        addAndMakeVisible(route);
        addAndMakeVisible(level);
        addAndMakeVisible(readout);
        addAndMakeVisible(preFader);
        addAndMakeVisible(clear);
        route.setVisible(false);
        level.setVisible(false);
        readout.setVisible(false);
        preFader.setVisible(false);
        clear.setVisible(false);
    }
    pan.setRange(-1.0, 1.0, 0.01);
    pan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    pan.setMouseDragSensitivity(220);
    pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pan.textFromValueFunction = [] (double value)
    {
        return juce::String(value, 2);
    };
    fader.setRange(0.0, AudioGainRange::maximumChannelGain, 0.01);
    // Unity gain must be at the centre of the physical fader, as on a console.
    fader.setSkewFactorFromMidPoint(1.0);
    fader.setSliderStyle(juce::Slider::LinearVertical);
    fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
    fader.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff11191d));
    fader.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    fader.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffd5e8ed));
    fader.textFromValueFunction = [] (double value)
    {
        if (value <= 0.0001)
            return juce::String("-inf dB");
        return juce::String(juce::Decibels::gainToDecibels(static_cast<float>(value)), 1) + " dB";
    };
    fader.valueFromTextFunction = [] (const juce::String& text)
    {
        if (text.containsIgnoreCase("inf"))
            return 0.0;
        return static_cast<double>(juce::Decibels::decibelsToGain(text.retainCharacters("-0123456789.").getFloatValue()));
    };
    panValue.setJustificationType(juce::Justification::centred);
    panValue.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    panValue.setColour(juce::Label::backgroundColourId, juce::Colour(0xff11191d));
    panValue.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    panValue.setColour(juce::Label::textColourId, juce::Colour(0xffd5e8ed));
    panValue.setText("0.00", juce::dontSendNotification);
    pan.onValueChange = [this]
    {
        panValue.setText(juce::String(pan.getValue(), 2), juce::dontSendNotification);
        if (isSelectedTrackStrip() && selectedTrack >= 0) trackModel.setTrackPan(static_cast<size_t>(selectedTrack), static_cast<float>(pan.getValue()));
        else if (isAuxBusStrip()) trackModel.setBusBalance(selectedBus, static_cast<float>(pan.getValue()));
        else if (! isSelectedTrackStrip()) audioEngine.setMasterPan(static_cast<float>(pan.getValue()));
    };
    fader.onValueChange = [this]
    {
        if (isSelectedTrackStrip() && selectedTrack >= 0) trackModel.setTrackVolume(static_cast<size_t>(selectedTrack), static_cast<float>(fader.getValue()));
        else if (isAuxBusStrip()) trackModel.setBusGain(selectedBus, static_cast<float>(fader.getValue()));
        else audioEngine.setMasterGain(static_cast<float>(fader.getValue()));
    };
    for (auto* component : { static_cast<juce::Component*>(&heading), static_cast<juce::Component*>(&fxHeading), static_cast<juce::Component*>(&sendsHeading), static_cast<juce::Component*>(&routingHeading), static_cast<juce::Component*>(&panLabel), static_cast<juce::Component*>(&levelLabel), static_cast<juce::Component*>(&panValue), static_cast<juce::Component*>(&pan), static_cast<juce::Component*>(&fader), static_cast<juce::Component*>(&meter) }) addAndMakeVisible(*component);
    startTimerHz(30);
}

void ChannelStripComponent::setSelectedTrack(int trackIndex) { selectedTrack = trackIndex; refresh(); }

void ChannelStripComponent::setAuxBus(BusId bus)
{
    selectedBus = bus;
    channelRole = bus.isValid() ? ChannelRole::auxBus : ChannelRole::stereoOutput;
    refresh();
}

void ChannelStripComponent::setViewedAux(BusId bus)
{
    viewedAux = bus;
    if (isSelectedTrackStrip()) refreshRouting();
}

const TrackDataModel::FxRackSnapshot* ChannelStripComponent::activeFxRack() const noexcept
{
    if (isAuxBusStrip())
        return trackModel.getBusFxRackSnapshot(selectedBus);

    if (! isSelectedTrackStrip())
        return audioEngine.getMasterFxRackSnapshot();

    if (selectedTrack < 0 || selectedTrack >= static_cast<int>(trackModel.getTrackCount()))
        return nullptr;

    return trackModel.getFxRackSnapshot(static_cast<size_t>(selectedTrack));
}

bool ChannelStripComponent::insertAudioFx(size_t slot, std::shared_ptr<AudioEffectProcessor> processor,
                                           int targetTrack)
{
    if (processor == nullptr || slot >= TrackDataModel::maxFxSlots)
        return false;

    if (isSelectedTrackStrip())
    {
        const auto trackIndex = targetTrack >= 0 ? targetTrack : selectedTrack;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(trackModel.getTrackCount()))
            return false;
        audioEngine.setFxProcessor(static_cast<size_t>(trackIndex), slot, std::move(processor));
    }
    else if (isAuxBusStrip())
    {
        if (! selectedBus.isValid())
            return false;
        trackModel.setBusFxProcessor(selectedBus, slot, std::move(processor));
    }
    else
    {
        audioEngine.setMasterFxProcessor(slot, std::move(processor));
    }

    return true;
}

void ChannelStripComponent::refreshChannelSelector()
{
    if (isSelectedTrackStrip()) return;
    const auto& buses = trackModel.getBuses();
    auto changed = channelSelector.getNumItems() != static_cast<int>(buses.size() + 1);
    for (size_t i = 0; ! changed && i < buses.size(); ++i)
        changed = channelSelector.getItemText(static_cast<int>(i + 1))
            != "Aux " + juce::String(static_cast<int>(i + 1)) + " / " + buses[i].name;
    if (changed)
    {
        channelSelector.clear(juce::dontSendNotification);
        channelSelector.addItem("Stereo Out", 1);
        for (size_t i = 0; i < buses.size(); ++i)
            channelSelector.addItem("Aux " + juce::String(static_cast<int>(i + 1)) + " / " + buses[i].name,
                                    static_cast<int>(i + 2));
    }
    int selected = 1;
    for (size_t i = 0; i < buses.size(); ++i)
        if (buses[i].id == selectedBus) selected = static_cast<int>(i + 2);
    channelSelector.setSelectedId(selected, juce::dontSendNotification);
}

void ChannelStripComponent::refresh()
{
    refreshChannelSelector();
    if (isAuxBusStrip())
    {
        const auto& buses = trackModel.getBuses();
        const auto found = std::find_if(buses.begin(), buses.end(), [this] (const auto& bus) { return bus.id == selectedBus; });
        if (found == buses.end())
        {
            setAuxBus({});
            return;
        }
        heading.setText(("AUX " + juce::String(static_cast<int>(std::distance(buses.begin(), found)) + 1)
                         + " / " + found->name).toUpperCase(), juce::dontSendNotification);
        // The legacy renderer currently sums an unassigned return into the
        // master mix. Do not present that implementation detail as an output
        // assignment: an Aux output must become a first-class routing choice.
        fxHeading.setText("FX RETURN INSERTS", juce::dontSendNotification);
        fader.setValue(found->gain, juce::dontSendNotification);
        pan.setValue(found->balance, juce::dontSendNotification);
        panValue.setText(juce::String(found->balance, 2), juce::dontSendNotification);
        refreshAudioFx();
        refreshFxScenes();
        return;
    }
    if (! isSelectedTrackStrip())
    {
        heading.setText("STEREO OUT", juce::dontSendNotification);
        fxHeading.setText("MASTER FX", juce::dontSendNotification);
        fader.setValue(audioEngine.getMasterGain(), juce::dontSendNotification);
        pan.setValue(audioEngine.getMasterPan(), juce::dontSendNotification);
        panValue.setText(juce::String(audioEngine.getMasterPan(), 2), juce::dontSendNotification);
        refreshAudioFx();
        refreshFxScenes();
        return;
    }
    const auto active = selectedTrack >= 0 && selectedTrack < static_cast<int>(trackModel.getTrackCount());
    for (auto* component : { static_cast<juce::Component*>(&addAudioFx), static_cast<juce::Component*>(&sendSummary), static_cast<juce::Component*>(&outputRoute), static_cast<juce::Component*>(&pan), static_cast<juce::Component*>(&fader) }) component->setEnabled(active);
    for (size_t slot = 0; slot < sendRoutes.size(); ++slot)
    {
        sendRoutes[slot].setEnabled(active);
        sendLevels[slot].setEnabled(active);
        sendPreFader[slot].setEnabled(active);
        clearSendButtons[slot].setEnabled(active);
    }
    if (! active)
    {
        heading.setText("NO TRACK SELECTED", juce::dontSendNotification);
        fxHeading.setText("AUDIO FX", juce::dontSendNotification);
        refreshSendSummary();
        refreshFxScenes();
        return;
    }
    const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
    const auto trackName = track.name.isNotEmpty() ? track.name : "Track " + juce::String(selectedTrack + 1);
    heading.setText(trackName.toUpperCase(), juce::dontSendNotification);
    fxHeading.setText(trackName.toUpperCase() + " FX", juce::dontSendNotification);
    sendsHeading.setText("FX SENDS", juce::dontSendNotification);
    fader.setValue(track.volume.load(), juce::dontSendNotification);
    pan.setValue(track.pan.load(), juce::dontSendNotification);
    refreshAudioFx();
    refreshFxScenes();
    refreshRouting();
    refreshSendSummary();
}

void ChannelStripComponent::refreshFxScenes()
{
    const auto active = isSelectedTrackStrip() && selectedTrack >= 0;
    sceneHeading.setVisible(active);
    const auto* scenes = active ? &trackModel.getFxScenes(static_cast<size_t>(selectedTrack)) : nullptr;
    for (size_t index = 0; index < sceneButtons.size(); ++index)
    {
        const auto available = scenes != nullptr && index < scenes->size();
        sceneButtons[index].setVisible(available);
        if (available)
            sceneButtons[index].setButtonText((*scenes)[index].name);
    }
    captureScene.setVisible(active && scenes != nullptr && scenes->size() < sceneButtons.size());
}

void ChannelStripComponent::refreshAudioFx()
{
    const auto rack = activeFxRack();

    constexpr size_t readyToAddSlots = 4;
    size_t visibleSlots = readyToAddSlots;
    if (rack != nullptr)
        for (size_t slot = 0; slot < rack->processors.size(); ++slot)
            if (rack->processors[slot] != nullptr)
                visibleSlots = juce::jmin(rack->processors.size(), slot + 2);

    for (size_t slot = 0; slot < audioFxSlots.size(); ++slot)
    {
        const auto processor = rack != nullptr ? rack->processors[slot] : nullptr;
        auto& button = audioFxSlots[slot];
        const auto bypassed = processor != nullptr && rack->bypass[slot];
        button.setBypassControlState(processor != nullptr, bypassed);
        button.setVisible(slot < visibleSlots);
        auto label = processor == nullptr ? juce::String {} : processor->getName();
        auto activeColour = juce::Colour(0xff276776);
        auto activeTooltip = juce::String("Click centre to open. Hover the left edge to bypass; right-click for Replace or Remove.");
        if (processor != nullptr && processor->isStudioForgeNativeVibeAutotune() && ! bypassed)
        {
            const auto health = processor->getLiveProcessorHealth();
            switch (health.state)
            {
                case LiveProcessorState::priming:
                    label << "  ·  PRIMING";
                    activeColour = juce::Colour(0xff8b692d);
                    activeTooltip = "PRIMING — direct mic safety path is active";
                    break;
                case LiveProcessorState::tracking:
                    label << "  ·  TRACKING";
                    activeColour = juce::Colour(0xff23806f);
                    activeTooltip = "TRACKING — tuned renderer is healthy";
                    break;
                case LiveProcessorState::holding:
                    label << "  ·  HOLD";
                    activeColour = juce::Colour(0xff3d7b85);
                    activeTooltip = "HOLD — retaining corrected continuity through a brief pitch dropout";
                    break;
                case LiveProcessorState::protectedDry:
                    label << "  ·  PROTECTED";
                    activeColour = juce::Colour(0xff765147);
                    activeTooltip = "PROTECTED — renderer fault/overload; latency-aligned dry mic remains live";
                    break;
                case LiveProcessorState::ready:
                case LiveProcessorState::unavailable:
                    label << "  ·  READY";
                    activeTooltip = "READY — waiting for a confident voiced pitch";
                    break;
            }
        }
        button.setButtonText(label);
        button.setColour(juce::TextButton::buttonColourId,
                         processor == nullptr ? juce::Colour(0xff303b42)
                                              : bypassed ? juce::Colour(0xff252a2e)
                                                         : activeColour);
        button.setColour(juce::TextButton::textColourOffId,
                         processor == nullptr ? StudioForgeTheme::primaryText.withAlpha(0.45f)
                                              : bypassed ? StudioForgeTheme::primaryText.withAlpha(0.48f)
                                                         : juce::Colour(0xffeffcff));
        button.setTooltip(processor == nullptr ? "Click to insert a plug-in"
            : bypassed ? "BYPASSED — click centre to open; hover the left edge to enable."
                       : activeTooltip);
    }
    addAudioFx.setVisible(visibleSlots < audioFxSlots.size());
    resized();
}

void ChannelStripComponent::refreshRouting()
{
    outputRoute.clear(juce::dontSendNotification);
    routeBusIds.fill({});
    outputRoute.addItem("Stereo Out", 1);
    for (auto& route : sendRoutes)
    {
        route.clear(juce::dontSendNotification);
        route.addItem("No Send", 1);
    }
    if (selectedTrack < 0) return;
    const auto& buses = trackModel.getBuses();
    for (size_t index = 0; index < buses.size() && index < routeBusIds.size(); ++index)
    {
        routeBusIds[index] = buses[index].id;
        const auto item = static_cast<int>(index + 2);
        outputRoute.addItem("AUX " + juce::String(static_cast<int>(index + 1)) + " / " + buses[index].name, item);
        for (auto& route : sendRoutes)
            route.addItem(sendBusLabel(buses[index], index), item);
    }
    const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
    int output = 1;
    for (size_t index = 0; index < buses.size() && index < routeBusIds.size(); ++index)
    {
        if (track.outputBus == routeBusIds[index]) output = static_cast<int>(index + 2);
    }
    outputRoute.setSelectedId(output, juce::dontSendNotification);
    for (size_t slot = 0; slot < sendRoutes.size(); ++slot)
    {
        const auto active = track.sends[slot].targetBus.isValid();
        // Set a selection for every visible route, after the full bus list is
        // built. Previously an active send could paint as an unlabelled arrow
        // while its level and POST switch remained visible.
        auto routeItem = 1;
        if (active)
            for (size_t busIndex = 0; busIndex < buses.size() && busIndex < routeBusIds.size(); ++busIndex)
                if (track.sends[slot].targetBus == routeBusIds[busIndex])
                {
                    routeItem = static_cast<int>(busIndex + 2);
                    break;
                }
        sendRoutes[slot].setSelectedId(routeItem, juce::dontSendNotification);
        sendRoutes[slot].setColour(juce::ComboBox::backgroundColourId,
            active && track.sends[slot].targetBus == viewedAux ? juce::Colour(0xff31555d) : juce::Colour(0xff20252c));
        sendLevels[slot].setValue(active ? track.sends[slot].level : 0.0f, juce::dontSendNotification);
        sendLevels[slot].setTooltip("Send level: " + sendLevelText(active ? track.sends[slot].level : 0.0f)
                                    + " (double-click resets to 0.0 dB)");
        sendLevelReadouts[slot].setText(sendLevelPercent(active ? track.sends[slot].level : 0.0f),
                                         juce::dontSendNotification);
        sendPreFader[slot].setToggleState(active && track.sends[slot].preFader, juce::dontSendNotification);
        sendRoutes[slot].setVisible(active);
        sendLevels[slot].setVisible(active);
        sendLevelReadouts[slot].setVisible(active);
        sendPreFader[slot].setButtonText(track.sends[slot].preFader ? "PRE" : track.sends[slot].postFaderBeforePan ? "FDR" : "PAN");
        sendPreFader[slot].setTooltip((track.sends[slot].preFader ? juce::String("Pre Fader")
            : track.sends[slot].postFaderBeforePan ? juce::String("Post Fader, before Pan") : juce::String("Post Pan")) + ". Click to change.");
        sendRoutes[slot].setTooltip(active && routeItem >= 2
            ? sendBusLabel(buses[static_cast<size_t>(routeItem - 2)], static_cast<size_t>(routeItem - 2))
                + " (send destination)"
            : "No send destination");
        sendPreFader[slot].setVisible(active);
        clearSendButtons[slot].setVisible(active);
    }
    resized();
}

void ChannelStripComponent::setSendRoute(size_t slot)
{
    if (selectedTrack < 0 || slot >= sendRoutes.size())
        return;
    const auto bus = busForMenuItem(sendRoutes[slot].getSelectedId());
    const auto id = trackModel.getTrackId(static_cast<size_t>(selectedTrack));
    if (bus.isValid())
        trackModel.setTrackSendRoute(id, slot, bus, static_cast<float>(sendLevels[slot].getValue()), sendPreFader[slot].getToggleState());
    else
        clearSendRoute(slot);
}

void ChannelStripComponent::clearSendRoute(size_t slot)
{
    if (selectedTrack < 0 || slot >= sendRoutes.size())
        return;

    const auto id = trackModel.getTrackId(static_cast<size_t>(selectedTrack));
    trackModel.clearTrackSendRoute(id, slot);
    refreshRouting();
    refreshSendSummary();
}

void ChannelStripComponent::refreshSendSummary()
{
    if (! isSelectedTrackStrip() || selectedTrack < 0 || selectedTrack >= static_cast<int>(trackModel.getTrackCount()))
    {
        sendSummary.setButtonText("+ Add FX Send");
        return;
    }

    const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
    if (track.activeSendCount == 0)
    {
        sendSummary.setButtonText("+ Add FX Send");
        sendSummary.setTooltip("Create an FX Bus and send this track to it");
    }
    else
    {
        sendSummary.setButtonText("+ Add FX Send");
        sendSummary.setTooltip("Add another FX Send or manage existing sends");
    }
}

void ChannelStripComponent::showSendMenu()
{
    if (! isSelectedTrackStrip() || selectedTrack < 0 || selectedTrack >= static_cast<int>(trackModel.getTrackCount()))
        return;

    const auto trackId = trackModel.getTrackId(static_cast<size_t>(selectedTrack));
    const auto& buses = trackModel.getBuses();
    juce::PopupMenu menu;
    const auto canAddSend = trackModel.getTrack(static_cast<size_t>(selectedTrack)).activeSendCount < TrackDataModel::maxSendsPerTrack;
    menu.addItem(1, "New FX Bus...", canAddSend);
    if (! buses.empty())
    {
        juce::PopupMenu addMenu;
        for (size_t bus = 0; bus < buses.size(); ++bus)
            addMenu.addItem(static_cast<int>(100 + bus), sendBusLabel(buses[bus], bus));
        menu.addSubMenu("Send to existing FX Bus", addMenu, canAddSend);
        menu.addSeparator();

        const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
        constexpr std::array<float, 5> levels { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        for (size_t slot = 0; slot < TrackDataModel::maxSendsPerTrack; ++slot)
        {
            if (! track.sends[slot].targetBus.isValid())
                continue;
            juce::PopupMenu slotMenu;
            slotMenu.addItem(static_cast<int>(5000 + slot), "Open Aux / Add Plug-in");
            slotMenu.addItem(static_cast<int>(6000 + slot), "Rename Bus...");
            slotMenu.addSeparator();
            juce::PopupMenu destinationMenu;
            for (size_t bus = 0; bus < buses.size(); ++bus)
                destinationMenu.addItem(static_cast<int>(1000 + slot * 100 + bus),
                                        sendBusLabel(buses[bus], bus),
                                        true, track.sends[slot].targetBus == buses[bus].id);
            slotMenu.addSubMenu("Destination", destinationMenu);

            juce::PopupMenu levelMenu;
            for (size_t level = 0; level < levels.size(); ++level)
                levelMenu.addItem(static_cast<int>(2000 + slot * 100 + level),
                                  juce::String(juce::roundToInt(levels[level] * 100.0f)) + "%",
                                  true, std::abs(track.sends[slot].level - levels[level]) < 0.005f);
            slotMenu.addSubMenu("Level", levelMenu);
            juce::PopupMenu tapMenu;
            tapMenu.addItem(static_cast<int>(3000 + slot), "Pre Fader", true, track.sends[slot].preFader);
            tapMenu.addItem(static_cast<int>(7000 + slot), "Post Fader", true, track.sends[slot].postFaderBeforePan);
            tapMenu.addItem(static_cast<int>(8000 + slot), "Post Pan", true, ! track.sends[slot].preFader && ! track.sends[slot].postFaderBeforePan);
            slotMenu.addSubMenu("Send Position", tapMenu);
            slotMenu.addSeparator();
            slotMenu.addItem(static_cast<int>(4000 + slot), "Remove Send");
            menu.addSubMenu("Send " + juce::String(static_cast<int>(slot + 1)), slotMenu);
        }
    }

    const auto safeOwner = juce::Component::SafePointer<ChannelStripComponent>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&sendSummary),
                       [safeOwner, trackId] (int choice)
    {
        if (safeOwner == nullptr || choice == 0)
            return;
        auto& model = safeOwner->trackModel;
        if (choice == 1)
        {
            const auto bus = model.addBus();
            const auto trackIndex = model.getTrackIndex(trackId);
            if (bus.isValid() && trackIndex >= 0)
            {
            const auto& track = model.getTrack(static_cast<size_t>(trackIndex));
            const auto slot = std::find_if(track.sends.begin(), track.sends.end(),
                                           [] (const auto& send) { return ! send.targetBus.isValid(); });
            if (slot != track.sends.end())
                model.setTrackSendRoute(trackId, static_cast<size_t>(std::distance(track.sends.begin(), slot)), bus, 1.0f, false);
            }
        }
        else if (choice >= 100 && choice < 1000)
        {
            const auto busIndex = static_cast<size_t>(choice - 100);
            const auto& buses = model.getBuses();
            const auto trackIndex = model.getTrackIndex(trackId);
            if (trackIndex >= 0 && busIndex < buses.size())
            {
                const auto& track = model.getTrack(static_cast<size_t>(trackIndex));
                const auto slot = std::find_if(track.sends.begin(), track.sends.end(),
                                               [] (const auto& send) { return ! send.targetBus.isValid(); });
                if (slot != track.sends.end())
                    model.setTrackSendRoute(trackId, static_cast<size_t>(std::distance(track.sends.begin(), slot)), buses[busIndex].id, 1.0f, false);
            }
        }
        else if (choice >= 1000 && choice < 2000)
        {
            const auto encoded = choice - 1000;
            const auto slot = static_cast<size_t>(encoded / 100);
            const auto busIndex = static_cast<size_t>(encoded % 100);
            const auto& buses = model.getBuses();
            const auto trackIndex = model.getTrackIndex(trackId);
            if (trackIndex >= 0 && busIndex < buses.size())
            {
                const auto& track = model.getTrack(static_cast<size_t>(trackIndex));
                if (slot < TrackDataModel::maxSendsPerTrack && track.sends[slot].targetBus.isValid())
                    model.setTrackSendRoute(trackId, slot, buses[busIndex].id, track.sends[slot].level, track.sends[slot].preFader);
            }
        }
        else if (choice >= 2000 && choice < 3000)
        {
            constexpr std::array<float, 5> levels { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
            const auto encoded = choice - 2000;
            const auto slot = static_cast<size_t>(encoded / 100);
            const auto level = static_cast<size_t>(encoded % 100);
            const auto trackIndex = model.getTrackIndex(trackId);
            if (trackIndex >= 0 && level < levels.size())
            {
                const auto& track = model.getTrack(static_cast<size_t>(trackIndex));
                if (slot < TrackDataModel::maxSendsPerTrack && track.sends[slot].targetBus.isValid())
                    model.setTrackSendRoute(trackId, slot, track.sends[slot].targetBus, levels[level], track.sends[slot].preFader);
            }
        }
        else if (choice >= 3000 && choice < 4000)
        {
            const auto slot = static_cast<size_t>(choice - 3000);
            const auto trackIndex = model.getTrackIndex(trackId);
            if (trackIndex >= 0)
            {
                const auto& track = model.getTrack(static_cast<size_t>(trackIndex));
                if (slot < TrackDataModel::maxSendsPerTrack && track.sends[slot].targetBus.isValid())
                    model.setTrackSendRoute(trackId, slot, track.sends[slot].targetBus, track.sends[slot].level, true);
            }
        }
        else if (choice >= 4000 && choice < 5000)
            model.clearTrackSendRoute(trackId, static_cast<size_t>(choice - 4000));
        else if (choice >= 7000 && choice < 9000)
            model.setTrackSendPostFaderBeforePan(trackId, static_cast<size_t>(choice - (choice >= 8000 ? 8000 : 7000)), choice < 8000);
        else if (choice >= 5000 && choice < 7000)
        {
            const auto rename = choice >= 6000;
            const auto slot = static_cast<size_t>(choice - (rename ? 6000 : 5000));
            const auto trackIndex = model.getTrackIndex(trackId);
            if (trackIndex >= 0 && slot < TrackDataModel::maxSendsPerTrack)
            {
                const auto bus = model.getTrack(static_cast<size_t>(trackIndex)).sends[slot].targetBus;
                const auto& buses = model.getBuses();
                const auto found = std::find_if(buses.begin(), buses.end(), [bus] (const auto& item) { return item.id == bus; });
                if (found != buses.end() && ! rename && safeOwner->onRevealAuxBus != nullptr)
                    safeOwner->onRevealAuxBus(bus);
                else if (found != buses.end() && rename)
                {
                    auto* dialog = new juce::AlertWindow("Rename Bus", "This name is shared by all sends to this bus.", juce::MessageBoxIconType::NoIcon);
                    dialog->addTextEditor("name", found->name, "Bus name");
                    dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
                    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safeOwner, dialog, bus] (int result)
                    {
                        if (result == 1 && safeOwner != nullptr)
                        {
                            const auto name = dialog->getTextEditorContents("name").trim();
                            if (name.isNotEmpty())
                            {
                                safeOwner->trackModel.setBusName(bus, name);
                                safeOwner->refreshRouting();
                                if (safeOwner->onRevealAuxBus != nullptr)
                                    safeOwner->onRevealAuxBus(bus);
                            }
                        }
                    }), true);
                }
            }
        }
        safeOwner->refreshRouting();
        safeOwner->refreshSendSummary();
    });
}

void ChannelStripComponent::showAudioFxMenu(size_t slot)
{
    if (isSelectedTrackStrip() && selectedTrack < 0) return;
    const auto rack = activeFxRack();
    const auto occupied = rack != nullptr && rack->processors[slot] != nullptr;
    const auto bypassed = occupied && rack->bypass[slot];
    juce::PopupMenu menu;
    menu.addItem(3, bypassed ? "Enable Plug-in" : "Bypass Plug-in", occupied);
    menu.addItem(6, "Open Plug-in Editor", occupied && rack->processors[slot]->hasEditor());
    menu.addSeparator();
    menu.addItem(4, occupied ? "Replace Plug-in..." : "Choose Plug-in...");
    menu.addItem(1, "Remove Plug-in", occupied);
    menu.addSeparator();
    menu.addItem(2, "Replace with Gain Utility");
    menu.addItem(5, "Scan VST3 Plug-in File...");
    const auto canAddMidiFx = isSelectedTrackStrip() && selectedTrack >= 0
        && trackModel.getTrack(static_cast<size_t>(selectedTrack)).type != TrackType::audio;
    menu.addItem(7, "Replace with MIDI Transpose (+12)", canAddMidiFx);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&audioFxSlots[slot]), [this, slot] (int choice)
    {
        if (choice == 1)
        {
            if (isSelectedTrackStrip()) audioEngine.setFxProcessor(static_cast<size_t>(selectedTrack), slot, nullptr);
            else if (isAuxBusStrip()) trackModel.setBusFxProcessor(selectedBus, slot, nullptr);
            else audioEngine.setMasterFxProcessor(slot, nullptr);
        }
        else if (choice == 2)
        {
            insertAudioFx(slot, std::make_shared<GainUtilityProcessor>());
        }
        else if (choice == 3)
        {
            const auto rack = activeFxRack();
            if (rack != nullptr)
            {
                if (isSelectedTrackStrip()) audioEngine.setFxBypassed(static_cast<size_t>(selectedTrack), slot, !rack->bypass[slot]);
                else if (isAuxBusStrip()) trackModel.setBusFxBypassed(selectedBus, slot, !rack->bypass[slot]);
                else audioEngine.setMasterFxBypassed(slot, !rack->bypass[slot]);
            }
        }
        else if (choice == 4)
        {
            showPluginBrowser(slot);
            return;
        }
        else if (choice == 5)
        {
            pluginFileChooser = std::make_unique<juce::FileChooser>("Load VST3 plug-in", juce::File {}, "*.vst3");
            pluginFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                [this, slot] (const juce::FileChooser& chooser)
                {
                    const auto file = chooser.getResult();
                    pluginFileChooser.reset();
                    if (! file.exists())
                    {
                        refresh();
                        return;
                    }

                    const juce::Component::SafePointer<ChannelStripComponent> safeThis(this);
                    pluginHost.scanVst3Async(file, [safeThis, slot](juce::Result result)
                    {
                        if (safeThis == nullptr)
                            return;

                        if (result.wasOk())
                            safeThis->showPluginBrowser(slot);
                        else
                            safeThis->refresh();
                    });
                });
            return;
        }
        else if (choice == 6)
        {
            showPluginEditor(slot);
            return;
        }
        else if (choice == 7 && isSelectedTrackStrip())
        {
            insertAudioFx(slot, std::make_shared<MidiTransposeProcessor>(12));
        }
        refresh();
    });
}

void ChannelStripComponent::showPluginBrowser(size_t slot)
{
    // Selection is a view concern. Capture the destination TrackId now, so
    // changing selection while the browser is open can never move an FX to a
    // different channel.
    const auto destinationIsMaster = channelRole == ChannelRole::stereoOutput;
    const auto destinationBus = isAuxBusStrip() ? selectedBus : BusId {};
    const auto destinationTrackId = isSelectedTrackStrip() && selectedTrack >= 0
        ? trackModel.getTrackId(static_cast<size_t>(selectedTrack)) : TrackId {};
    if (! destinationIsMaster && ! destinationBus.isValid() && ! destinationTrackId.isValid())
        return;

    auto* panel = new PluginBrowserPanel(pluginHost, [safeThis = juce::Component::SafePointer<ChannelStripComponent>(this), slot, destinationIsMaster, destinationBus, destinationTrackId](const juce::PluginDescription& description)
    {
        if (safeThis == nullptr)
            return;

        auto& owner = *safeThis;
        juce::String error;
        auto* device = owner.audioEngine.getAudioDeviceManager().getCurrentAudioDevice();
        const auto sampleRate = device != nullptr ? device->getCurrentSampleRate() : owner.trackModel.getSampleRate();
        const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
        if (auto effect = owner.pluginHost.createEffect(description, sampleRate, blockSize, error))
        {
            const auto shouldOpenEditor = effect->hasEditor();
            auto inserted = false;
            if (destinationIsMaster)
                inserted = owner.insertAudioFx(slot, std::move(effect));
            else if (destinationBus.isValid())
                inserted = owner.insertAudioFx(slot, std::move(effect));
            else if (const auto destinationIndex = owner.trackModel.getTrackIndex(destinationTrackId); destinationIndex >= 0)
                inserted = owner.insertAudioFx(slot, std::move(effect), destinationIndex);

            if (inserted)
            {
                owner.refresh();
                if (shouldOpenEditor)
                    juce::MessageManager::callAsync([safeThis, slot]
                    {
                        if (safeThis != nullptr)
                            safeThis->showPluginEditor(slot);
                    });
                return;
            }
        }
        owner.refresh();
    });
    // A browse dialog must open at a useful working size. Without this, JUCE
    // derives its size from the unlaid-out content and produces a tiny dialog.
    panel->setSize(PluginBrowserLayout::width, PluginBrowserLayout::height);

    auto& anchor = audioFxSlots[slot];
    // Keep the transient menu inside the DAW's native peer. A desktop-level
    // callout (a null parent) can receive the tail of the originating macOS
    // click as an outside-click and dismiss before it is ever painted.
    auto* ownerWindow = getTopLevelComponent();
    auto& callout = juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(panel),
                                                            anchor.getScreenBounds(), ownerWindow);
    callout.setDismissalMouseClicksAreAlwaysConsumed(true);
}

void ChannelStripComponent::showPluginEditor(size_t slot)
{
    // A native JUCE editor creates an NSWindow. Creating that peer while the
    // originating slot is still handling mouse-down/up leaves AppKit routing
    // the final event to a stale peer on macOS. Post it after the gesture.
    const juce::Component::SafePointer<ChannelStripComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis, slot]
    {
        if (safeThis != nullptr)
            safeThis->openPluginEditor(slot);
    });
}

void ChannelStripComponent::openPluginEditor(size_t slot)
{
    const auto* rack = isSelectedTrackStrip()
        ? (selectedTrack >= 0 ? trackModel.getFxRackSnapshot(static_cast<size_t>(selectedTrack)) : nullptr)
        : isAuxBusStrip() ? trackModel.getBusFxRackSnapshot(selectedBus)
                          : audioEngine.getMasterFxRackSnapshot();
    if (rack == nullptr || slot >= rack->processors.size())
        return;

    const auto processor = rack->processors[slot];
    if (processor == nullptr)
        return;

    if (! processor->hasEditor())
        return;

    // Keep a short, low-overhead audio evidence trail around an editor open.
    // This lets support distinguish UI focus failures from a DSP/device fault.
    audioEngine.startAudioTrace();
    if (isSelectedTrackStrip() && selectedTrack >= 0)
        audioEngine.selectPluginTraceTarget(static_cast<size_t>(selectedTrack), slot);
    else
        audioEngine.clearPluginTraceTarget();
    audioEngine.markAudioTraceIssue();
    auto editor = processor->createEditor();
    if (editor == nullptr)
        return;

    const auto editorBounds = editor->getBounds();
    auto* host = new PluginEditorHost(processor, std::move(editor));
    // Geometry is supplied by the editor itself, never a product-name branch.
    const auto editorWidth = juce::jmax(320, editorBounds.getWidth());
    const auto editorHeight = juce::jmax(220, editorBounds.getHeight());
    host->setSize(editorWidth, editorHeight);
    const juce::Component::SafePointer<ChannelStripComponent> safeOwner(this);
    auto closeWindow = [safeOwner](FloatingPluginEditorWindow* window)
    {
        if (safeOwner == nullptr)
            return;

        auto& windows = safeOwner->pluginEditorWindows;
        windows.erase(std::remove_if(windows.begin(), windows.end(), [window](const auto& ownedWindow)
        {
            return ownedWindow.get() == window;
        }), windows.end());
    };
    pluginEditorWindows.push_back(std::make_unique<FloatingPluginEditorWindow>(processor->getName(), host, this,
                                                                                 std::move(closeWindow)));
    if (auto* window = dynamic_cast<FloatingPluginEditorWindow*>(pluginEditorWindows.back().get()))
        window->togglePlayback = [this]
    {
        audioEngine.setPlaybackState(! audioEngine.isPlaying());
    };
}

BusId ChannelStripComponent::busForMenuItem(int itemId) const noexcept
{
    const auto index = itemId - 2;
    return index >= 0 && index < static_cast<int>(routeBusIds.size()) ? routeBusIds[static_cast<size_t>(index)] : BusId {};
}

void ChannelStripComponent::timerCallback()
{
    refreshChannelSelector();
    if (isSelectedTrackStrip() && selectedTrack >= 0
        && selectedTrack < static_cast<int>(trackModel.getTrackCount()))
    {
        const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
        const auto useInputMeter = track.type == TrackType::audio
            && (track.armed.load(std::memory_order_relaxed)
                || track.inputMonitoring.load(std::memory_order_relaxed)
                || track.autoInputMonitoring.load(std::memory_order_relaxed)
                || audioEngine.isLivePerformanceEnabled());
        meter.updatePeak(useInputMeter ? audioEngine.getTrackInputPeak(static_cast<size_t>(selectedTrack))
                                       : audioEngine.getTrackPeak(static_cast<size_t>(selectedTrack)));
    }
    else if (isAuxBusStrip())
    {
        const auto& buses = trackModel.getBuses();
        const auto found = std::find_if(buses.begin(), buses.end(), [this] (const auto& bus) { return bus.id == selectedBus; });
        meter.updatePeak(found != buses.end() ? audioEngine.getBusPeak(static_cast<size_t>(std::distance(buses.begin(), found))) : 0.0f);
    }
    else
        meter.updateStereoPeak(audioEngine.getMasterLeftPeak(), audioEngine.getMasterRightPeak());
    synchroniseControlsFromState();
}

void ChannelStripComponent::synchroniseControlsFromState()
{
    if (isAuxBusStrip())
    {
        const auto& buses = trackModel.getBuses();
        const auto found = std::find_if(buses.begin(), buses.end(), [this] (const auto& bus) { return bus.id == selectedBus; });
        if (found != buses.end())
        {
            fader.setValue(found->gain, juce::dontSendNotification);
            pan.setValue(found->balance, juce::dontSendNotification);
            panValue.setText(juce::String(found->balance, 2), juce::dontSendNotification);
        }
        return;
    }
    if (! isSelectedTrackStrip())
    {
        fader.setValue(audioEngine.getMasterGain(), juce::dontSendNotification);
        pan.setValue(audioEngine.getMasterPan(), juce::dontSendNotification);
        panValue.setText(juce::String(audioEngine.getMasterPan(), 2), juce::dontSendNotification);
        return;
    }

    if (selectedTrack < 0 || selectedTrack >= static_cast<int>(trackModel.getTrackCount()))
        return;

    const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
    fader.setValue(track.volume.load(std::memory_order_relaxed), juce::dontSendNotification);
    pan.setValue(track.pan.load(std::memory_order_relaxed), juce::dontSendNotification);
    panValue.setText(juce::String(pan.getValue(), 2), juce::dontSendNotification);
    for (size_t slot = 0; slot < sendLevels.size(); ++slot)
    {
        const auto active = track.sends[slot].targetBus.isValid();
        sendLevels[slot].setValue(active ? track.sends[slot].level : 0.0f, juce::dontSendNotification);
        sendLevelReadouts[slot].setText(sendLevelPercent(active ? track.sends[slot].level : 0.0f),
                                         juce::dontSendNotification);
        sendPreFader[slot].setToggleState(active && track.sends[slot].preFader, juce::dontSendNotification);
    }
}

void ChannelStripComponent::paint(juce::Graphics& g)
{
    g.setColour(StudioForgeTheme::panelBackground);
    g.fillRect(getLocalBounds());
    const auto drawSection = [&g] (juce::Rectangle<int> bounds)
    {
        if (bounds.isEmpty()) return;
        g.setColour(StudioForgeTheme::separator.withAlpha(0.82f));
        g.drawHorizontalLine(bounds.getY(), static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
    };
    drawSection(audioFxRackBounds);
    if (isSelectedTrackStrip())
    {
        drawSection(sceneBounds);
        drawSection(sendsBounds);
        drawSection(routingBounds);
    }

    const auto meterBounds = meter.getBounds();
    if (meterBounds.isEmpty())
        return;

    g.setFont(juce::Font(juce::FontOptions(7.0f, juce::Font::bold)));
    g.setColour(StudioForgeTheme::secondaryText.withAlpha(0.78f));
    struct DecibelMark { const char* text; float decibels; };
    // A continuous, evenly spaced console scale. The unity (0 dB) mark is
    // exactly at the fader midpoint because of the skew point above.
    constexpr std::array<DecibelMark, 5> decibelLabels {{
        { "+12", 12.0f }, { "+6", 6.0f }, { "0", 0.0f },
        { "-6", -6.0f }, { "-12", -12.0f }
    }};
    const auto scaleTop = fader.getY() + juce::roundToInt(fader.getPositionOfValue(AudioGainRange::maximumChannelGain));
    const auto scaleBottom = fader.getY() + juce::roundToInt(fader.getPositionOfValue(0.0));
    for (size_t index = 0; index < decibelLabels.size(); ++index)
    {
        const auto& mark = decibelLabels[index];
        const auto proportion = static_cast<float>(index) / static_cast<float>(decibelLabels.size() - 1);
        const auto y = juce::roundToInt(juce::jmap(proportion, static_cast<float>(scaleTop), static_cast<float>(scaleBottom))) - 4;
        g.drawText(mark.text, meterBounds.getRight() + 2, y, 22, 8, juce::Justification::centredLeft, false);
    }
}

void ChannelStripComponent::resized()
{
    auto bounds = getLocalBounds().reduced(stripPadding, 3);
    const auto consoleHeight = juce::jlimit(224, 330, bounds.getHeight());
    auto visibleFxSlots = 0;
    for (const auto& slot : audioFxSlots)
        if (slot.isVisible())
            ++visibleFxSlots;

    auto visibleSceneSlots = 0;
    if (isSelectedTrackStrip())
        for (const auto& scene : sceneButtons)
            if (scene.isVisible())
                ++visibleSceneSlots;

    auto visibleSendRoutes = 0;
    if (isSelectedTrackStrip())
        for (const auto& route : sendRoutes)
            if (route.isVisible())
                ++visibleSendRoutes;

    // At a narrow inspector width a send's destination must never disappear.
    // Give it a full first row, with amount/mode/remove on a second row.
    const auto compactSendLayout = isSelectedTrackStrip() && getWidth() < 190;
    const auto sendControlRows = visibleSendRoutes == 0 ? 1
        : visibleSendRoutes * (compactSendLayout ? 2 : 1);

    // Reserve the physical console before laying out inserts, scenes and
    // sends.  These are two adjacent channel strips: one must never receive a
    // shorter fader merely because it happens to have more visible send rows.
    // Content above this boundary may become compact, but the PAN/meter/fader
    // module always preserves the same proportions in both strips.
    auto titleRow = bounds.removeFromTop(sectionHeaderHeight);
    auto performance = bounds.removeFromBottom(consoleHeight).reduced(2, 2);

    // Full-screen performance layouts should make the rack easy to scan and
    // hit with a mouse. Keep a compact minimum for smaller windows, using only
    // the space above the fixed console module.
    const auto sectionHeaders = isSelectedTrackStrip() ? 4 : 2;
    const auto controlRows = visibleFxSlots + 1
        + (isSelectedTrackStrip() ? 1 + visibleSceneSlots + sendControlRows + 1 : 0);
    const auto availableControlSpace = juce::jmax(0,
        bounds.getHeight() - sectionHeaders * sectionHeaderHeight);
    const auto responsiveControlHeight = juce::jlimit(18, 28,
        availableControlSpace / juce::jmax(1, controlRows));
    audioFxRackBounds = {};
    sceneBounds = {};
    sendsBounds = {};
    routingBounds = {};
    heading.setVisible(isSelectedTrackStrip());
    channelSelector.setVisible(! isSelectedTrackStrip());
    if (isSelectedTrackStrip()) heading.setBounds(titleRow);
    else channelSelector.setBounds(titleRow);
    if (isSelectedTrackStrip())
    {
        const auto fxStart = bounds.getY();
        fxHeading.setBounds(bounds.removeFromTop(sectionHeaderHeight));
        for (auto& slot : audioFxSlots)
            if (slot.isVisible()) slot.setBounds(bounds.removeFromTop(responsiveControlHeight).reduced(2, 1));
        addAudioFx.setBounds(bounds.removeFromTop(responsiveControlHeight).reduced(2, 1));
        audioFxRackBounds = { bounds.getX(), fxStart, bounds.getWidth(), bounds.getY() - fxStart };

        const auto sceneStart = bounds.getY();
        auto sceneHeader = bounds.removeFromTop(responsiveControlHeight);
        sceneHeading.setBounds(sceneHeader.removeFromLeft(58));
        if (captureScene.isVisible()) captureScene.setBounds(sceneHeader.reduced(0, 3));
        for (auto& scene : sceneButtons)
            if (scene.isVisible()) scene.setBounds(bounds.removeFromTop(responsiveControlHeight).reduced(1, 1));
        sceneBounds = { bounds.getX(), sceneStart, bounds.getWidth(), bounds.getY() - sceneStart };

        const auto sendsStart = bounds.getY();
        auto sendsHeader = bounds.removeFromTop(sectionHeaderHeight);
        sendsHeading.setBounds(sendsHeader);
        if (visibleSendRoutes == 0)
        {
            sendSummary.setVisible(true);
            sendSummary.setBounds(bounds.removeFromTop(responsiveControlHeight).reduced(1, 1));
        }
        else
        {
            sendSummary.setVisible(true);
            sendSummary.setBounds(sendsHeader.removeFromRight(20).reduced(1));
            for (size_t slot = 0; slot < sendRoutes.size(); ++slot)
            {
                if (! sendRoutes[slot].isVisible())
                    continue;
                if (compactSendLayout)
                {
                    // Destination is the primary decision, so it owns the
                    // first row. The compact second row retains amount,
                    // tap-point and remove without stealing its text space.
                    sendRoutes[slot].setBounds(bounds.removeFromTop(responsiveControlHeight).reduced(1, 1));
                }
                auto row = bounds.removeFromTop(responsiveControlHeight).reduced(1, 1);
                clearSendButtons[slot].setBounds(row.removeFromRight(22));
                sendPreFader[slot].setBounds(row.removeFromRight(compactSendLayout ? 35 : 48));
                sendLevels[slot].setBounds(row.removeFromLeft(juce::jmin(row.getHeight() + 2, 30)).reduced(1));
                sendLevelReadouts[slot].setVisible(true);
                sendLevelReadouts[slot].setBounds(row.removeFromLeft(juce::jmin(34, row.getWidth())));
                if (! compactSendLayout)
                    sendRoutes[slot].setBounds(row);
            }
        }
        sendsBounds = { bounds.getX(), sendsStart, bounds.getWidth(), bounds.getY() - sendsStart };

        const auto routingStart = bounds.getY();
        routingHeading.setBounds(bounds.removeFromTop(sectionHeaderHeight));
        outputRoute.setBounds(bounds.removeFromTop(responsiveControlHeight).reduced(0, 1));
        routingBounds = { bounds.getX(), routingStart, bounds.getWidth(), bounds.getY() - routingStart };
    }
    else
    {
        const auto fxStart = bounds.getY();
        fxHeading.setBounds(bounds.removeFromTop(sectionHeaderHeight));
        for (auto& slot : audioFxSlots)
            if (slot.isVisible()) slot.setBounds(bounds.removeFromTop(responsiveControlHeight).reduced(2, 1));
        addAudioFx.setVisible(true);
        addAudioFx.setBounds(bounds.removeFromTop(responsiveControlHeight).reduced(2, 1));
        audioFxRackBounds = { bounds.getX(), fxStart, bounds.getWidth(), bounds.getY() - fxStart };
        sceneHeading.setVisible(false);
        captureScene.setVisible(false);
        for (auto& scene : sceneButtons) scene.setVisible(false);
        sendsHeading.setVisible(false); sendSummary.setVisible(false); routingHeading.setVisible(false); outputRoute.setVisible(false);
        for (size_t slot = 0; slot < sendRoutes.size(); ++slot)
        {
            sendRoutes[slot].setVisible(false);
            sendLevels[slot].setVisible(false);
            sendLevelReadouts[slot].setVisible(false);
            sendPreFader[slot].setVisible(false);
            clearSendButtons[slot].setVisible(false);
        }
    }
    // Both channel strips share the Inspector's reserved bottom console,
    // keeping PAN/LEVEL and both faders aligned at every window size.
    panLabel.setText(isSelectedTrackStrip() ? "PAN" : "BALANCE", juce::dontSendNotification);
    panLabel.setVisible(true);
    pan.setVisible(true);
    panValue.setVisible(true);
    panLabel.setBounds(performance.removeFromTop(sectionHeaderHeight));
    auto panArea = performance.removeFromTop(70).withSizeKeepingCentre(58, 68);
    pan.setBounds(panArea.removeFromTop(46).withSizeKeepingCentre(46, 46));
    panArea.removeFromTop(4);
    panValue.setBounds(panArea.removeFromTop(18).withSizeKeepingCentre(54, 18));
    levelLabel.setBounds(performance.removeFromTop(sectionHeaderHeight));
    auto levelArea = performance.reduced(0, 2);
    auto console = levelArea.withSizeKeepingCentre(104, levelArea.getHeight());
    meter.setBounds(console.removeFromLeft(18).reduced(1, 1));
    console.removeFromLeft(26);
    fader.setBounds(console.removeFromLeft(48));
}
