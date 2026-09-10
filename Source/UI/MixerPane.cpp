#include "MixerPane.h"
#include "../AudioEngine/AudioEngine.h"
#include "../AudioEngine/GainUtilityProcessor.h"

namespace
{
const auto panelMain = juce::Colour(0xff2b2b2b);
const auto panelDark = juce::Colour(0xff1a1a1a);
const auto accentBlue = juce::Colour(0xff00a8ff);
const auto accentCyan = juce::Colour(0xff00ffe0);
}

MixerPane::MixerPane(TrackDataModel* model, AudioEngine* engine)
    : trackModel(model), audioEngine(engine)
{
    startTimerHz(60);
    titleLabel.setText("Mixer", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    for (int i = 0; i < 8; ++i)
    {
        labels[i].setText("Ch " + juce::String(i + 1), juce::dontSendNotification);
        labels[i].setJustificationType(juce::Justification::centred);
        labels[i].setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.82f));
        labels[i].setFont(juce::Font(12.0f, juce::Font::plain));
        addAndMakeVisible(labels[i]);
        addAndMakeVisible(meters[i]);
        muteButtons[i].setButtonText("M");
        muteButtons[i].setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        muteButtons[i].setColour(juce::ToggleButton::tickColourId, accentCyan);
        addAndMakeVisible(muteButtons[i]);

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
                                !rack->bypass[slot].load(std::memory_order_acquire));
                    }
                });
            };
            addAndMakeVisible(button);
        }

        faders[i].setRange(0.0, 1.0, 0.01);
        faders[i].setValue(0.75 + (i * 0.03));
        faders[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        faders[i].setColour(juce::Slider::trackColourId, accentBlue);
        faders[i].setColour(juce::Slider::thumbColourId, accentCyan);
        addAndMakeVisible(faders[i]);
    }
}

void MixerPane::timerCallback()
{
    if (audioEngine == nullptr)
        return;

    for (size_t i = 0; i < meters.size(); ++i)
    {
        meters[i].updatePeak(audioEngine->getTrackPeak(i));
        const auto rack = trackModel != nullptr ? trackModel->getFxRackSnapshot(i) : nullptr;
        for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
            fxButtons[i][slot].setActive(rack != nullptr && rack->processors[slot] != nullptr);
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
}

void MixerPane::resized()
{
    auto area = getLocalBounds();
    titleLabel.setBounds(area.removeFromTop(24).reduced(8, 0));

    const auto stripWidth = (area.getWidth() - 20) / 8;
    for (int i = 0; i < 8; ++i)
    {
        auto strip = area.removeFromLeft(stripWidth);
        auto content = strip.reduced(6, 4);
        const auto nameHeight = 18;
        const auto muteHeight = 22;
        labels[i].setBounds(content.removeFromBottom(nameHeight));
        muteButtons[i].setBounds(content.removeFromBottom(muteHeight).reduced(4, 1));
        auto fxArea = content.removeFromTop(80).reduced(2, 2);
        const auto fxHeight = juce::jmax(1, fxArea.getHeight() / static_cast<int>(TrackDataModel::maxFxSlots));
        for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
            fxButtons[static_cast<size_t>(i)][slot].setBounds(fxArea.removeFromTop(fxHeight).reduced(1, 1));
        auto controlRow = content.reduced(4, 0).withHeight(70).withY(content.getCentreY() - 35);
        meters[i].setBounds(controlRow.removeFromLeft(controlRow.getWidth() / 3).reduced(2, 0));
        faders[i].setBounds(controlRow.reduced(4, 0));
    }
}
