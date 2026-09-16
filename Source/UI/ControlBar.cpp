#include "ControlBar.h"
#include "../AudioEngine/AudioEngine.h"
#include "../Models/TrackDataModel.h"

namespace
{
const auto darkPanel = juce::Colour(0xff353535);
const auto accentBlue = juce::Colour(0xff6f88a8);
const auto accentCyan = juce::Colour(0xffa7c5df);
}

ControlBar::ControlBar(TrackDataModel* model, AudioEngine* engine)
    : trackModel(model), audioEngine(engine)
{
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(recordButton);
    addAndMakeVisible(metronomeButton);
    addAndMakeVisible(bpmSlider);
    addAndMakeVisible(inspectorButton);
    addAndMakeVisible(mixerButton);
    addAndMakeVisible(pianoRollButton);
    addAndMakeVisible(browserButton);
    addAndMakeVisible(addTrackButton);
    addAndMakeVisible(timecodeLabel);
    addAndMakeVisible(pointerTool);
    addAndMakeVisible(scissorsTool);
    addAndMakeVisible(eraserTool);
    inspectorButton.setClickingTogglesState(true);
    mixerButton.setClickingTogglesState(true);
    pianoRollButton.setClickingTogglesState(true);
    browserButton.setClickingTogglesState(true);
    inspectorButton.setTooltip("Show or hide Inspector");
    mixerButton.setTooltip("Show or hide Mixer");
    pianoRollButton.setTooltip("Show or hide Piano Roll");
    browserButton.setTooltip("Show or hide Media Browser");
    addTrackButton.setTooltip("Create new track");
    inspectorButton.onClick = [this] { if (onInspectorToggle != nullptr) onInspectorToggle(); };
    mixerButton.onClick = [this] { if (onMixerToggle != nullptr) onMixerToggle(); };
    pianoRollButton.onClick = [this] { if (onPianoRollToggle != nullptr) onPianoRollToggle(); };
    browserButton.onClick = [this] { if (onBrowserToggle != nullptr) onBrowserToggle(); };
    addTrackButton.onClick = [this]
    {
        // The modal owns type/count choices so the in-project command uses the
        // same validated creation flow as a new empty project.
        if (onCreateTrack != nullptr)
            onCreateTrack(TrackType::audio);
    };
    pointerTool.setClickingTogglesState(true);
    scissorsTool.setClickingTogglesState(true);
    eraserTool.setClickingTogglesState(true);
    pointerTool.onClick = [this]
    {
        if (trackModel != nullptr) trackModel->setActiveTool(TrackDataModel::EditTool::Pointer);
        pointerTool.setToggleState(true, juce::dontSendNotification);
        scissorsTool.setToggleState(false, juce::dontSendNotification);
        eraserTool.setToggleState(false, juce::dontSendNotification);
    };
    scissorsTool.onClick = [this]
    {
        if (trackModel != nullptr) trackModel->setActiveTool(TrackDataModel::EditTool::Scissors);
        pointerTool.setToggleState(false, juce::dontSendNotification);
        scissorsTool.setToggleState(true, juce::dontSendNotification);
        eraserTool.setToggleState(false, juce::dontSendNotification);
    };
    eraserTool.onClick = [this]
    {
        if (trackModel != nullptr) trackModel->setActiveTool(TrackDataModel::EditTool::Eraser);
        pointerTool.setToggleState(false, juce::dontSendNotification);
        scissorsTool.setToggleState(false, juce::dontSendNotification);
        eraserTool.setToggleState(true, juce::dontSendNotification);
    };
    pointerTool.setToggleState(true, juce::dontSendNotification);
    playButton.onClick = [this]
    {
        if (audioEngine != nullptr)
            audioEngine->setPlaybackState(true);
    };
    stopButton.onClick = [this]
    {
        stopPlayback();
    };
    recordButton.onClick = [this] { toggleRecording(); };

    playButton.setColour(juce::TextButton::buttonColourId, accentCyan.withAlpha(0.16f));
    stopButton.setColour(juce::TextButton::buttonColourId, juce::Colours::white.withAlpha(0.08f));
    recordButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffdc4d4d).withAlpha(0.15f));
    inspectorButton.setColour(juce::TextButton::buttonColourId, darkPanel);
    mixerButton.setColour(juce::TextButton::buttonColourId, darkPanel);
    pianoRollButton.setColour(juce::TextButton::buttonColourId, darkPanel);
    timecodeLabel.setText("00:00:00:00", juce::dontSendNotification);
    timecodeLabel.setJustificationType(juce::Justification::centred);
    timecodeLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff0a0a0a));
    timecodeLabel.setColour(juce::Label::textColourId, accentCyan);
    timecodeLabel.setColour(juce::Label::outlineColourId, juce::Colour(0xff333333));
    timecodeLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));

    bpmSlider.setRange(60.0, 220.0, 1.0);
    bpmSlider.setValue(trackModel != nullptr ? trackModel->getBpm() : 120.0);
    bpmSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    bpmSlider.setColour(juce::Slider::trackColourId, accentBlue);
    bpmSlider.setColour(juce::Slider::thumbColourId, accentCyan);
    bpmSlider.onValueChange = [this]
    {
        if (audioEngine != nullptr)
            audioEngine->setTempo(bpmSlider.getValue());
        if (trackModel != nullptr)
            bpmLabel.setText(juce::String(juce::roundToInt(trackModel->getBpm())) + " BPM",
                             juce::dontSendNotification);
    };

    metronomeButton.setToggleState(true, juce::NotificationType::dontSendNotification);
    bpmLabel.setText("120 BPM", juce::dontSendNotification);
    bpmLabel.setJustificationType(juce::Justification::centredRight);
    bpmLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    bpmLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(bpmLabel);
    startTimerHz(30);
}

