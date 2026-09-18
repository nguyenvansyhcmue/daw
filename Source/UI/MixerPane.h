#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioPeakMeter.h"
#include "../Models/TrackDataModel.h"

class PluginHostService;

class AudioEngine;

class FxSlotButton final : public juce::TextButton
{
public:
    FxSlotButton() = default;

    void setActive(bool shouldBeActive) noexcept
    {
        active = shouldBeActive;
        repaint();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (event.mods.isRightButtonDown())
        {
            if (onClick != nullptr)
                onClick();
            return;
        }

        juce::TextButton::mouseUp(event);
    }

private:
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0xff1a1a1a));
        g.fillRoundedRectangle(bounds, 3.0f);

        if (active)
        {
            juce::ColourGradient outline(juce::Colour(0xff00e5ff), bounds.getX(), bounds.getY(),
                                          juce::Colour(0xff00a8ff), bounds.getRight(), bounds.getBottom(), false);
            g.setGradientFill(outline);
            g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
        }
        else
        {
            g.setColour(juce::Colour(0xff333333));
            g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
        }

        g.setColour(juce::Colours::white.withAlpha(highlighted || down ? 1.0f : 0.8f));
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    }

    bool active = false;
};

class MixerPane final : public juce::Component, private juce::Timer
{
public:
    explicit MixerPane(TrackDataModel* model = nullptr, AudioEngine* engine = nullptr,
                       PluginHostService* pluginHost = nullptr);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void refreshFromModel();

private:
    void timerCallback() override;
    void showPluginBrowser(int trackIndex, size_t slot);

    TrackDataModel* trackModel = nullptr;
    AudioEngine* audioEngine = nullptr;
    PluginHostService* pluginHostService = nullptr;
    std::unique_ptr<juce::FileChooser> pluginFileChooser;
    std::array<AudioPeakMeter, TrackDataModel::maxTracks> meters;
    std::array<juce::Slider, TrackDataModel::maxTracks> faders;
    std::array<juce::Slider, TrackDataModel::maxTracks> panSliders;
    std::array<juce::ToggleButton, TrackDataModel::maxTracks> muteButtons;
    std::array<juce::ToggleButton, TrackDataModel::maxTracks> soloButtons;
    std::array<std::array<FxSlotButton, TrackDataModel::maxFxSlots>, TrackDataModel::maxTracks> fxButtons;
    std::array<juce::Label, TrackDataModel::maxTracks> labels;
    std::array<AudioPeakMeter, TrackDataModel::maxBuses> busMeters;
    std::array<juce::Slider, TrackDataModel::maxBuses> busFaders;
    std::array<juce::ToggleButton, TrackDataModel::maxBuses> busMuteButtons;
    std::array<std::array<FxSlotButton, TrackDataModel::maxFxSlots>, TrackDataModel::maxBuses> busFxButtons;
    std::array<juce::Label, TrackDataModel::maxBuses> busLabels;
    juce::Label titleLabel;
    juce::Label busTitleLabel;
};
