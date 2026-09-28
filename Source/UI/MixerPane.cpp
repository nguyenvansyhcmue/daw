#include "MixerPane.h"
#include "../AudioEngine/AudioEngine.h"
#include "../AudioEngine/GainUtilityProcessor.h"
#include "../Plugins/PluginHostService.h"
#include "PluginBrowserPanel.h"

namespace
{
const auto panelMain = juce::Colour(0xff2b2b2b);
const auto panelDark = juce::Colour(0xff1a1a1a);
const auto accentBlue = juce::Colour(0xff00a8ff);
const auto accentCyan = juce::Colour(0xff00ffe0);
const auto stripSurface = juce::Colour(0xff202428);
const auto stripBorder = juce::Colour(0xff53616d);

template <typename ComponentArray>
void setComponentsVisible(ComponentArray& components, bool visible)
{
    for (auto& component : components)
        component.setVisible(visible);
}
}

MixerPane::MixerPane(TrackDataModel* model, AudioEngine* engine, PluginHostService* pluginHost)
    : trackModel(model), audioEngine(engine), pluginHostService(pluginHost)
{
    startTimerHz(30);
    titleLabel.setText("MIXER", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel);
    busTitleLabel.setText("AUX RETURNS", juce::dontSendNotification);
    busTitleLabel.setJustificationType(juce::Justification::centredLeft);
    busTitleLabel.setColour(juce::Label::textColourId, accentCyan.withAlpha(0.85f));
    busTitleLabel.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    addAndMakeVisible(busTitleLabel);

    for (int i = 0; i < static_cast<int>(TrackDataModel::maxTracks); ++i)
    {
        labels[i].setText("Ch " + juce::String(i + 1), juce::dontSendNotification);
        labels[i].setJustificationType(juce::Justification::centred);
        labels[i].setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.82f));
        labels[i].setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::plain)));
        addAndMakeVisible(labels[i]);
        addAndMakeVisible(meters[i]);
        muteButtons[i].setButtonText("M");
        muteButtons[i].setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        muteButtons[i].setColour(juce::ToggleButton::tickColourId, accentCyan);
        muteButtons[i].setToggleState(trackModel != nullptr && static_cast<size_t>(i) < trackModel->getTrackCount()
                                          && trackModel->getTrack(static_cast<size_t>(i)).muted.load(),
                                      juce::dontSendNotification);
        muteButtons[i].onClick = [this, i]
        {
            if (trackModel != nullptr)
                trackModel->setTrackMuted(static_cast<size_t>(i), muteButtons[i].getToggleState());
        };
        addAndMakeVisible(muteButtons[i]);

        soloButtons[i].setButtonText("S");
        soloButtons[i].setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        soloButtons[i].setColour(juce::ToggleButton::tickColourId, juce::Colour(0xffffd166));
        soloButtons[i].setToggleState(trackModel != nullptr && static_cast<size_t>(i) < trackModel->getTrackCount()
                                          && trackModel->getTrack(static_cast<size_t>(i)).solo.load(),
                                      juce::dontSendNotification);
        soloButtons[i].onClick = [this, i]
        {
            if (trackModel != nullptr)
                trackModel->setTrackSolo(static_cast<size_t>(i), soloButtons[i].getToggleState());
        };
        addAndMakeVisible(soloButtons[i]);

        for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
        {
            auto& button = fxButtons[static_cast<size_t>(i)][slot];
            button.setButtonText("FX " + juce::String(static_cast<int>(slot + 1)));
            button.setColour(juce::TextButton::buttonColourId, panelDark);
            button.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.8f));
            button.onClick = [this, i, slot]
            {
                juce::PopupMenu menu;
                menu.addItem(1, "None");
                menu.addItem(2, "Gain Utility (-6dB)");
                menu.addItem(3, "Bypass Toggle");
                menu.addItem(4, "Load VST3...");
                menu.showMenuAsync(juce::PopupMenu::Options {},
                                   [this, i, slot](int result)
                {
                    if (audioEngine == nullptr)
                        return;

                    if (result == 1)
                        audioEngine->setFxProcessor(static_cast<size_t>(i), slot, nullptr);
                    else if (result == 2)
                        audioEngine->setFxProcessor(static_cast<size_t>(i), slot,
                                                    std::make_shared<GainUtilityProcessor>());
                    else if (result == 3)
                    {
                        const auto rack = trackModel != nullptr
                            ? trackModel->getFxRackSnapshot(static_cast<size_t>(i)) : nullptr;
                        if (rack != nullptr)
                            audioEngine->setFxBypassed(static_cast<size_t>(i), slot,
                                !rack->bypass[slot]);
                    }
                    else if (result == 4 && pluginHostService != nullptr)
                    {
                        pluginFileChooser = std::make_unique<juce::FileChooser>("Load VST3 plug-in", juce::File {}, "*.vst3");
                        pluginFileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                                            | juce::FileBrowserComponent::canSelectFiles
                                                            | juce::FileBrowserComponent::canSelectDirectories,
                            [this, i, slot](const juce::FileChooser& chooser)
                            {
                                const auto selected = chooser.getResult();
                                if (pluginHostService == nullptr || ! selected.exists())
                                {
                                    pluginFileChooser.reset();
                                    return;
                                }

                                pluginFileChooser.reset();
                                const juce::Component::SafePointer<MixerPane> safeThis(this);
                                pluginHostService->scanVst3Async(selected, [safeThis, i, slot](juce::Result result)
                                {
                                    if (safeThis != nullptr && result.wasOk())
                                        safeThis->showPluginBrowser(i, slot);
                                });
                            });
                    }
                });
            };
            addAndMakeVisible(button);
        }

        faders[i].setRange(0.0, 2.0, 0.01);
        faders[i].setSliderStyle(juce::Slider::LinearVertical);
        faders[i].setValue(trackModel != nullptr
                                ? trackModel->getTrack(static_cast<size_t>(i)).volume.load() : 1.0,
                            juce::dontSendNotification);
        faders[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        faders[i].setColour(juce::Slider::trackColourId, accentBlue);
        faders[i].setColour(juce::Slider::thumbColourId, accentCyan);
        faders[i].onValueChange = [this, i]
        {
            if (trackModel != nullptr)
                trackModel->setTrackVolume(static_cast<size_t>(i), static_cast<float>(faders[i].getValue()));
        };
        addAndMakeVisible(faders[i]);

        panSliders[i].setRange(-1.0, 1.0, 0.01);
        panSliders[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        panSliders[i].setValue(trackModel != nullptr
                                   ? trackModel->getTrack(static_cast<size_t>(i)).pan.load() : 0.0,
                               juce::dontSendNotification);
        panSliders[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        panSliders[i].setColour(juce::Slider::trackColourId, accentBlue);
        panSliders[i].setColour(juce::Slider::thumbColourId, accentCyan);
        panSliders[i].onValueChange = [this, i]
        {
            if (trackModel != nullptr)
                trackModel->setTrackPan(static_cast<size_t>(i), static_cast<float>(panSliders[i].getValue()));
        };
        addAndMakeVisible(panSliders[i]);
    }

    for (int i = 0; i < static_cast<int>(TrackDataModel::maxBuses); ++i)
    {
        busLabels[i].setJustificationType(juce::Justification::centred);
        busLabels[i].setColour(juce::Label::textColourId, accentCyan.withAlpha(0.9f));
        busLabels[i].setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        addAndMakeVisible(busLabels[i]);
        addAndMakeVisible(busMeters[i]);

        busFaders[i].setRange(0.0, 2.0, 0.01);
        busFaders[i].setSliderStyle(juce::Slider::LinearVertical);
        busFaders[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        busFaders[i].setColour(juce::Slider::trackColourId, accentBlue);
        busFaders[i].setColour(juce::Slider::thumbColourId, accentCyan);
        busFaders[i].onValueChange = [this, i]
        {
            if (trackModel == nullptr || static_cast<size_t>(i) >= trackModel->getBuses().size())
                return;
            trackModel->setBusGain(trackModel->getBuses()[static_cast<size_t>(i)].id,
                                    static_cast<float>(busFaders[i].getValue()));
        };
        addAndMakeVisible(busFaders[i]);

        busMuteButtons[i].setButtonText("M");
        busMuteButtons[i].setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        busMuteButtons[i].setColour(juce::ToggleButton::tickColourId, accentCyan);
        busMuteButtons[i].onClick = [this, i]
        {
            if (trackModel == nullptr || static_cast<size_t>(i) >= trackModel->getBuses().size())
                return;
            trackModel->setBusMuted(trackModel->getBuses()[static_cast<size_t>(i)].id,
                                    busMuteButtons[i].getToggleState());
        };
        addAndMakeVisible(busMuteButtons[i]);

        for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
        {
            auto& button = busFxButtons[static_cast<size_t>(i)][slot];
            button.setButtonText("FX " + juce::String(static_cast<int>(slot + 1)));
            button.onClick = [this, i, slot]
            {
                if (trackModel == nullptr || static_cast<size_t>(i) >= trackModel->getBuses().size())
                    return;
                const auto bus = trackModel->getBuses()[static_cast<size_t>(i)].id;
                juce::PopupMenu menu;
                menu.addItem(1, "None");
                menu.addItem(2, "Gain Utility (-6dB)");
                menu.addItem(3, "Bypass Toggle");
                menu.showMenuAsync(juce::PopupMenu::Options {}, [this, bus, slot](int result)
                {
                    if (trackModel == nullptr)
                        return;
                    if (result == 1) trackModel->setBusFxProcessor(bus, slot, nullptr);
                    else if (result == 2) trackModel->setBusFxProcessor(bus, slot, std::make_shared<GainUtilityProcessor>());
                    else if (result == 3)
                    {
                        if (const auto* rack = trackModel->getBusFxRackSnapshot(bus); rack != nullptr)
                            trackModel->setBusFxBypassed(bus, slot, ! rack->bypass[slot]);
                    }
                });
            };
            addAndMakeVisible(button);
        }
    }

    refreshFromModel();
}

void MixerPane::showPluginBrowser(int trackIndex, size_t slot)
{
    if (pluginHostService == nullptr || audioEngine == nullptr || trackIndex < 0)
        return;

    auto* panel = new PluginBrowserPanel(*pluginHostService, [safeThis = juce::Component::SafePointer<MixerPane>(this), trackIndex, slot]
                                         (const juce::PluginDescription& description)
    {
        if (safeThis == nullptr || safeThis->audioEngine == nullptr || safeThis->pluginHostService == nullptr)
            return;

        juce::String error;
        auto* device = safeThis->audioEngine->getAudioDeviceManager().getCurrentAudioDevice();
        const auto sampleRate = device != nullptr ? device->getCurrentSampleRate()
                                                  : (safeThis->trackModel != nullptr ? safeThis->trackModel->getSampleRate() : 44100.0);
        const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
        if (auto effect = safeThis->pluginHostService->createEffect(description, sampleRate, blockSize, error))
            safeThis->audioEngine->setFxProcessor(static_cast<size_t>(trackIndex), slot, std::move(effect));
    });

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(panel);
    options.dialogTitle = "Plug-in Browser";
    options.dialogBackgroundColour = juce::Colour(0xff25282d);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.launchAsync();
}

void MixerPane::refreshFromModel()
{
    const auto count = trackModel != nullptr ? trackModel->getTrackCount() : 0;
    const auto& buses = trackModel != nullptr ? trackModel->getBuses() : std::vector<TrackDataModel::BusState> {};
    const auto layoutChanged = displayedTrackCount != static_cast<int>(count)
        || displayedBusCount != static_cast<int>(buses.size());
    displayedTrackCount = static_cast<int>(count);
    displayedBusCount = static_cast<int>(buses.size());
    for (size_t index = 0; index < faders.size(); ++index)
    {
        const auto active = index < count;
        const auto& track = active ? trackModel->getTrack(index) : TrackDataModel::TrackState {};
        labels[index].setText(active ? (track.name.isNotEmpty() ? track.name
                                                                : "Ch " + juce::String(static_cast<int>(index + 1))) : "—",
                              juce::dontSendNotification);
        faders[index].setValue(active ? track.volume.load(std::memory_order_relaxed) : 1.0,
                                juce::dontSendNotification);
        panSliders[index].setValue(active ? track.pan.load(std::memory_order_relaxed) : 0.0,
                                   juce::dontSendNotification);
        muteButtons[index].setToggleState(active && track.muted.load(std::memory_order_relaxed),
                                          juce::dontSendNotification);
        soloButtons[index].setToggleState(active && track.solo.load(std::memory_order_relaxed),
                                          juce::dontSendNotification);
        faders[index].setEnabled(active);
        panSliders[index].setEnabled(active);
        muteButtons[index].setEnabled(active);
        soloButtons[index].setEnabled(active);
        setComponentsVisible(fxButtons[index], active);
        for (size_t slot = 0; slot < fxButtons[index].size(); ++slot)
        {
            auto& button = fxButtons[index][slot];
            button.setEnabled(active);
            const auto rack = active ? trackModel->getFxRackSnapshot(index) : nullptr;
            const auto processor = rack != nullptr ? rack->processors[slot] : nullptr;
            button.setButtonText(processor != nullptr ? processor->getName().substring(0, 12) : "+ FX");
        }
        labels[index].setVisible(active);
        meters[index].setVisible(active);
        faders[index].setVisible(active);
        panSliders[index].setVisible(active);
        muteButtons[index].setVisible(active);
        soloButtons[index].setVisible(active);
    }

    for (size_t index = 0; index < busFaders.size(); ++index)
    {
        const auto active = index < buses.size();
        busLabels[index].setText(active ? "Bus " + juce::String(static_cast<int>(index + 1)) : "—",
                                 juce::dontSendNotification);
        busFaders[index].setValue(active ? buses[index].gain : 1.0f, juce::dontSendNotification);
        busMuteButtons[index].setToggleState(active && buses[index].muted, juce::dontSendNotification);
        busFaders[index].setEnabled(active);
        busMuteButtons[index].setEnabled(active);
        setComponentsVisible(busFxButtons[index], active);
        for (size_t slot = 0; slot < busFxButtons[index].size(); ++slot)
        {
            auto& button = busFxButtons[index][slot];
            button.setEnabled(active);
            const auto* rack = active ? trackModel->getBusFxRackSnapshot(buses[index].id) : nullptr;
            const auto processor = rack != nullptr ? rack->processors[slot] : nullptr;
            button.setButtonText(processor != nullptr ? processor->getName().substring(0, 12) : "+ FX");
        }
        busLabels[index].setVisible(active);
        busMeters[index].setVisible(active);
        busFaders[index].setVisible(active);
        busMuteButtons[index].setVisible(active);
    }

    busTitleLabel.setVisible(! buses.empty());
    if (layoutChanged)
        resized();
}

void MixerPane::timerCallback()
{
    if (audioEngine == nullptr)
        return;

    refreshFromModel();

    for (size_t i = 0; i < meters.size(); ++i)
    {
        meters[i].updatePeak(audioEngine->getTrackPeak(i));
        const auto rack = trackModel != nullptr ? trackModel->getFxRackSnapshot(i) : nullptr;
        for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
            fxButtons[i][slot].setActive(rack != nullptr && rack->processors[slot] != nullptr);
    }

    if (trackModel != nullptr)
        for (size_t i = 0; i < busMeters.size(); ++i)
        {
            busMeters[i].updatePeak(audioEngine->getBusPeak(i));
            const auto& buses = trackModel->getBuses();
            const auto* rack = i < buses.size() ? trackModel->getBusFxRackSnapshot(buses[i].id) : nullptr;
            for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
                busFxButtons[i][slot].setActive(rack != nullptr && rack->processors[slot] != nullptr);
        }

}

void MixerPane::paint(juce::Graphics& g)
{
    g.fillAll(panelMain);

    const auto rect = getLocalBounds().toFloat();
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawLine(rect.getX(), rect.getY(), rect.getRight(), rect.getY(), 1.0f);
    g.setColour(accentBlue.withAlpha(0.35f));
    g.drawLine(rect.getX(), rect.getY() + 1.0f, rect.getRight(), rect.getY() + 1.0f, 1.0f);

    const auto drawStrip = [&g] (juce::Rectangle<int> bounds, juce::Colour accent)
    {
        if (bounds.isEmpty())
            return;
        auto card = bounds.toFloat().reduced(2.0f, 1.0f);
        g.setColour(stripSurface);
        g.fillRoundedRectangle(card, 5.0f);
        g.setColour(stripBorder.withAlpha(0.42f));
        g.drawRoundedRectangle(card, 5.0f, 1.0f);
        g.setColour(accent.withAlpha(0.86f));
        g.fillRoundedRectangle(card.removeFromTop(2.0f), 1.0f);
    };
    for (const auto& bounds : trackStripBounds)
        drawStrip(bounds, accentBlue);
    for (const auto& bounds : busStripBounds)
        drawStrip(bounds, accentCyan);
}

void MixerPane::resized()
{
    auto area = getLocalBounds();
    titleLabel.setBounds(area.removeFromTop(24).reduced(8, 0));
    trackStripBounds.fill({});
    busStripBounds.fill({});
    const auto trackCount = trackModel != nullptr ? static_cast<int>(trackModel->getTrackCount()) : 0;
    const auto busCount = trackModel != nullptr ? static_cast<int>(trackModel->getBuses().size()) : 0;
    const auto busWidth = busCount > 0 ? juce::jlimit(120, 280, area.getWidth() / 3) : 0;
    auto busArea = busCount > 0 ? area.removeFromRight(busWidth) : juce::Rectangle<int> {};
    busTitleLabel.setBounds(busArea.removeFromTop(22).reduced(8, 0));
    auto trackArea = area.reduced(8, 3);

    // A mixer owns the whole lower dock. Split its available width across the
    // active channels instead of leaving a dead panel after the last strip.
    const auto stripWidth = trackCount > 0
        ? juce::jmax(1, trackArea.getWidth() / trackCount) : trackArea.getWidth();
    for (int i = 0; i < trackCount; ++i)
    {
        auto strip = trackArea.removeFromLeft(stripWidth);
        trackStripBounds[static_cast<size_t>(i)] = strip;
        auto content = strip.reduced(8, 5);
        const auto nameHeight = 20;
        const auto buttonHeight = 21;
        labels[i].setBounds(content.removeFromBottom(nameHeight));
        auto muteSoloArea = content.removeFromBottom(buttonHeight);
        muteButtons[i].setBounds(muteSoloArea.removeFromLeft(muteSoloArea.getWidth() / 2).reduced(2, 1));
        soloButtons[i].setBounds(muteSoloArea.reduced(2, 1));
        auto panArea = content.removeFromBottom(30);
        panSliders[i].setBounds(panArea.withSizeKeepingCentre(30, 30));
        auto fxArea = content.removeFromTop(62).reduced(1, 1);
        const auto fxHeight = juce::jmax(1, fxArea.getHeight() / 4);
        for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
        {
            const auto column = slot % 2;
            const auto row = slot / 2;
            fxButtons[static_cast<size_t>(i)][slot].setBounds(fxArea.getX() + column * fxArea.getWidth() / 2,
                                                               fxArea.getY() + static_cast<int>(row) * fxHeight,
                                                               fxArea.getWidth() / 2, fxHeight);
        }
        // Keep the physical fader and meter compact even when a wide display
        // gives a channel more room; the surrounding card preserves a clear
        // channel grouping rather than turning the fader into a giant block.
        auto controlRow = content.reduced(5, 2);
        controlRow = controlRow.withWidth(juce::jmin(142, controlRow.getWidth()))
                               .withCentre(controlRow.getCentre());
        meters[i].setBounds(controlRow.removeFromLeft(16).reduced(1, 0));
        faders[i].setBounds(controlRow.reduced(1, 0));
    }

    auto returns = busArea.reduced(5, 3);
    const auto busStripWidth = busCount > 0 ? juce::jmax(96, returns.getWidth() / busCount) : 96;
    for (int i = 0; i < busCount; ++i)
    {
        auto strip = returns.removeFromLeft(busStripWidth);
        busStripBounds[static_cast<size_t>(i)] = strip;
        strip.reduce(7, 5);
        busLabels[i].setBounds(strip.removeFromBottom(18));
        busMuteButtons[i].setBounds(strip.removeFromBottom(22).reduced(3, 1));
        auto fxArea = strip.removeFromTop(62);
        const auto fxHeight = juce::jmax(1, fxArea.getHeight() / 4);
        for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
            busFxButtons[static_cast<size_t>(i)][slot].setBounds(fxArea.getX() + static_cast<int>(slot % 2) * fxArea.getWidth() / 2,
                                                                  fxArea.getY() + static_cast<int>(slot / 2) * fxHeight,
                                                                  fxArea.getWidth() / 2, fxHeight);
        auto faderArea = strip.reduced(6, 2);
        busMeters[i].setBounds(faderArea.removeFromLeft(14));
        busFaders[i].setBounds(faderArea);
    }
}
