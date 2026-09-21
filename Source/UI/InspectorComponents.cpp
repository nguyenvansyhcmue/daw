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

void configureSectionLabel(juce::Label& label)
{
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, StudioForgeTheme::secondaryText);
    label.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
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

private:
    std::shared_ptr<AudioEffectProcessor> owner;
    std::unique_ptr<juce::Component> editor;
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

TrackInspectorComponent::TrackInspectorComponent(TrackDataModel& model) : trackModel(model)
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
    const std::array<std::pair<juce::ToggleButton*, const char*>, 5> buttons {{
        { &recordEnable, "Record enable" }, { &inputMonitoring, "Input monitoring" },
        { &mute, "Mute" }, { &solo, "Solo" }, { &soloSafe, "Solo Safe" }
    }};
    for (const auto& [button, tooltip] : buttons)
    {
        button->setTooltip(tooltip);
        addAndMakeVisible(*button);
    }
    recordEnable.onClick = [this] { if (selectedTrack >= 0) trackModel.setTrackArmed(static_cast<size_t>(selectedTrack), recordEnable.getToggleState()); };
    inputMonitoring.onClick = [this] { if (selectedTrack >= 0) trackModel.setTrackInputMonitoring(static_cast<size_t>(selectedTrack), inputMonitoring.getToggleState()); };
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
    inputMonitoring.setToggleState(track.inputMonitoring.load(), juce::dontSendNotification);
    inputMonitoring.setEnabled(state.supportsInputMonitoring);
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
    auto buttons = row.removeFromRight(5 * controlHeight);
    trackName.setBounds(row.reduced(1, 0));
    for (auto* button : { &recordEnable, &inputMonitoring, &mute, &solo, &soloSafe }) button->setBounds(buttons.removeFromLeft(controlHeight).reduced(1));
    inputDetails.setBounds(bounds.removeFromTop(16));
    outputDetails.setBounds(bounds.removeFromTop(16));
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
    sendSummary.setTooltip("Manage aux sends for the selected track");
    sendSummary.onClick = [this] { showSendMenu(); };
    addAndMakeVisible(sendSummary);
    for (size_t slot = 0; slot < audioFxSlots.size(); ++slot)
    {
        auto& button = audioFxSlots[slot];
        button.onClick = [this, slot] { showAudioFxMenu(slot); };
        addAndMakeVisible(button);
    }
    addAudioFx.onClick = [this]
    {
        const auto rack = isSelectedTrackStrip()
                            ? (selectedTrack >= 0 ? trackModel.getFxRackSnapshot(static_cast<size_t>(selectedTrack)) : nullptr)
                            : audioEngine.getMasterFxRackSnapshot();
        for (size_t slot = 0; rack != nullptr && slot < rack->processors.size(); ++slot)
            if (rack->processors[slot] == nullptr) { showAudioFxMenu(slot); return; }
    };
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
        route.onChange = [this, slot] { setSendRoute(slot); };
        level.setRange(0.0, 1.0, 0.01);
        level.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 18);
        level.onValueChange = [this, slot] { setSendRoute(slot); };
        preFader.setButtonText("Pre");
        preFader.setTooltip("Pre-Fader send. Disabled means Post-Pan: follows the track volume and stereo pan.");
        preFader.onClick = [this, slot] { setSendRoute(slot); };
        clear.setButtonText("×");
        clear.setTooltip("Remove send");
        clear.onClick = [this, slot] { clearSendRoute(slot); };
        addAndMakeVisible(route);
        addAndMakeVisible(level);
        addAndMakeVisible(preFader);
        addAndMakeVisible(clear);
        route.setVisible(false);
        level.setVisible(false);
        preFader.setVisible(false);
        clear.setVisible(false);
    }
    pan.setRange(-1.0, 1.0, 0.01);
    pan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    pan.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
    pan.textFromValueFunction = [] (double value)
    {
        const auto panValue = static_cast<float>(value);
        if (std::abs(panValue) < 0.005f)
            return juce::String("C");
        return (panValue < 0.0f ? "L " : "R ") + juce::String(juce::roundToInt(std::abs(panValue) * 100.0f));
    };
    fader.setRange(0.0, 2.0, 0.01);
    fader.setSliderStyle(juce::Slider::LinearVertical);
    fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
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
    pan.onValueChange = [this] { if (selectedTrack >= 0) trackModel.setTrackPan(static_cast<size_t>(selectedTrack), static_cast<float>(pan.getValue())); };
    fader.onValueChange = [this]
    {
        if (isSelectedTrackStrip() && selectedTrack >= 0) trackModel.setTrackVolume(static_cast<size_t>(selectedTrack), static_cast<float>(fader.getValue()));
        else audioEngine.setMasterGain(static_cast<float>(fader.getValue()));
    };
    for (auto* component : { static_cast<juce::Component*>(&heading), static_cast<juce::Component*>(&fxHeading), static_cast<juce::Component*>(&sendsHeading), static_cast<juce::Component*>(&routingHeading), static_cast<juce::Component*>(&panLabel), static_cast<juce::Component*>(&levelLabel), static_cast<juce::Component*>(&pan), static_cast<juce::Component*>(&fader), static_cast<juce::Component*>(&meter) }) addAndMakeVisible(*component);
    startTimerHz(30);
}

