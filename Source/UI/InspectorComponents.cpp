#include "InspectorComponents.h"

#include "../AudioEngine/AudioEngine.h"
#include "../AudioEngine/GainUtilityProcessor.h"
#include "../Plugins/PluginHostService.h"

#include <algorithm>

namespace
{
constexpr int sectionHeaderHeight = 18;
constexpr int controlHeight = 22;
constexpr int stripPadding = 5;

void configureSectionLabel(juce::Label& label)
{
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.62f));
    label.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
}
}

RegionInspectorComponent::RegionInspectorComponent(TrackDataModel& model) : trackModel(model)
{
    configureSectionLabel(heading);
    context.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    context.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::plain)));
    addAndMakeVisible(heading);
    addAndMakeVisible(context);
    clearSelection();
}

void RegionInspectorComponent::setSelectedAudioClip(ClipId clip)
{
    selectedMidiClip = {};
    for (size_t trackIndex = 0; trackIndex < trackModel.getTrackCount(); ++trackIndex)
        for (const auto& candidate : trackModel.getTrack(trackIndex).clips)
            if (candidate.id == clip)
            {
                context.setText("Audio region - gain and fades available", juce::dontSendNotification);
                return;
            }
    clearSelection();
}

void RegionInspectorComponent::setSelectedMidiClip(MidiClipId clip)
{
    selectedMidiClip = clip;
    const auto found = std::find_if(trackModel.getMidiClips().begin(), trackModel.getMidiClips().end(),
                                    [clip] (const MidiClipState& candidate) { return candidate.id == clip; });
    context.setText(found != trackModel.getMidiClips().end() ? "MIDI region - edit notes in Piano Roll" : "No region selected",
                    juce::dontSendNotification);
}

void RegionInspectorComponent::clearSelection()
{
    selectedMidiClip = {};
    context.setText("No region selected", juce::dontSendNotification);
}

void RegionInspectorComponent::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff2d3137));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
}

void RegionInspectorComponent::resized()
{
    auto bounds = getLocalBounds().reduced(stripPadding, 3);
    heading.setBounds(bounds.removeFromTop(sectionHeaderHeight));
    context.setBounds(bounds);
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
    const std::array<std::pair<juce::ToggleButton*, const char*>, 4> buttons {{
        { &recordEnable, "Record enable" }, { &inputMonitoring, "Input monitoring" },
        { &mute, "Mute" }, { &solo, "Solo" }
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
    addAndMakeVisible(heading);
    addAndMakeVisible(trackName);
}

void TrackInspectorComponent::setSelectedTrack(int trackIndex)
{
    selectedTrack = trackIndex;
    refresh();
}

void TrackInspectorComponent::refresh()
{
    const auto active = selectedTrack >= 0 && selectedTrack < static_cast<int>(trackModel.getTrackCount());
    trackName.setEnabled(active);
    for (auto* button : { &recordEnable, &inputMonitoring, &mute, &solo }) button->setEnabled(active);
    if (! active) { trackName.setText("No track selected", juce::dontSendNotification); return; }
    const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
    trackName.setText(track.name, juce::dontSendNotification);
    recordEnable.setToggleState(track.armed.load(), juce::dontSendNotification);
    inputMonitoring.setToggleState(track.inputMonitoring.load(), juce::dontSendNotification);
    inputMonitoring.setEnabled(track.type == TrackType::audio);
    mute.setToggleState(track.muted.load(), juce::dontSendNotification);
    solo.setToggleState(track.solo.load(), juce::dontSendNotification);
}

void TrackInspectorComponent::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff2d3137));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
}

void TrackInspectorComponent::resized()
{
    auto bounds = getLocalBounds().reduced(stripPadding, 3);
    heading.setBounds(bounds.removeFromTop(sectionHeaderHeight));
    auto row = bounds.removeFromTop(controlHeight);
    auto buttons = row.removeFromRight(4 * controlHeight);
    trackName.setBounds(row.reduced(1, 0));
    for (auto* button : { &recordEnable, &inputMonitoring, &mute, &solo }) button->setBounds(buttons.removeFromLeft(controlHeight).reduced(1));
}

