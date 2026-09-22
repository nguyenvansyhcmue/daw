#include "VocalistRackComponent.h"
#include "StudioForgeDialog.h"

VocalistRackComponent::VocalistRackComponent(AudioEngine& engine, PluginHostService& host)
    : audioEngine(engine), pluginHost(host)
{
    addAndMakeVisible(title);
    addAndMakeVisible(addVocalist);
    for (size_t slot = 0; slot < fxSlots.size(); ++slot)
    {
        fxSlots[slot].setButtonText("FX " + juce::String(static_cast<int>(slot + 1)) + "  + Add Plugin");
        fxSlots[slot].onClick = [this, slot] { selectedFxSlot = slot; showFxMenu(slot); };
        addAndMakeVisible(fxSlots[slot]);
    }
    addAndMakeVisible(masterFx);
    masterFx.onClick = [this] { selectedVocalist = -1; refresh(); };
    for (size_t slot = 0; slot < fxSlots.size(); ++slot)
    {
        fxSlots[slot].setButtonText("FX " + juce::String(static_cast<int>(slot + 1)) + "  + Add Plugin");
        addAndMakeVisible(fxSlots[slot]);
    }
    addVocalist.onClick = [this]
    {
        uint32_t id = 0;
        audioEngine.addVocalist(static_cast<int>(audioEngine.getVocalistCount()), id);
        refresh();
    };
    for (size_t i = 0; i < strips.size(); ++i)
    {
        strips[i].setButtonText("Mic " + juce::String(static_cast<int>(i + 1)));
        strips[i].onClick = [this, i] { selectedVocalist = static_cast<int>(i); refresh(); };
        addAndMakeVisible(strips[i]);
        gains[i].setRange(0.0, 2.0, 0.01);
        gains[i].setValue(1.0, juce::dontSendNotification);
        gains[i].setSliderStyle(juce::Slider::LinearVertical);
        gains[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        gains[i].onValueChange = [this, i] { audioEngine.setVocalistGain(i, static_cast<float>(gains[i].getValue())); };
        addAndMakeVisible(gains[i]);
        mutes[i].setButtonText("M");
        mutes[i].onClick = [this, i] { audioEngine.setVocalistMuted(i, mutes[i].getToggleState()); };
        addAndMakeVisible(mutes[i]);
        for (int channel = 1; channel <= 8; ++channel)
            inputs[i].addItem("In " + juce::String(channel), channel);
        inputs[i].onChange = [this, i]
        {
            if (inputs[i].getSelectedId() > 0)
                audioEngine.setVocalistInputChannel(i, inputs[i].getSelectedId() - 1);
        };
        addAndMakeVisible(inputs[i]);
        addAndMakeVisible(meters[i]);
        sends[i].setRange(0.0, 1.0, 0.01);
        sends[i].setSliderStyle(juce::Slider::LinearVertical);
        sends[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        sends[i].setTooltip("Shared Aux Send");
        sends[i].onValueChange = [this, i] { audioEngine.setVocalistSend(i, static_cast<float>(sends[i].getValue())); };
        addAndMakeVisible(sends[i]);
    }
    if (audioEngine.getVocalistCount() == 0)
    {
        uint32_t hostId = 0;
        audioEngine.addVocalist(0, hostId);
    }
    startTimerHz(10);
    refresh();
}

void VocalistRackComponent::refresh()
{
    const auto count = audioEngine.getVocalistCount();
    for (auto& slot : fxSlots)
        slot.setEnabled(selectedVocalist >= 0 && selectedVocalist < static_cast<int>(count));
    for (size_t i = 0; i < strips.size(); ++i)
    {
        strips[i].setVisible(i < count);
        gains[i].setVisible(i < count);
        mutes[i].setVisible(i < count);
        inputs[i].setVisible(i < count);
        meters[i].setVisible(i < count);
        sends[i].setVisible(i < count);
        meters[i].updatePeak(audioEngine.getVocalistPeak(i));
        if (i < count)
        {
            strips[i].setButtonText("Mic " + juce::String(static_cast<int>(i + 1)));
            VocalistConfig config;
            if (audioEngine.getVocalistConfig(i, config))
            {
                inputs[i].setSelectedId(config.inputChannel + 1, juce::dontSendNotification);
                mutes[i].setToggleState(config.muted, juce::dontSendNotification);
                gains[i].setValue(config.gain, juce::dontSendNotification);
            }
        }
    }
    resized();
}

void VocalistRackComponent::timerCallback() { refresh(); }
void VocalistRackComponent::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff182028)); }