void ChannelStripComponent::setSelectedTrack(int trackIndex) { selectedTrack = trackIndex; refresh(); }

void ChannelStripComponent::refresh()
{
    if (! isSelectedTrackStrip())
    {
        fader.setValue(audioEngine.getMasterGain(), juce::dontSendNotification);
        refreshAudioFx();
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
    if (! active) { refreshSendSummary(); return; }
    const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
    fader.setValue(track.volume.load(), juce::dontSendNotification);
    pan.setValue(track.pan.load(), juce::dontSendNotification);
    refreshAudioFx();
    refreshRouting();
    refreshSendSummary();
}

void ChannelStripComponent::refreshAudioFx()
{
    const auto rack = isSelectedTrackStrip()
                        ? (selectedTrack >= 0 ? trackModel.getFxRackSnapshot(static_cast<size_t>(selectedTrack)) : nullptr)
                        : audioEngine.getMasterFxRackSnapshot();
    for (size_t slot = 0; slot < audioFxSlots.size(); ++slot)
    {
        const auto processor = rack != nullptr ? rack->processors[slot] : nullptr;
        audioFxSlots[slot].setVisible(true);
        audioFxSlots[slot].setButtonText(processor != nullptr
                                             ? "Insert " + juce::String(static_cast<int>(slot + 1)) + "  " + processor->getName()
                                             : "Insert " + juce::String(static_cast<int>(slot + 1)) + "  Empty");
    }
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
        outputRoute.addItem("Bus " + juce::String(static_cast<int>(index + 1)), item);
        for (auto& route : sendRoutes)
            route.addItem("Bus " + juce::String(static_cast<int>(index + 1)), item);
    }
    const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
    int output = 1;
    for (size_t index = 0; index < buses.size() && index < routeBusIds.size(); ++index)
    {
        if (track.outputBus == routeBusIds[index]) output = static_cast<int>(index + 2);
        for (size_t slot = 0; slot < TrackDataModel::maxSendsPerTrack; ++slot)
            if (track.sends[slot].targetBus == routeBusIds[index])
                sendRoutes[slot].setSelectedId(static_cast<int>(index + 2), juce::dontSendNotification);
    }
    outputRoute.setSelectedId(output, juce::dontSendNotification);
    for (size_t slot = 0; slot < sendRoutes.size(); ++slot)
    {
        const auto active = track.sends[slot].targetBus.isValid();
        if (! active)
            sendRoutes[slot].setSelectedId(1, juce::dontSendNotification);
        sendLevels[slot].setValue(active ? track.sends[slot].level : 0.0f, juce::dontSendNotification);
        sendPreFader[slot].setToggleState(active && track.sends[slot].preFader, juce::dontSendNotification);
    }
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
        sendSummary.setButtonText("No Sends  +");
        return;
    }

    const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
    if (track.activeSendCount == 0)
        sendSummary.setButtonText("No Sends  +");
    else
        sendSummary.setButtonText(juce::String(static_cast<int>(track.activeSendCount))
                                  + " Send" + (track.activeSendCount == 1 ? "" : "s") + "  Manage...");
}

