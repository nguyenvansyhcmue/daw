#include "ControlBar.h"
#include "../Models/TrackDataModel.h"

namespace
{
const auto darkPanel = juce::Colour(0xff2b2b2b);
const auto accentBlue = juce::Colour(0xff00a8ff);
const auto accentCyan = juce::Colour(0xff00ffe0);
}

ControlBar::ControlBar(TrackDataModel* model)
    : trackModel(model)
{
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(recordButton);
    addAndMakeVisible(metronomeButton);
    addAndMakeVisible(bpmSlider);
    addAndMakeVisible(inspectorButton);
    addAndMakeVisible(mixerButton);
    addAndMakeVisible(timecodeLabel);
    addAndMakeVisible(pointerTool);
    addAndMakeVisible(scissorsTool);
    pointerTool.setClickingTogglesState(true);
    scissorsTool.setClickingTogglesState(true);
    pointerTool.onClick = [this]
    {
        if (trackModel != nullptr) trackModel->setActiveTool(TrackDataModel::EditTool::Select);
        pointerTool.setToggleState(true, juce::dontSendNotification);
        scissorsTool.setToggleState(false, juce::dontSendNotification);
    };
    scissorsTool.onClick = [this]
    {
        if (trackModel != nullptr) trackModel->setActiveTool(TrackDataModel::EditTool::Split);
        pointerTool.setToggleState(false, juce::dontSendNotification);
        scissorsTool.setToggleState(true, juce::dontSendNotification);
    };
    pointerTool.setToggleState(true, juce::dontSendNotification);

    playButton.setColour(juce::TextButton::buttonColourId, accentCyan.withAlpha(0.16f));
    stopButton.setColour(juce::TextButton::buttonColourId, juce::Colours::white.withAlpha(0.08f));
    recordButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffdc4d4d).withAlpha(0.15f));
    inspectorButton.setColour(juce::TextButton::buttonColourId, darkPanel);
    mixerButton.setColour(juce::TextButton::buttonColourId, darkPanel);
    timecodeLabel.setText("00:00:00:00", juce::dontSendNotification);
    timecodeLabel.setJustificationType(juce::Justification::centred);
    timecodeLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff0a0a0a));
    timecodeLabel.setColour(juce::Label::textColourId, accentCyan);
    timecodeLabel.setColour(juce::Label::outlineColourId, juce::Colour(0xff333333));
    timecodeLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));

    bpmSlider.setRange(60.0, 220.0, 1.0);
    bpmSlider.setValue(120.0);
    bpmSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    bpmSlider.setColour(juce::Slider::trackColourId, accentBlue);
    bpmSlider.setColour(juce::Slider::thumbColourId, accentCyan);

    metronomeButton.setToggleState(true, juce::NotificationType::dontSendNotification);
    bpmLabel.setText("120 BPM", juce::dontSendNotification);
    bpmLabel.setJustificationType(juce::Justification::centredRight);
    bpmLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    bpmLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(bpmLabel);
}

void ControlBar::paint(juce::Graphics& g)
{
    g.fillAll(darkPanel);

    const auto area = getLocalBounds().toFloat();
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawLine(area.getX(), area.getBottom() - 1.0f, area.getRight(), area.getBottom() - 1.0f, 1.0f);

    auto topBar = area.reduced(0.0f, 0.0f);
    auto glow = juce::ColourGradient(accentBlue, 0.0f, 0.0f, accentCyan, topBar.getWidth(), 0.0f, false);
    g.setGradientFill(glow);
    g.fillRect(topBar.getX(), topBar.getY(), topBar.getWidth(), 2.0f);
}

void ControlBar::resized()
{
    const auto bounds = getLocalBounds().reduced(10, 10);
    const auto buttonWidth = 70;
    const auto buttonHeight = 40;

    inspectorButton.setBounds(bounds.getX(), bounds.getY(), 34, buttonHeight);
    mixerButton.setBounds(bounds.getX() + 40, bounds.getY(), 34, buttonHeight);

    const auto centre = bounds.getCentreX();
    const auto transportWidth = buttonWidth * 2 + 6;
    const auto transportX = centre - transportWidth / 2;
    timecodeLabel.setBounds(centre - 90, bounds.getY(), 180, 24);
    playButton.setBounds(transportX, bounds.getY() + 25, buttonWidth, buttonHeight - 5);
    stopButton.setBounds(transportX + buttonWidth + 6, bounds.getY() + 25, buttonWidth, buttonHeight - 5);

    recordButton.setBounds(bounds.getRight() - 310, bounds.getY(), 78, buttonHeight);
    metronomeButton.setBounds(bounds.getRight() - 225, bounds.getY(), 100, buttonHeight);
    bpmSlider.setBounds(bounds.getRight() - 115, bounds.getY() + 4, 75, 30);
    bpmLabel.setBounds(bounds.getRight() - 55, bounds.getY(), 55, 40);
    pointerTool.setBounds(bounds.getX() + 84, bounds.getY(), 82, 40);
    scissorsTool.setBounds(bounds.getX() + 170, bounds.getY(), 82, 40);
}