void VocalistRackComponent::showPluginBrowser()
{
    const auto vocalistIndex = static_cast<size_t>(juce::jmax(0, selectedVocalist));
    auto* panel = new PluginBrowserPanel(pluginHost, [this, vocalistIndex] (const juce::PluginDescription& description)
    {
        juce::String error;
        auto* device = audioEngine.getAudioDeviceManager().getCurrentAudioDevice();
        const auto sampleRate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
        const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 128;
        if (auto effect = pluginHost.createEffect(description, sampleRate, blockSize, error))
            audioEngine.setVocalistFxProcessor(vocalistIndex, selectedFxSlot, std::move(effect));
        else
            StudioForgeDialog::showWarning(StudioForgeDialog::fromUtf8("Kh\u00F4ng th\u1EC3 m\u1EDF plugin"), error);
    });
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(panel);
    options.dialogTitle = "Add Vocalist Plugin";
    options.dialogBackgroundColour = juce::Colour(0xff20242a);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void VocalistRackComponent::showFxMenu(size_t slot)
{
    if (selectedVocalist < 0) return;
    const auto vocalistIndex = static_cast<size_t>(selectedVocalist);
    const auto processor = audioEngine.getVocalistFxProcessor(vocalistIndex, slot);
    if (processor == nullptr)
    {
        showPluginBrowser();
        return;
    }

    juce::PopupMenu menu;
    menu.addItem(1, "Open Editor", processor->hasEditor());
    menu.addItem(2, "Bypass");
    menu.addItem(3, "Remove");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&fxSlots[slot]),
                       [this, vocalistIndex, slot, processor] (int choice)
    {
        if (choice == 2)
            audioEngine.setVocalistFxBypassed(vocalistIndex, slot, true);
        else if (choice == 3)
            audioEngine.setVocalistFxProcessor(vocalistIndex, slot, nullptr);
        else if (choice == 1)
        {
            if (auto editor = processor->createEditor())
            {
                juce::DialogWindow::LaunchOptions options;
                options.content.setOwned(editor.release());
                options.dialogTitle = processor->getName();
                options.dialogBackgroundColour = juce::Colour(0xff20242a);
                options.useNativeTitleBar = true;
                options.resizable = true;
                options.launchAsync();
            }
        }
    });
}

void VocalistRackComponent::resized()
{
    auto area = getLocalBounds().reduced(6);
    title.setBounds(area.removeFromLeft(120));
    addVocalist.setBounds(area.removeFromRight(130).reduced(1));
    masterFx.setBounds(area.removeFromRight(90).reduced(1));
    for (size_t i = 0; i < strips.size(); ++i)
        if (strips[i].isVisible())
        {
            auto stripArea = area.removeFromLeft(72).reduced(2);
            strips[i].setBounds(stripArea.removeFromTop(20));
            inputs[i].setBounds(stripArea.removeFromTop(20));
            meters[i].setBounds(stripArea.removeFromRight(10));
            sends[i].setBounds(stripArea.removeFromRight(10));
            mutes[i].setBounds(stripArea.removeFromBottom(20));
            gains[i].setBounds(stripArea);
        }
    auto fxArea = getLocalBounds().removeFromBottom(28).reduced(6);
    for (auto& slot : fxSlots)
        slot.setBounds(fxArea.removeFromLeft(112).reduced(1));
}
