#include "ControlBar.h"
#include "BinaryData.h"
#include "../AudioEngine/AudioEngine.h"
#include "../Models/TrackDataModel.h"
#include "Theme/StudioForgeLookAndFeel.h"

namespace
{
const auto darkPanel = juce::Colour(0xff353535);
const auto accentBlue = juce::Colour(0xff6f88a8);
const auto accentCyan = juce::Colour(0xffa7c5df);
constexpr float logoForegroundBrightness = 0.12f;
constexpr float logoAlphaScale = 3188.0f;
constexpr float idleLogoBrightnessBoost = 0.28f;
constexpr float illuminatedLogoBrightnessBoost = 0.72f;

juce::Rectangle<int> findVisibleLogoBounds(const juce::Image& source)
{
    juce::Rectangle<int> visibleBounds;
    for (int y = 0; y < source.getHeight(); ++y)
        for (int x = 0; x < source.getWidth(); ++x)
            if (source.getPixelAt(x, y).getBrightness() > logoForegroundBrightness)
                visibleBounds = visibleBounds.getUnion({ x, y, 1, 1 });

    return visibleBounds;
}

juce::Image createTransparentLogo(const juce::Image& source, float brightnessBoost)
{
    const auto visibleBounds = findVisibleLogoBounds(source);
    if (visibleBounds.isEmpty())
        return {};

    const auto croppedSource = source.getClippedImage(visibleBounds.expanded(4).getIntersection(source.getBounds()));
    juce::Image transparentLogo(juce::Image::ARGB, croppedSource.getWidth(), croppedSource.getHeight(), true);
    for (int y = 0; y < croppedSource.getHeight(); ++y)
        for (int x = 0; x < croppedSource.getWidth(); ++x)
        {
            const auto colour = croppedSource.getPixelAt(x, y);
            if (colour.getBrightness() <= logoForegroundBrightness)
                continue;

            const auto alpha = juce::jlimit(0, 255,
                juce::roundToInt((colour.getBrightness() - logoForegroundBrightness) * logoAlphaScale));
            transparentLogo.setPixelAt(x, y, colour.brighter(brightnessBoost)
                                                  .withAlpha(static_cast<uint8_t>(alpha)));
        }
    return transparentLogo;
}

juce::Image loadBrandLogoSource()
{
    return juce::ImageFileFormat::loadFrom(BinaryData::LiveDawLogo_png,
                                            BinaryData::LiveDawLogo_pngSize);
}
}

TransportIconButton::TransportIconButton(Icon requestedIcon)
    : juce::Button({}), icon(requestedIcon)
{
    setWantsKeyboardFocus(false);
}

void TransportIconButton::paintButton(juce::Graphics& graphics, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const auto active = getToggleState();
    auto background = isColourSpecified(juce::TextButton::buttonColourId)
        ? findColour(juce::TextButton::buttonColourId)
        : juce::Colour(0xff243139);

    if (icon == Icon::record && active)
        background = juce::Colour(0xffb64d56);
    else if (icon == Icon::play && active)
        background = juce::Colour(0xff258c9d);
    else if (active)
        background = juce::Colour(0xff366d7d);
    else if (down)
        background = background.brighter(0.16f);
    else if (highlighted)
        background = background.brighter(0.08f);

    graphics.setColour(background);
    graphics.fillRoundedRectangle(bounds, 5.0f);
    graphics.setColour(juce::Colours::white.withAlpha(active ? 0.16f : 0.06f));
    graphics.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    graphics.setColour(juce::Colours::white.withAlpha(highlighted || active ? 0.96f : 0.78f));

    const auto iconBounds = bounds.reduced(7.0f);
    const auto centreX = iconBounds.getCentreX();
    const auto centreY = iconBounds.getCentreY();
    juce::Path path;

    const auto drawTriangle = [&graphics](float left, float top, float width, float height, bool pointsRight)
    {
        juce::Path triangle;
        if (pointsRight)
        {
            triangle.startNewSubPath(left, top);
            triangle.lineTo(left + width, top + height * 0.5f);
            triangle.lineTo(left, top + height);
        }
        else
        {
            triangle.startNewSubPath(left + width, top);
            triangle.lineTo(left, top + height * 0.5f);
            triangle.lineTo(left + width, top + height);
        }
        triangle.closeSubPath();
        graphics.fillPath(triangle);
    };

    switch (icon)
    {
        case Icon::goToBeginning:
            graphics.fillRect(iconBounds.getX(), iconBounds.getY(), 1.8f, iconBounds.getHeight());
            drawTriangle(iconBounds.getX() + 3.5f, iconBounds.getY() + 1.0f,
                         iconBounds.getWidth() - 4.5f, iconBounds.getHeight() - 2.0f, false);
            break;

        case Icon::rewind:
            drawTriangle(iconBounds.getX(), iconBounds.getY() + 1.0f,
                         iconBounds.getWidth() * 0.58f, iconBounds.getHeight() - 2.0f, false);
            drawTriangle(iconBounds.getX() + iconBounds.getWidth() * 0.38f, iconBounds.getY() + 1.0f,
                         iconBounds.getWidth() * 0.58f, iconBounds.getHeight() - 2.0f, false);
            break;

        case Icon::play:
            drawTriangle(iconBounds.getX() + 2.5f, iconBounds.getY() + 1.0f,
                         iconBounds.getWidth() - 4.5f, iconBounds.getHeight() - 2.0f, true);
            break;

        case Icon::stop:
            graphics.fillRoundedRectangle(centreX - 4.5f, centreY - 4.5f, 9.0f, 9.0f, 1.2f);
            break;

        case Icon::forward:
            drawTriangle(iconBounds.getX() + iconBounds.getWidth() * 0.04f, iconBounds.getY() + 1.0f,
                         iconBounds.getWidth() * 0.58f, iconBounds.getHeight() - 2.0f, true);
            drawTriangle(iconBounds.getX() + iconBounds.getWidth() * 0.42f, iconBounds.getY() + 1.0f,
                         iconBounds.getWidth() * 0.58f, iconBounds.getHeight() - 2.0f, true);
            break;

        case Icon::record:
            graphics.fillEllipse(centreX - 5.0f, centreY - 5.0f, 10.0f, 10.0f);
            break;
    }
}

