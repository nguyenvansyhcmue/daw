#include "InspectorPane.h"

#include "../AudioEngine/AudioEngine.h"
#include "../Models/TrackDataModel.h"

InspectorPane::InspectorPane(TrackDataModel* model, AudioEngine* engine)
    : trackModel(model), audioEngine(engine)
{
    title.setJustificationType(juce::Justification::centredLeft);
    title.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    title.setText("INSPECTOR", juce::dontSendNotification);
    title.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    for (auto* label : { &selectedStripTitle, &masterStripTitle, &eqSlot })
    {
        label->setJustificationType(juce::Justification::centred);
        label->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
        addAndMakeVisible(*label);
    }
    trackName.setJustificationType(juce::Justification::centred);
    trackName.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    trackName.setEditable(true, true, false);
    trackName.onTextChange = [this] { if (trackModel != nullptr) trackModel->setTrackName(static_cast<size_t>(selectedTrack), trackName.getText()); };

    volume.setRange(0.0, 2.0, 0.01); volume.setSliderStyle(juce::Slider::LinearVertical);
    volume.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 18);
    pan.setRange(-1.0, 1.0, 0.01); pan.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 18);
    volume.onValueChange = [this] { if (trackModel != nullptr) trackModel->setTrackVolume(static_cast<size_t>(selectedTrack), static_cast<float>(volume.getValue())); };
    pan.onValueChange = [this] { if (trackModel != nullptr) trackModel->setTrackPan(static_cast<size_t>(selectedTrack), static_cast<float>(pan.getValue())); };
    mute.onClick = [this] { if (trackModel != nullptr) trackModel->setTrackMuted(static_cast<size_t>(selectedTrack), mute.getToggleState()); };
    solo.onClick = [this] { if (trackModel != nullptr) trackModel->setTrackSolo(static_cast<size_t>(selectedTrack), solo.getToggleState()); };
    arm.onClick = [this] { if (trackModel != nullptr) trackModel->setTrackArmed(static_cast<size_t>(selectedTrack), arm.getToggleState()); };
    masterVolume.setRange(0.0, 2.0, 0.01); masterVolume.setSliderStyle(juce::Slider::LinearVertical);
    masterVolume.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 18);
    masterVolume.setValue(audioEngine != nullptr ? audioEngine->getMasterGain() : 1.0, juce::dontSendNotification);
    masterVolume.onValueChange = [this] { if (audioEngine != nullptr) audioEngine->setMasterGain(static_cast<float>(masterVolume.getValue())); };
    const std::array<juce::String, 4> insertNames { "MIDI FX", "ALCHEMY", "CHANNEL EQ", "DISTORTION" };
    for (size_t index = 0; index < insertSlots.size(); ++index)
    {
        insertSlots[index].setButtonText(insertNames[index]);
        insertSlots[index].setColour(juce::TextButton::buttonColourId, juce::Colour(0xff202020));
        addAndMakeVisible(insertSlots[index]);
    }
    addAndMakeVisible(title); addAndMakeVisible(trackName); addAndMakeVisible(volume); addAndMakeVisible(pan);
    addAndMakeVisible(trackMeter); addAndMakeVisible(masterVolume); addAndMakeVisible(masterMeter);
    addAndMakeVisible(mute); addAndMakeVisible(solo); addAndMakeVisible(arm);
    refreshControls();
    startTimerHz(30);
}

void InspectorPane::setSelectedTrack(int track) noexcept { selectedTrack = juce::jmax(0, track); refreshControls(); repaint(); }
void InspectorPane::refreshControls()
{
    if (trackModel == nullptr || selectedTrack >= static_cast<int>(trackModel->getTrackCount())) return;
    const auto& track = trackModel->getTrack(static_cast<size_t>(selectedTrack));
    trackName.setText(track.name.isNotEmpty() ? track.name : "Track " + juce::String(selectedTrack + 1), juce::dontSendNotification);
    volume.setValue(track.volume.load(), juce::dontSendNotification); pan.setValue(track.pan.load(), juce::dontSendNotification);
    mute.setToggleState(track.muted.load(), juce::dontSendNotification); solo.setToggleState(track.solo.load(), juce::dontSendNotification);
    arm.setToggleState(track.armed.load(), juce::dontSendNotification);
}
void InspectorPane::timerCallback()
{
    if (audioEngine == nullptr) return;
    trackMeter.updatePeak(audioEngine->getTrackPeak(static_cast<size_t>(selectedTrack)));
    masterMeter.updatePeak(audioEngine->getMasterPeak());
}
void InspectorPane::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2b2b2b));
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
    g.drawHorizontalLine(37, 8.0f, static_cast<float>(getWidth() - 8));
    g.drawVerticalLine(getWidth() / 2, 44.0f, static_cast<float>(getHeight() - 8));
}
void InspectorPane::resized()
{
    auto area = getLocalBounds().reduced(8); title.setBounds(area.removeFromTop(24));
    auto selected = area.removeFromLeft(area.getWidth() / 2 - 3); auto master = area.reduced(3, 0);
    selectedStripTitle.setBounds(selected.removeFromTop(18)); trackName.setBounds(selected.removeFromTop(28));
    eqSlot.setBounds(selected.removeFromTop(42).reduced(2));
    for (auto& insert : insertSlots) insert.setBounds(selected.removeFromTop(22).reduced(2, 1));
    auto buttons = selected.removeFromTop(28); arm.setBounds(buttons.removeFromLeft(50).reduced(1));
    mute.setBounds(buttons.removeFromLeft(40).reduced(1)); solo.setBounds(buttons.reduced(1));
    pan.setBounds(selected.removeFromBottom(42).reduced(2)); auto lowerSelected = selected.reduced(3, 4);
    trackMeter.setBounds(lowerSelected.removeFromLeft(14)); volume.setBounds(lowerSelected);
    masterStripTitle.setBounds(master.removeFromTop(18)); master.removeFromTop(24);
    auto lowerMaster = master.reduced(5, 4); masterMeter.setBounds(lowerMaster.removeFromLeft(16)); masterVolume.setBounds(lowerMaster);
}