void ChannelStripComponent::showSendMenu()
{
    if (! isSelectedTrackStrip() || selectedTrack < 0 || selectedTrack >= static_cast<int>(trackModel.getTrackCount()))
        return;

    const auto trackId = trackModel.getTrackId(static_cast<size_t>(selectedTrack));
    const auto& buses = trackModel.getBuses();
    juce::PopupMenu menu;
    if (buses.empty())
    {
        menu.addItem(1, "Create Aux Bus");
    }
    else
    {
        juce::PopupMenu addMenu;
        for (size_t bus = 0; bus < buses.size(); ++bus)
            addMenu.addItem(static_cast<int>(100 + bus), "Send to Bus " + juce::String(static_cast<int>(bus + 1)));
        menu.addSubMenu("Add Send", addMenu, trackModel.getTrack(static_cast<size_t>(selectedTrack)).activeSendCount < TrackDataModel::maxSendsPerTrack);

        const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
        constexpr std::array<float, 5> levels { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        for (size_t slot = 0; slot < TrackDataModel::maxSendsPerTrack; ++slot)
        {
            if (! track.sends[slot].targetBus.isValid())
                continue;
            juce::PopupMenu slotMenu;
            juce::PopupMenu destinationMenu;
            for (size_t bus = 0; bus < buses.size(); ++bus)
                destinationMenu.addItem(static_cast<int>(1000 + slot * 100 + bus),
                                        "Bus " + juce::String(static_cast<int>(bus + 1)),
                                        true, track.sends[slot].targetBus == buses[bus].id);
            slotMenu.addSubMenu("Destination", destinationMenu);

            juce::PopupMenu levelMenu;
            for (size_t level = 0; level < levels.size(); ++level)
                levelMenu.addItem(static_cast<int>(2000 + slot * 100 + level),
                                  juce::String(juce::roundToInt(levels[level] * 100.0f)) + "%",
                                  true, std::abs(track.sends[slot].level - levels[level]) < 0.005f);
            slotMenu.addSubMenu("Level", levelMenu);
            slotMenu.addItem(static_cast<int>(3000 + slot), track.sends[slot].preFader ? "Switch to Post-Pan" : "Switch to Pre-Fader");
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
                model.setTrackSendRoute(trackId, static_cast<size_t>(std::distance(track.sends.begin(), slot)), bus, 0.5f, false);
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
                    model.setTrackSendRoute(trackId, static_cast<size_t>(std::distance(track.sends.begin(), slot)), buses[busIndex].id, 0.5f, false);
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
                    model.setTrackSendRoute(trackId, slot, track.sends[slot].targetBus, track.sends[slot].level, !track.sends[slot].preFader);
            }
        }
        else if (choice >= 4000 && choice < 5000)
            model.clearTrackSendRoute(trackId, static_cast<size_t>(choice - 4000));
        safeOwner->refreshRouting();
        safeOwner->refreshSendSummary();
    });
}