ControlBar::ControlBar(TrackDataModel* model, AudioEngine* engine)
    : transportLCD(model), trackModel(model), audioEngine(engine)
{
    const auto logoSource = loadBrandLogoSource();
    brandLogo = createTransparentLogo(logoSource, idleLogoBrightnessBoost);
    illuminatedBrandLogo = createTransparentLogo(logoSource, illuminatedLogoBrightnessBoost);

    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(recordButton);
    addAndMakeVisible(countInButton);
    addAndMakeVisible(punchButton);
    addAndMakeVisible(cycleButton);
    addAndMakeVisible(metronomeButton);
    addAndMakeVisible(bpmSlider);
    addAndMakeVisible(inspectorButton);
    addAndMakeVisible(mixerButton);
    addAndMakeVisible(pianoRollButton);
    addAndMakeVisible(browserButton);
    addAndMakeVisible(addTrackButton);
    addAndMakeVisible(goToBeginningButton);
    addAndMakeVisible(rewindButton);
    addAndMakeVisible(forwardButton);
    addAndMakeVisible(transportLCD);
    addAndMakeVisible(masterMeter);
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
    goToBeginningButton.setTooltip("Go to beginning (Return)");
    rewindButton.setTooltip("Move playhead back one bar");
    forwardButton.setTooltip("Move playhead forward one bar");
    goToBeginningButton.onClick = [this] { goToBeginning(); };
    rewindButton.onClick = [this] { rewindOneBar(); };
    forwardButton.onClick = [this] { forwardOneBar(); };
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
    countInButton.setClickingTogglesState(true);
    countInButton.setTooltip("Record after one bar of count-in");
    countInButton.onClick = [this]
    {
        if (onCountInChanged != nullptr)
            onCountInChanged(countInButton.getToggleState());
    };
    punchButton.setClickingTogglesState(true);
    punchButton.setTooltip("Enable punch recording. Uses the cycle range, or one bar at the playhead.");
    punchButton.onClick = [this]
    {
        if (onPunchChanged != nullptr)
            onPunchChanged(punchButton.getToggleState());
    };
    cycleButton.setClickingTogglesState(true);
    cycleButton.setTooltip("Repeat the active cycle range (C)");
    cycleButton.onClick = [this] { toggleCycle(); };
    metronomeButton.setClickingTogglesState(true);
    metronomeButton.setTooltip("Enable metronome click (K)");
    metronomeButton.onClick = [this]
    {
        if (audioEngine != nullptr)
            audioEngine->setMetronomeEnabled(metronomeButton.getToggleState());
    };

    for (auto* button : { &goToBeginningButton, &rewindButton, &playButton, &stopButton, &forwardButton, &recordButton })
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff243139));
    punchButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffde9b42).withAlpha(0.18f));
    inspectorButton.setColour(juce::TextButton::buttonColourId, darkPanel);
    mixerButton.setColour(juce::TextButton::buttonColourId, darkPanel);
    pianoRollButton.setColour(juce::TextButton::buttonColourId, darkPanel);
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

    metronomeButton.setToggleState(audioEngine != nullptr && audioEngine->isMetronomeEnabled(), juce::dontSendNotification);
    bpmLabel.setText("120 BPM", juce::dontSendNotification);
    bpmLabel.setJustificationType(juce::Justification::centredRight);
    bpmLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    bpmLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(bpmLabel);

    transportTimeLabel.setJustificationType(juce::Justification::centred);
    transportTimeLabel.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold)));
    transportTimeLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff10171b));
    transportTimeLabel.setColour(juce::Label::textColourId, StudioForgeTheme::lcdCyan.withAlpha(0.92f));
    transportTimeLabel.setColour(juce::Label::outlineColourId, juce::Colour(0xff26343a));
    transportTimeLabel.setOpaque(true);
    addAndMakeVisible(transportTimeLabel);
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

