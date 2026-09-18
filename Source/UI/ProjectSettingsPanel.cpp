#include "ProjectSettingsPanel.h"

#include "../Models/TrackDataModel.h"

ProjectSettingsPanel::ProjectSettingsPanel(TrackDataModel& model)
    : trackDataModel(model)
{
    for (auto* label : { &tempoLabel, &signatureLabel })
    {
        label->setJustificationType(juce::Justification::centredLeft);
        label->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.82f));
        addAndMakeVisible(*label);
    }

    tempo.setRange(40.0, 220.0, 0.1);
    tempo.setValue(trackDataModel.getBpm(), juce::dontSendNotification);
    tempo.setTextBoxStyle(juce::Slider::TextBoxRight, false, 68, 24);
    tempo.setColour(juce::Slider::trackColourId, juce::Colour(0xff6f88a8));
    tempo.setColour(juce::Slider::thumbColourId, juce::Colour(0xffa7c5df));
    addAndMakeVisible(tempo);

    for (int beats = 1; beats <= 12; ++beats)
        beatsPerBar.addItem(juce::String(beats) + "/4", beats);
    beatsPerBar.setSelectedId(trackDataModel.getTimeSignatureNumerator(), juce::dontSendNotification);
    addAndMakeVisible(beatsPerBar);

    cancel.onClick = [this] { close(); };
    apply.onClick = [this] { applyAndClose(); };
    addAndMakeVisible(cancel);
    addAndMakeVisible(apply);
}

void ProjectSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced(18);
    constexpr int rowHeight = 30;
    tempoLabel.setBounds(area.removeFromTop(rowHeight));
    tempo.setBounds(area.removeFromTop(rowHeight).reduced(0, 2));
    area.removeFromTop(10);
    signatureLabel.setBounds(area.removeFromTop(rowHeight));
    beatsPerBar.setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(14);
    auto buttons = area.removeFromBottom(30);
    cancel.setBounds(buttons.removeFromRight(84));
    buttons.removeFromRight(8);
    apply.setBounds(buttons.removeFromRight(84));
}

void ProjectSettingsPanel::applyAndClose()
{
    trackDataModel.setBpm(tempo.getValue());
    trackDataModel.setTimeSignatureNumerator(beatsPerBar.getSelectedId());
    close();
}

void ProjectSettingsPanel::close()
{
    if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
        dialog->exitModalState(0);
}