void ChannelStripComponent::showAudioFxMenu(size_t slot)
{
    if (isSelectedTrackStrip() && selectedTrack < 0) return;
    juce::PopupMenu menu;
    menu.addItem(1, "Remove Audio FX"); menu.addItem(2, "Add Gain Utility"); menu.addItem(3, "Bypass");
    menu.addItem(4, "Load VST3 Plug-in..."); menu.addItem(5, "Open Plug-in Editor");
    const auto canAddMidiFx = isSelectedTrackStrip() && selectedTrack >= 0
        && trackModel.getTrack(static_cast<size_t>(selectedTrack)).type != TrackType::audio;
    menu.addItem(6, "Add MIDI Transpose (+12)", canAddMidiFx);
    menu.showMenuAsync(juce::PopupMenu::Options {}, [this, slot] (int choice)
    {
        if (choice == 1)
        {
            if (isSelectedTrackStrip()) audioEngine.setFxProcessor(static_cast<size_t>(selectedTrack), slot, nullptr);
            else audioEngine.setMasterFxProcessor(slot, nullptr);
        }
        else if (choice == 2)
        {
            if (isSelectedTrackStrip()) audioEngine.setFxProcessor(static_cast<size_t>(selectedTrack), slot, std::make_shared<GainUtilityProcessor>());
            else audioEngine.setMasterFxProcessor(slot, std::make_shared<GainUtilityProcessor>());
        }
        else if (choice == 3)
        {
            const auto rack = isSelectedTrackStrip() ? trackModel.getFxRackSnapshot(static_cast<size_t>(selectedTrack))
                                                      : audioEngine.getMasterFxRackSnapshot();
            if (rack != nullptr)
            {
                if (isSelectedTrackStrip()) audioEngine.setFxBypassed(static_cast<size_t>(selectedTrack), slot, !rack->bypass[slot]);
                else audioEngine.setMasterFxBypassed(slot, !rack->bypass[slot]);
            }
        }
        else if (choice == 4)
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
        else if (choice == 5)
        {
            showPluginEditor(slot);
            return;
        }
        else if (choice == 6 && isSelectedTrackStrip())
        {
            audioEngine.setFxProcessor(static_cast<size_t>(selectedTrack), slot,
                                       std::make_shared<MidiTransposeProcessor>(12));
        }
        refresh();
    });
}

void ChannelStripComponent::showPluginBrowser(size_t slot)
{
    auto* panel = new PluginBrowserPanel(pluginHost, [this, slot](const juce::PluginDescription& description)
    {
        juce::String error;
        auto* device = audioEngine.getAudioDeviceManager().getCurrentAudioDevice();
        const auto sampleRate = device != nullptr ? device->getCurrentSampleRate() : trackModel.getSampleRate();
        const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
        if (auto effect = pluginHost.createEffect(description, sampleRate, blockSize, error))
        {
            if (isSelectedTrackStrip()) audioEngine.setFxProcessor(static_cast<size_t>(selectedTrack), slot, std::move(effect));
            else audioEngine.setMasterFxProcessor(slot, std::move(effect));
        }
        refresh();
    });

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(panel);
    options.dialogTitle = "Plug-in Browser";
    options.dialogBackgroundColour = juce::Colour(0xff25282d);
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.launchAsync();
}

void ChannelStripComponent::showPluginEditor(size_t slot)
{
    const auto* rack = isSelectedTrackStrip()
        ? (selectedTrack >= 0 ? trackModel.getFxRackSnapshot(static_cast<size_t>(selectedTrack)) : nullptr)
        : audioEngine.getMasterFxRackSnapshot();
    if (rack == nullptr || slot >= rack->processors.size())
        return;

    const auto processor = rack->processors[slot];
    if (processor == nullptr || ! processor->hasEditor())
        return;

    auto editor = processor->createEditor();
    if (editor == nullptr)
        return;

    const auto editorBounds = editor->getBounds();
    auto* host = new PluginEditorHost(processor, std::move(editor));
    host->setSize(juce::jmax(320, editorBounds.getWidth()), juce::jmax(220, editorBounds.getHeight()));
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(host);
    options.dialogTitle = processor->getName();
    options.dialogBackgroundColour = juce::Colour(0xff25282d);
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.launchAsync();
}

BusId ChannelStripComponent::busForMenuItem(int itemId) const noexcept
{
    const auto index = itemId - 2;
    return index >= 0 && index < static_cast<int>(routeBusIds.size()) ? routeBusIds[static_cast<size_t>(index)] : BusId {};
}

void ChannelStripComponent::timerCallback()
{
    if (isSelectedTrackStrip() && selectedTrack >= 0)
        meter.updatePeak(audioEngine.getTrackPeak(static_cast<size_t>(selectedTrack)));
    else
        meter.updateStereoPeak(audioEngine.getMasterLeftPeak(), audioEngine.getMasterRightPeak());
    synchroniseControlsFromState();
}

