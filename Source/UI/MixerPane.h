#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioPeakMeter.h"
#include "../Models/TrackDataModel.h"

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
    explicit MixerPane(TrackDataModel* model = nullptr, AudioEngine* engine = nullptr);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    TrackDataModel* trackModel = nullptr;
    AudioEngine* audioEngine = nullptr;
    std::array<AudioPeakMeter, 8> meters;
    std::array<juce::Slider, 8> faders;
    std::array<juce::ToggleButton, 8> muteButtons;
    std::array<std::array<FxSlotButton, TrackDataModel::maxFxSlots>, 8> fxButtons;
    std::array<juce::Label, 8> labels;
    juce::Label titleLabel;
};