void ControlBar::goToBeginning() noexcept
{
    if (audioEngine != nullptr)
        audioEngine->setPlaybackState(false);
    if (trackModel != nullptr)
        trackModel->setPlayheadPosition(0.0);
}

void ControlBar::rewindOneBar() noexcept
{
    if (trackModel == nullptr)
        return;
    const auto samplesPerBar = trackModel->getSampleRate() * 60.0
        / juce::jmax(1.0, trackModel->getTempoAtSample(trackModel->getPlayheadPosition()))
        * trackModel->getTimeSignatureNumerator();
    trackModel->setPlayheadPosition(trackModel->getPlayheadPosition() - samplesPerBar);
}

void ControlBar::forwardOneBar() noexcept
{
    if (trackModel == nullptr)
        return;
    const auto samplesPerBar = trackModel->getSampleRate() * 60.0
        / juce::jmax(1.0, trackModel->getTempoAtSample(trackModel->getPlayheadPosition()))
        * trackModel->getTimeSignatureNumerator();
    trackModel->setPlayheadPosition(trackModel->getPlayheadPosition() + samplesPerBar);
}

void ControlBar::toggleCycle() noexcept
{
    if (trackModel == nullptr)
        return;
    if (trackModel->isCycleActive())
    {
        trackModel->clearCycle();
        return;
    }
    const auto start = trackModel->getPlayheadPosition();
    const auto length = trackModel->getSampleRate() * 60.0 / juce::jmax(1.0, trackModel->getBpm())
        * trackModel->getTimeSignatureNumerator();
    trackModel->setCycle(start, start + length);
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

    recordButton.setToggleState(audioEngine->isRecording() || audioEngine->isMidiRecording(), juce::dontSendNotification);
}

void ControlBar::setRecordActive(bool active) noexcept
{
    recordButton.setToggleState(active, juce::dontSendNotification);
}

void ControlBar::setRecordCountdown(bool active) noexcept
{
    recordButton.setTooltip(active ? "Recording starts after count-in" : "Record");
    recordButton.setToggleState(active, juce::dontSendNotification);
}

void ControlBar::timerCallback()
{
    const auto isPerforming = audioEngine != nullptr
        && (audioEngine->isPlaying() || audioEngine->isRecording() || audioEngine->isMidiRecording());
    updateLogoPlaybackGlow(isPerforming);

    if (audioEngine != nullptr)
        masterMeter.updateStereoPeak(audioEngine->getMasterLeftPeak(), audioEngine->getMasterRightPeak());
    playButton.setToggleState(audioEngine != nullptr && audioEngine->isPlaying(), juce::dontSendNotification);

    if (trackModel == nullptr)
        return;

    const auto sampleRate = trackModel->getSampleRate();
    const auto totalCentiseconds = sampleRate > 0.0
        ? static_cast<long long>(trackModel->getPlayheadPosition() * 100.0 / sampleRate + 0.5)
        : 0LL;
    const auto totalSeconds = totalCentiseconds / 100;
    transportTimeLabel.setText(juce::String::formatted("%02d:%02d:%02d.%02d",
                                  static_cast<int>(totalSeconds / 3600),
                                  static_cast<int>((totalSeconds / 60) % 60),
                                  static_cast<int>(totalSeconds % 60),
                                  static_cast<int>(totalCentiseconds % 100)),
                               juce::dontSendNotification);
    punchButton.setToggleState(trackModel->isPunchActive(), juce::dontSendNotification);
    cycleButton.setToggleState(trackModel->isCycleActive(), juce::dontSendNotification);
    metronomeButton.setToggleState(audioEngine != nullptr && audioEngine->isMetronomeEnabled(), juce::dontSendNotification);
}

void ControlBar::updateLogoPlaybackGlow(bool shouldGlow) noexcept
{
    constexpr float glowStep = 0.10f;
    const auto targetGlow = shouldGlow ? 1.0f : 0.0f;
    const auto nextGlow = logoPlaybackGlow + (targetGlow - logoPlaybackGlow) * glowStep;
    if (std::abs(nextGlow - logoPlaybackGlow) < 0.002f)
        return;

    logoPlaybackGlow = nextGlow;
    repaint(20, 5, 204, 54);
}