ChannelStripComponent::ChannelStripComponent(ChannelRole role, TrackDataModel& model, AudioEngine& engine, PluginHostService& host)
    : channelRole(role), trackModel(model), audioEngine(engine), pluginHost(host)
{
    heading.setText(isSelectedTrackStrip() ? "CHANNEL STRIP" : "STEREO OUT", juce::dontSendNotification);
    configureSectionLabel(heading);
    configureSectionLabel(fxHeading);
    configureSectionLabel(sendsHeading);
    configureSectionLabel(routingHeading);
    for (size_t slot = 0; slot < audioFxSlots.size(); ++slot)
    {
        auto& button = audioFxSlots[slot];
        button.onClick = [this, slot] { showAudioFxMenu(slot); };
        addAndMakeVisible(button);
    }
    addAudioFx.onClick = [this]
    {
        const auto rack = selectedTrack >= 0 ? trackModel.getFxRackSnapshot(static_cast<size_t>(selectedTrack)) : nullptr;
        for (size_t slot = 0; rack != nullptr && slot < rack->processors.size(); ++slot)
            if (rack->processors[slot] == nullptr) { showAudioFxMenu(slot); return; }
    };
    addAndMakeVisible(addAudioFx);
    for (auto* route : { &outputRoute, &sendRoute })
    {
        route->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff20252c));
        route->setColour(juce::ComboBox::textColourId, juce::Colours::white.withAlpha(0.86f));
        addAndMakeVisible(*route);
    }
    outputRoute.onChange = [this]
    {
        if (selectedTrack >= 0) trackModel.setTrackOutputBus(trackModel.getTrackId(static_cast<size_t>(selectedTrack)), busForMenuItem(outputRoute.getSelectedId()));
    };
    sendRoute.onChange = [this]
    {
        if (selectedTrack < 0) return;
        const auto bus = busForMenuItem(sendRoute.getSelectedId());
        const auto id = trackModel.getTrackId(static_cast<size_t>(selectedTrack));
        if (bus.isValid()) trackModel.setTrackSend(id, bus, static_cast<float>(sendLevel.getValue())); else trackModel.clearTrackSend(id);
    };
    sendLevel.setRange(0.0, 1.0, 0.01);
    sendLevel.setTextBoxStyle(juce::Slider::TextBoxRight, false, 38, 18);
    sendLevel.onValueChange = [this]
    {
        if (selectedTrack < 0) return;
        const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
        if (track.sendBus.isValid()) trackModel.setTrackSend(track.id, track.sendBus, static_cast<float>(sendLevel.getValue()));
    };
    pan.setRange(-1.0, 1.0, 0.01);
    pan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    pan.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
    fader.setRange(0.0, 2.0, 0.01);
    fader.setSliderStyle(juce::Slider::LinearVertical);
    fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
    pan.onValueChange = [this] { if (selectedTrack >= 0) trackModel.setTrackPan(static_cast<size_t>(selectedTrack), static_cast<float>(pan.getValue())); };
    fader.onValueChange = [this]
    {
        if (isSelectedTrackStrip() && selectedTrack >= 0) trackModel.setTrackVolume(static_cast<size_t>(selectedTrack), static_cast<float>(fader.getValue()));
        else audioEngine.setMasterGain(static_cast<float>(fader.getValue()));
    };
    for (auto* component : { static_cast<juce::Component*>(&heading), static_cast<juce::Component*>(&fxHeading), static_cast<juce::Component*>(&sendsHeading), static_cast<juce::Component*>(&routingHeading), static_cast<juce::Component*>(&sendLevel), static_cast<juce::Component*>(&pan), static_cast<juce::Component*>(&fader), static_cast<juce::Component*>(&meter) }) addAndMakeVisible(*component);
    startTimerHz(30);
}

void ChannelStripComponent::setSelectedTrack(int trackIndex) { selectedTrack = trackIndex; refresh(); }

void ChannelStripComponent::refresh()
{
    if (! isSelectedTrackStrip()) { fader.setValue(audioEngine.getMasterGain(), juce::dontSendNotification); return; }
    const auto active = selectedTrack >= 0 && selectedTrack < static_cast<int>(trackModel.getTrackCount());
    for (auto* component : { static_cast<juce::Component*>(&addAudioFx), static_cast<juce::Component*>(&sendRoute), static_cast<juce::Component*>(&sendLevel), static_cast<juce::Component*>(&outputRoute), static_cast<juce::Component*>(&pan), static_cast<juce::Component*>(&fader) }) component->setEnabled(active);
    if (! active) return;
    const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
    fader.setValue(track.volume.load(), juce::dontSendNotification);
    pan.setValue(track.pan.load(), juce::dontSendNotification);
    sendLevel.setValue(track.sendAmount, juce::dontSendNotification);
    refreshAudioFx();
    refreshRouting();
}

void ChannelStripComponent::refreshAudioFx()
{
    const auto rack = selectedTrack >= 0 ? trackModel.getFxRackSnapshot(static_cast<size_t>(selectedTrack)) : nullptr;
    for (size_t slot = 0; slot < audioFxSlots.size(); ++slot)
    {
        const auto processor = rack != nullptr ? rack->processors[slot] : nullptr;
        audioFxSlots[slot].setVisible(processor != nullptr);
        audioFxSlots[slot].setButtonText(processor != nullptr ? processor->getName() : juce::String {});
    }
    resized();
}

void ChannelStripComponent::refreshRouting()
{
    outputRoute.clear(juce::dontSendNotification); sendRoute.clear(juce::dontSendNotification); routeBusIds.fill({});
    outputRoute.addItem("Stereo Out", 1); sendRoute.addItem("No Send", 1);
    if (selectedTrack < 0) return;
    const auto& buses = trackModel.getBuses();
    for (size_t index = 0; index < buses.size() && index < routeBusIds.size(); ++index)
    {
        routeBusIds[index] = buses[index].id;
        const auto item = static_cast<int>(index + 2);
        outputRoute.addItem("Bus " + juce::String(static_cast<int>(index + 1)), item);
        sendRoute.addItem("Bus " + juce::String(static_cast<int>(index + 1)), item);
    }
    const auto& track = trackModel.getTrack(static_cast<size_t>(selectedTrack));
    int output = 1, send = 1;
    for (size_t index = 0; index < buses.size() && index < routeBusIds.size(); ++index)
    {
        if (track.outputBus == routeBusIds[index]) output = static_cast<int>(index + 2);
        if (track.sendBus == routeBusIds[index]) send = static_cast<int>(index + 2);
    }
    outputRoute.setSelectedId(output, juce::dontSendNotification); sendRoute.setSelectedId(send, juce::dontSendNotification);
}