void ChannelStripComponent::synchroniseControlsFromState()
{
    if (! isSelectedTrackStrip())
    {
        fader.setValue(audioEngine.getMasterGain(), juce::dontSendNotification);
        return;
    }

    if (selectedTrack < 0 || selectedTrack >= static_cast<int>(trackModel.getTrackCount()))
        return;

    const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
    fader.setValue(track.volume.load(std::memory_order_relaxed), juce::dontSendNotification);
    pan.setValue(track.pan.load(std::memory_order_relaxed), juce::dontSendNotification);
    for (size_t slot = 0; slot < sendLevels.size(); ++slot)
    {
        const auto active = track.sends[slot].targetBus.isValid();
        sendLevels[slot].setValue(active ? track.sends[slot].level : 0.0f, juce::dontSendNotification);
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
        drawSection(sendsBounds);
        drawSection(routingBounds);
    }
}

void ChannelStripComponent::resized()
{
    auto bounds = getLocalBounds().reduced(stripPadding, 3);
    audioFxRackBounds = {};
    sendsBounds = {};
    routingBounds = {};
    heading.setBounds(bounds.removeFromTop(sectionHeaderHeight));
    if (isSelectedTrackStrip())
    {
        const auto fxStart = bounds.getY();
        fxHeading.setBounds(bounds.removeFromTop(sectionHeaderHeight));
        for (auto& slot : audioFxSlots)
            if (slot.isVisible()) slot.setBounds(bounds.removeFromTop(controlHeight).reduced(1, 0));
        addAudioFx.setBounds(bounds.removeFromTop(controlHeight));
        audioFxRackBounds = { bounds.getX(), fxStart, bounds.getWidth(), bounds.getY() - fxStart };

        const auto sendsStart = bounds.getY();
        sendsHeading.setBounds(bounds.removeFromTop(sectionHeaderHeight));
        sendSummary.setVisible(true);
        sendSummary.setBounds(bounds.removeFromTop(controlHeight).reduced(1, 0));
        for (size_t slot = 0; slot < sendRoutes.size(); ++slot)
        {
            sendRoutes[slot].setVisible(false);
            sendLevels[slot].setVisible(false);
            sendPreFader[slot].setVisible(false);
            clearSendButtons[slot].setVisible(false);
        }
        sendsBounds = { bounds.getX(), sendsStart, bounds.getWidth(), bounds.getY() - sendsStart };

        const auto routingStart = bounds.getY();
        routingHeading.setBounds(bounds.removeFromTop(sectionHeaderHeight));
        outputRoute.setBounds(bounds.removeFromTop(controlHeight));
        routingBounds = { bounds.getX(), routingStart, bounds.getWidth(), bounds.getY() - routingStart };
    }
    else
    {
        const auto fxStart = bounds.getY();
        fxHeading.setBounds(bounds.removeFromTop(sectionHeaderHeight));
        for (auto& slot : audioFxSlots)
            if (slot.isVisible()) slot.setBounds(bounds.removeFromTop(controlHeight).reduced(1, 0));
        addAudioFx.setVisible(true);
        addAudioFx.setBounds(bounds.removeFromTop(controlHeight));
        audioFxRackBounds = { bounds.getX(), fxStart, bounds.getWidth(), bounds.getY() - fxStart };
        sendsHeading.setVisible(false); sendSummary.setVisible(false); routingHeading.setVisible(false); outputRoute.setVisible(false);
        for (size_t slot = 0; slot < sendRoutes.size(); ++slot)
        {
            sendRoutes[slot].setVisible(false);
            sendLevels[slot].setVisible(false);
            sendPreFader[slot].setVisible(false);
            clearSendButtons[slot].setVisible(false);
        }
    }
    const auto consoleHeight = juce::jmin(204, bounds.getHeight());
    auto performance = bounds.removeFromBottom(consoleHeight).reduced(2, 2);
    panLabel.setBounds(performance.removeFromTop(sectionHeaderHeight));
    auto panArea = performance.removeFromTop(50);
    pan.setBounds(panArea.withSizeKeepingCentre(46, 46));
    levelLabel.setBounds(performance.removeFromTop(sectionHeaderHeight));
    auto levelArea = performance.reduced(0, 2);
    auto console = levelArea.withSizeKeepingCentre(58, levelArea.getHeight());
    meter.setBounds(console.removeFromLeft(12).reduced(1, 1));
    console.removeFromLeft(4);
    fader.setBounds(console.removeFromLeft(42));
}