void ControlBar::paint(juce::Graphics& g)
{
    g.fillAll(StudioForgeTheme::panelBackground);

    const auto area = getLocalBounds().toFloat();
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawLine(area.getX(), area.getBottom() - 1.0f, area.getRight(), area.getBottom() - 1.0f, 1.0f);

    if (brandLogo.isValid())
    {
        constexpr float idleLogoOpacity = 0.32f;
        constexpr float activeLogoOpacity = 0.92f;
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        const auto logoBounds = juce::Rectangle<float>(20.0f, 5.0f, 204.0f, 54.0f);
        juce::Graphics::ScopedSaveState logoState(g);
        g.setOpacity(idleLogoOpacity);
        g.drawImageWithin(brandLogo, logoBounds.getX(), logoBounds.getY(), logoBounds.getWidth(), logoBounds.getHeight(),
                          juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        if (logoPlaybackGlow > 0.001f)
        {
            g.setOpacity(activeLogoOpacity * logoPlaybackGlow);
            g.drawImageWithin(illuminatedBrandLogo, logoBounds.getX(), logoBounds.getY(), logoBounds.getWidth(), logoBounds.getHeight(),
                              juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        }
    }

    auto transportBounds = goToBeginningButton.getBounds();
    for (auto* button : { &rewindButton, &playButton, &stopButton, &forwardButton, &recordButton })
        transportBounds = transportBounds.getUnion(button->getBounds());
    // The transport shell deliberately encloses only the button row. Keeping
    // the time readout outside it prevents two outlines from visually merging
    // with the ControlBar divider on compact-height layouts.
    g.setColour(juce::Colour(0xff111b21));
    g.fillRoundedRectangle(transportBounds.expanded(5, 4).toFloat(), 7.0f);
    g.setColour(juce::Colour(0xff26343a));
    g.drawRoundedRectangle(transportBounds.expanded(5, 4).toFloat(), 7.0f, 1.0f);
}

void ControlBar::resized()
{
    const auto bounds = getLocalBounds().reduced(10, 6);
    const auto buttonWidth = 30;
    const auto buttonHeight = 32;

    inspectorButton.setVisible(false);
    mixerButton.setVisible(false);
    pianoRollButton.setVisible(false);
    browserButton.setVisible(false);
    addTrackButton.setVisible(false);

    const auto rightControlsWidth = 0;
    const auto rightX = bounds.getRight() - rightControlsWidth;
    cycleButton.setVisible(false);
    countInButton.setVisible(false);
    punchButton.setVisible(false);
    metronomeButton.setVisible(false);
    bpmSlider.setVisible(false);
    bpmLabel.setVisible(false);

    const auto wideLayout = getWidth() >= 1450;
    const auto lcdWidth = wideLayout ? 276 : 220;
    const auto meterWidth = wideLayout ? 168 : 0;
    const auto clusterWidth = lcdWidth + meterWidth + 6 * 30 + 30;
    const auto clusterMinX = bounds.getX() + 402;
    const auto clusterMaxX = rightX - clusterWidth - 8;
    const auto clusterX = clusterMaxX > clusterMinX
        ? juce::jlimit(clusterMinX, clusterMaxX, bounds.getCentreX() - clusterWidth / 2)
        : clusterMaxX;
    transportLCD.setBounds(clusterX, bounds.getY(), lcdWidth, 52);
    const auto transportX = clusterX + lcdWidth + 18;
    goToBeginningButton.setBounds(transportX, bounds.getY() + 7, 28, 28);
    rewindButton.setBounds(transportX + 32, bounds.getY() + 7, 28, 28);
    playButton.setBounds(transportX + 64, bounds.getY() + 6, 30, 30);
    stopButton.setBounds(transportX + 98, bounds.getY() + 7, 28, 28);
    forwardButton.setBounds(transportX + 130, bounds.getY() + 7, 28, 28);
    recordButton.setBounds(transportX + 162, bounds.getY() + 7, 28, 28);
    // Keep the compact time readout visually separate from the control bar's
    // bottom edge; its enclosing transport frame still has a calm dark rim.
    transportTimeLabel.setBounds(transportX + 3, bounds.getY() + 40, 184, 12);
    masterMeter.setBounds(transportX + 200, bounds.getY() + 5, meterWidth, 34);
    masterMeter.setVisible(wideLayout);
    pointerTool.setBounds(bounds.getX() + 174, bounds.getY(), 68, buttonHeight);
    scissorsTool.setBounds(bounds.getX() + 246, bounds.getY(), 78, buttonHeight);
    eraserTool.setBounds(bounds.getX() + 328, bounds.getY(), 64, buttonHeight);

    // The transport has priority on compact windows; tools remain accessible
    // through their shortcuts rather than overlapping its LCD and buttons.
    pointerTool.setVisible(false);
    scissorsTool.setVisible(false);
    eraserTool.setVisible(false);
}