void ChannelStripComponent::showAudioFxMenu(size_t slot)
{
    if (selectedTrack < 0) return;
    juce::PopupMenu menu;
    menu.addItem(1, "Remove Audio FX"); menu.addItem(2, "Add Gain Utility"); menu.addItem(3, "Bypass"); menu.addSeparator(); menu.addItem(4, "Load VST3 Plug-in...");
    menu.showMenuAsync(juce::PopupMenu::Options {}, [this, slot] (int choice)
    {
        if (choice == 1) audioEngine.setFxProcessor(static_cast<size_t>(selectedTrack), slot, nullptr);
        else if (choice == 2) audioEngine.setFxProcessor(static_cast<size_t>(selectedTrack), slot, std::make_shared<GainUtilityProcessor>());
        else if (choice == 3)
        {
            if (const auto rack = trackModel.getFxRackSnapshot(static_cast<size_t>(selectedTrack)); rack != nullptr)
                audioEngine.setFxBypassed(static_cast<size_t>(selectedTrack), slot, !rack->bypass[slot]);
        }
        else if (choice == 4)
        {
            pluginFileChooser = std::make_unique<juce::FileChooser>("Load VST3 plug-in", juce::File {}, "*.vst3");
            pluginFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                [this, slot] (const juce::FileChooser& chooser)
                {
                    const auto file = chooser.getResult();
                    if (file.exists() && pluginHost.scanVst3(file).wasOk())
                    {
                        const auto plugins = pluginHost.getKnownPlugins();
                        juce::String error;
                        auto* device = audioEngine.getAudioDeviceManager().getCurrentAudioDevice();
                        const auto sampleRate = device != nullptr ? device->getCurrentSampleRate() : trackModel.getSampleRate();
                        const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
                        if (! plugins.isEmpty()) if (auto effect = pluginHost.createEffect(plugins.getLast(), sampleRate, blockSize, error)) audioEngine.setFxProcessor(static_cast<size_t>(selectedTrack), slot, std::move(effect));
                    }
                    pluginFileChooser.reset(); refresh();
                });
            return;
        }
        refresh();
    });
}

BusId ChannelStripComponent::busForMenuItem(int itemId) const noexcept
{
    const auto index = itemId - 2;
    return index >= 0 && index < static_cast<int>(routeBusIds.size()) ? routeBusIds[static_cast<size_t>(index)] : BusId {};
}

void ChannelStripComponent::timerCallback()
{
    meter.updatePeak(isSelectedTrackStrip() && selectedTrack >= 0 ? audioEngine.getTrackPeak(static_cast<size_t>(selectedTrack)) : audioEngine.getMasterPeak());
}

void ChannelStripComponent::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff292d33));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    if (isSelectedTrackStrip())
    {
        const auto drawSection = [&g] (juce::Rectangle<int> bounds)
        {
            if (bounds.isEmpty()) return;
            const auto frame = bounds.toFloat().expanded(1.0f, 2.0f);
            g.setColour(juce::Colour(0xff20252c));
            g.fillRoundedRectangle(frame, 4.0f);
            g.setColour(juce::Colours::white.withAlpha(0.10f));
            g.drawRoundedRectangle(frame, 4.0f, 1.0f);
        };
        drawSection(audioFxRackBounds);
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
        sendRoute.setBounds(bounds.removeFromTop(controlHeight));
        sendLevel.setBounds(bounds.removeFromTop(controlHeight));
        sendsBounds = { bounds.getX(), sendsStart, bounds.getWidth(), bounds.getY() - sendsStart };

        const auto routingStart = bounds.getY();
        routingHeading.setBounds(bounds.removeFromTop(sectionHeaderHeight));
        outputRoute.setBounds(bounds.removeFromTop(controlHeight));
        routingBounds = { bounds.getX(), routingStart, bounds.getWidth(), bounds.getY() - routingStart };
    }
    else
    {
        for (auto& slot : audioFxSlots) slot.setVisible(false);
        addAudioFx.setVisible(false); sendsHeading.setVisible(false); sendRoute.setVisible(false); sendLevel.setVisible(false); routingHeading.setVisible(false); outputRoute.setVisible(false);
    }
    const auto dockHeight = juce::jlimit(130, 220, bounds.getHeight());
    auto dock = bounds.removeFromBottom(dockHeight);
    pan.setBounds(dock.removeFromTop(48));
    meter.setBounds(dock.removeFromLeft(12).reduced(1, 4));
    fader.setBounds(dock.reduced(4, 4));
}