void ControlBar::setInspectorVisible(bool visible) noexcept
{
    inspectorButton.setToggleState(visible, juce::dontSendNotification);
}

void ControlBar::setMixerVisible(bool visible) noexcept
{
    mixerButton.setToggleState(visible, juce::dontSendNotification);
}

void ControlBar::setPianoRollVisible(bool visible) noexcept
{
    pianoRollButton.setToggleState(visible, juce::dontSendNotification);
}

void ControlBar::setBrowserVisible(bool visible) noexcept
{
    browserButton.setToggleState(visible, juce::dontSendNotification);
}

void ControlBar::togglePlayback() noexcept
{
    if (audioEngine != nullptr)
        audioEngine->setPlaybackState(! audioEngine->isPlaying());
}

void ControlBar::stopPlayback() noexcept
{
    if (audioEngine != nullptr)
        audioEngine->setPlaybackState(false);
}

void ControlBar::toggleRecording()
{
    if (onRecordRequested != nullptr)
    {
        onRecordRequested();
        return;
    }

    if (audioEngine == nullptr)
        return;

    if (audioEngine->isRecording())
    {
        audioEngine->stopRecording();
    }
    else
    {
        if (trackModel == nullptr || trackModel->getTrackCount() == 0)
            return;
        const auto armedTrack = trackModel != nullptr ? trackModel->getFirstArmedTrackIndex() : -1;
        const auto targetTrack = static_cast<size_t>(juce::jmax(0, armedTrack));
        audioEngine->startRecording(targetTrack, juce::File::getSpecialLocation(juce::File::tempDirectory)
                                                  .getChildFile("StudioForgeRecording.wav"));
    }

    recordButton.setToggleState(audioEngine->isRecording(), juce::dontSendNotification);
}

void ControlBar::setRecordActive(bool active) noexcept
{
    recordButton.setToggleState(active, juce::dontSendNotification);
}

void ControlBar::timerCallback()
{
    if (trackModel == nullptr)
        return;

    const auto seconds = static_cast<int>(trackModel->getPlayheadPosition() / trackModel->getSampleRate());
    const auto hours = seconds / 3600;
    const auto minutes = (seconds / 60) % 60;
    const auto remainingSeconds = seconds % 60;
    timecodeLabel.setText(juce::String::formatted("%02d:%02d:%02d", hours, minutes, remainingSeconds),
                          juce::dontSendNotification);
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
    const auto bounds = getLocalBounds().reduced(8, 7);
    const auto buttonWidth = 58;
    const auto buttonHeight = 32;

    inspectorButton.setBounds(bounds.getX(), bounds.getY(), 30, buttonHeight);
    mixerButton.setBounds(bounds.getX() + 34, bounds.getY(), 30, buttonHeight);
    pianoRollButton.setBounds(bounds.getX() + 68, bounds.getY(), 30, buttonHeight);
    browserButton.setBounds(bounds.getX() + 102, bounds.getY(), 30, buttonHeight);
    addTrackButton.setBounds(bounds.getX() + 136, bounds.getY(), 30, buttonHeight);

    const auto centre = bounds.getCentreX();
    const auto transportWidth = buttonWidth * 2 + 6;
    const auto transportX = centre - transportWidth / 2;
    timecodeLabel.setBounds(centre - 95, bounds.getY(), 190, 18);
    playButton.setBounds(transportX, bounds.getY() + 19, buttonWidth, 26);
    stopButton.setBounds(transportX + buttonWidth + 5, bounds.getY() + 19, buttonWidth, 26);

    recordButton.setBounds(bounds.getRight() - 270, bounds.getY(), 66, buttonHeight);
    metronomeButton.setBounds(bounds.getRight() - 198, bounds.getY(), 86, buttonHeight);
    bpmSlider.setBounds(bounds.getRight() - 108, bounds.getY() + 2, 62, 25);
    bpmLabel.setBounds(bounds.getRight() - 46, bounds.getY(), 46, buttonHeight);
    pointerTool.setBounds(bounds.getX() + 174, bounds.getY(), 68, buttonHeight);
    scissorsTool.setBounds(bounds.getX() + 246, bounds.getY(), 78, buttonHeight);
    eraserTool.setBounds(bounds.getX() + 328, bounds.getY(), 64, buttonHeight);

    // The transport has priority on compact windows; tools remain accessible
    // through their shortcuts rather than overlapping its LCD and buttons.
    const auto showToolStrip = getWidth() >= 1040;
    pointerTool.setVisible(showToolStrip);
    scissorsTool.setVisible(showToolStrip);
    eraserTool.setVisible(showToolStrip);
}
