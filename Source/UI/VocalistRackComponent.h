#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../AudioEngine/AudioEngine.h"
#include "AudioPeakMeter.h"
#include "PluginBrowserPanel.h"

class VocalistRackComponent final : public juce::Component, private juce::Timer
{
public:
    VocalistRackComponent(AudioEngine& engine, PluginHostService& pluginHost);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    AudioEngine& audioEngine;
    PluginHostService& pluginHost;
    juce::TextButton addVocalist { "+ Add Vocalist" };
    juce::Label title { {}, "VOCALIST RACK" };
    std::array<juce::TextButton, VocalistRackSnapshot::maxVocalists> strips;
    std::array<juce::Slider, VocalistRackSnapshot::maxVocalists> gains;
    std::array<juce::TextButton, VocalistRackSnapshot::maxVocalists> mutes;
    std::array<juce::ComboBox, VocalistRackSnapshot::maxVocalists> inputs;
    std::array<AudioPeakMeter, VocalistRackSnapshot::maxVocalists> meters;
    std::array<juce::Slider, VocalistRackSnapshot::maxVocalists> sends;
    size_t selectedFxSlot = 0;
    void showPluginBrowser();
    void showFxMenu(size_t slot);
    std::array<juce::TextButton, TrackDataModel::maxFxSlots> fxSlots;
    juce::TextButton masterFx { "MASTER FX" };
    int selectedVocalist = 0;
};
