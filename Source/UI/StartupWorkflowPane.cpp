#include "StartupWorkflowPane.h"
#include "PerformanceRoomIconLibrary.h"

namespace
{
const auto overlay = juce::Colour(0x99000000);
const auto dialog = juce::Colour(0xfff7f8fa);
const auto panel = juce::Colour(0xffedf0f3);
const auto stagePanel = juce::Colour(0xffe8eff8);
const auto text = juce::Colour(0xff2f3942);
const auto muted = juce::Colour(0xff68747e);
const auto blue = juce::Colour(0xff2f6398);

juce::String roleName(PerformanceRole role)
{
    static constexpr std::array<const char*, PerformanceRoomModel::memberCount> names
        { "Vocal", "Guitar", "Bass", "Keyboard", "Drums", "Beat" };
    return names[static_cast<size_t>(role)];
}

void drawChevron(juce::Graphics& g, float x, float y)
{
    g.drawLine(x, y, x + 5.0f, y + 5.0f, 1.2f);
    g.drawLine(x + 5.0f, y + 5.0f, x + 10.0f, y, 1.2f);
}
}

StartupWorkflowPane::StartupWorkflowPane()
{
    for (juce::Component* component : { static_cast<juce::Component*>(&title), static_cast<juce::Component*>(&newProject), static_cast<juce::Component*>(&openProject), static_cast<juce::Component*>(&enterPerformance), static_cast<juce::Component*>(&cancel), static_cast<juce::Component*>(&progress), static_cast<juce::Component*>(&audioInputSelector), static_cast<juce::Component*>(&audioOutputSelector), static_cast<juce::Component*>(&configureAudioDevice) })
        addAndMakeVisible(component);

    title.setJustificationType(juce::Justification::centred);
    title.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, juce::Colours::white);
    progress.setJustificationType(juce::Justification::centred);
    progress.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.8f));
    newProject.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    newProject.setColour(juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
    openProject.setColour(juce::TextButton::buttonColourId, juce::Colours::white);
    openProject.setColour(juce::TextButton::textColourOffId, text);
    enterPerformance.setColour(juce::TextButton::buttonColourId, blue);
    enterPerformance.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    cancel.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff303b44));
    cancel.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    for (size_t i = 0; i < recentProjectButtons.size(); ++i)
    {
        auto& button = recentProjectButtons[i];
        addAndMakeVisible(button);
        button.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        button.setColour(juce::TextButton::textColourOffId, muted);
        button.onClick = [this, i] { if (i < static_cast<size_t>(recentProjects.size()) && onOpenRecentProject) onOpenRecentProject(juce::File(recentProjects[static_cast<int>(i)])); };
    }
    for (size_t i = 0; i < performancePresets.size(); ++i)
    {
        addAndMakeVisible(performancePresets[i]);
        performancePresets[i].onClick = [this, i] { showPerformancePreset(static_cast<PerformancePreset>(i)); };
    }
    for (size_t i = 0; i < PerformanceRoomModel::memberCount; ++i)
    {
        auto& label = setupLabels[i];
        label.setEditable(false, true, false);
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, text);
        label.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        label.onTextChange = [this, i]
        {
            auto value = setupLabels[i].getText().trim();
            performanceRoom.getMembers()[i].displayName = value.isEmpty() ? roleName(performanceRoom.getMembers()[i].role) : value;
            repaint();
        };
        auto& quantity = setupQuantities[i];
        quantity.setJustificationType(juce::Justification::centred);
        quantity.setColour(juce::Label::backgroundColourId, juce::Colours::white);
        quantity.setColour(juce::Label::textColourId, text);
        quantity.setColour(juce::Label::outlineColourId, juce::Colour(0xffcfd7df));
        for (auto* button : { &setupMinus[i], &setupPlus[i] })
        {
            addAndMakeVisible(button);
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff35434e));
            button->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        }
        setupMinus[i].setButtonText("-");
        setupPlus[i].setButtonText("+");
        setupMinus[i].onClick = [this, i] { auto& member = performanceRoom.getMembers()[i]; member.quantity = juce::jmax(0, member.quantity - 1); synchronisePerformanceControls(); };
        setupPlus[i].onClick = [this, i]
        {
            if (performanceRoom.getTotalQuantity() >= 14)
                return;
            auto& member = performanceRoom.getMembers()[i];
            member.quantity = juce::jmin(8, member.quantity + 1);
            synchronisePerformanceControls();
        };
        addAndMakeVisible(label);
        addAndMakeVisible(quantity);
    }
    newProject.onClick = [this] { if (onTemplateSelected) onTemplateSelected(ProjectTemplate::empty); };
    openProject.onClick = [this] { if (onOpenProject) onOpenProject(); };
    configureAudioDevice.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffdbe2e9));
    configureAudioDevice.setColour(juce::TextButton::textColourOffId, text);
    configureAudioDevice.onClick = [this] { if (onAudioDeviceSettingsRequested) onAudioDeviceSettingsRequested(); };
    audioInputSelector.onChange = [this]
    {
        if (onAudioInputChannelSelected && audioInputSelector.getSelectedId() > 0)
            onAudioInputChannelSelected(audioInputSelector.getSelectedId());
    };
    audioOutputSelector.onChange = [this]
    {
        if (onAudioOutputChannelSelected && audioOutputSelector.getSelectedId() > 0)
            onAudioOutputChannelSelected(audioOutputSelector.getSelectedId());
    };
    enterPerformance.onClick = [this] { if (onPerformanceConfigured) onPerformanceConfigured(performanceRoom.createLiveSetup()); };
    cancel.onClick = [this]
    {
        if (returnToWorkspaceOnCancel) { setVisible(false); return; }
        step = Step::chooseProject;
        title.setText("Choose a Project", juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, text);
        resized(); repaint();
    };
    showPerformancePreset(PerformancePreset::vocalAndBand);
    step = Step::splash;
    title.setText("StudioForge DAW", juce::dontSendNotification);
    setAlpha(0.0f);
    startTimerHz(60);
}

void StartupWorkflowPane::timerCallback()
{
    if (step == Step::splash && --splashFramesRemaining <= 0)
    {
        step = Step::chooseProject;
        title.setText("Choose a Project", juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, text);
        currentAlpha = 0.76f;
        resized(); repaint();
    }
    currentAlpha = juce::jmin(1.0f, currentAlpha + 0.08f);
    setAlpha(currentAlpha);
    if (currentAlpha >= 1.0f && step != Step::splash) stopTimer();
}

void StartupWorkflowPane::showTrackCreation(bool returnToWorkspace)
{
    returnToWorkspaceOnCancel = returnToWorkspace;
    step = Step::performanceRoom;
    title.setText("Performance Room", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, text);
    resized(); repaint(); beginFadeIn();
}

void StartupWorkflowPane::setRecentProjects(juce::StringArray paths)
{
    recentProjects = std::move(paths);
    for (size_t i = 0; i < recentProjectButtons.size(); ++i)
        recentProjectButtons[i].setButtonText(i < static_cast<size_t>(recentProjects.size()) ? juce::File(recentProjects[static_cast<int>(i)]).getFileNameWithoutExtension() : juce::String {});
    resized(); repaint();
}

void StartupWorkflowPane::setAudioDeviceChannels(int inputs, int outputs)
{
    audioInputChannels = juce::jmax(0, inputs);
    audioOutputChannels = juce::jmax(0, outputs);
    audioInputSelector.clear(juce::dontSendNotification);
    audioOutputSelector.clear(juce::dontSendNotification);
    for (int channel = 1; channel <= audioInputChannels; ++channel)
        audioInputSelector.addItem("Input " + juce::String(channel), channel);
    for (int channel = 1; channel <= audioOutputChannels; channel += 2)
    {
        const auto label = channel < audioOutputChannels
            ? "Output " + juce::String(channel) + " + " + juce::String(channel + 1)
            : "Output " + juce::String(channel);
        audioOutputSelector.addItem(label, channel);
    }
    if (audioInputChannels == 0)
        audioInputSelector.addItem("No input available", 1);
    if (audioOutputChannels == 0)
        audioOutputSelector.addItem("No output available", 1);
    audioInputSelector.setSelectedId(1, juce::dontSendNotification);
    audioOutputSelector.setSelectedId(1, juce::dontSendNotification);
    repaint();
}

void StartupWorkflowPane::showPerformancePreset(PerformancePreset preset)
{
    performanceRoom.reset(preset);
    for (size_t i = 0; i < performancePresets.size(); ++i)
    {
        const auto selected = i == static_cast<size_t>(preset);
        performancePresets[i].setColour(juce::TextButton::buttonColourId, selected ? blue : juce::Colour(0xffdde3e8));
        performancePresets[i].setColour(juce::TextButton::textColourOffId, selected ? juce::Colours::white : text);
    }
    synchronisePerformanceControls();
}

void StartupWorkflowPane::synchronisePerformanceControls()
{
    const auto& members = performanceRoom.getMembers();
    for (size_t i = 0; i < members.size(); ++i)
    {
        setupLabels[i].setText(members[i].displayName, juce::dontSendNotification);
        setupLabels[i].setColour(juce::Label::textColourId, text);
        setupQuantities[i].setText(juce::String(members[i].quantity), juce::dontSendNotification);
    }
    repaint();
}

void StartupWorkflowPane::beginFadeIn()
{
    currentAlpha = 0.76f; setAlpha(currentAlpha); startTimerHz(60);
}

juce::Rectangle<int> StartupWorkflowPane::getDialogBounds() const
{
    const auto chooser = step == Step::chooseProject;
    const auto splash = step == Step::splash;
    const auto width = chooser ? juce::jlimit(640, 780, getWidth() - 96) : splash ? juce::jlimit(420, 560, getWidth() - 120) : juce::jlimit(760, 900, getWidth() - 70);
    const auto height = chooser ? juce::jlimit(420, 480, getHeight() - 90) : splash ? juce::jlimit(180, 260, getHeight() - 100) : juce::jlimit(430, 500, getHeight() - 70);
    return getLocalBounds().withSizeKeepingCentre(width, height);
}

void StartupWorkflowPane::paint(juce::Graphics& g)
{
    g.fillAll(overlay);
    const auto bounds = getDialogBounds().toFloat();
    if (step == Step::splash)
    {
        g.setColour(juce::Colour(0xff202a31)); g.fillRoundedRectangle(bounds, 12.0f);
        g.setColour(juce::Colour(0xff4b9dea)); g.fillRoundedRectangle(bounds.withHeight(4.0f), 2.0f); return;
    }
    g.setColour(juce::Colours::black.withAlpha(0.20f)); g.fillRoundedRectangle(bounds.translated(0.0f, 5.0f).expanded(5.0f), 10.0f);
    g.setColour(dialog); g.fillRoundedRectangle(bounds, 10.0f);
    if (step == Step::chooseProject)
    {
        constexpr auto footerHeight = 52.0f;
        const auto sidebar = juce::Rectangle<float>(bounds.getX(), bounds.getY() + 38.0f, 168.0f, bounds.getHeight() - 38.0f - footerHeight);
        g.setColour(juce::Colour(0xffe5e8eb));
        g.fillRect(sidebar);
        g.setColour(juce::Colour(0xffd0d6dc));
        g.fillRoundedRectangle(sidebar.getX() + 10.0f, sidebar.getY() + 14.0f, sidebar.getWidth() - 20.0f, 23.0f, 4.0f);
        g.setColour(text); g.setFont(12.0f);
        g.drawText("New Project", sidebar.getX() + 20.0f, sidebar.getY() + 14.0f, sidebar.getWidth() - 40.0f, 23.0f, juce::Justification::centredLeft, false);
        if (! recentProjects.isEmpty())
        {
            g.setColour(muted); g.setFont(10.0f);
            g.drawText("RECENT", sidebar.getX() + 18.0f, sidebar.getY() + 66.0f, sidebar.getWidth() - 36.0f, 16.0f, juce::Justification::centredLeft, false);
        }
        const auto tile = juce::Rectangle<float>(sidebar.getRight() + 46.0f, bounds.getY() + 64.0f, 180.0f, 120.0f);
        g.setColour(blue); g.fillRoundedRectangle(tile, 8.0f);
        g.setColour(juce::Colours::white.withAlpha(0.94f));
        for (int row = 0; row < 3; ++row)
            g.fillRoundedRectangle(tile.getX() + 20.0f, tile.getY() + 18.0f + row * 18.0f, row == 0 ? 48.0f : 78.0f, 10.0f, 2.0f);
        g.setColour(blue); g.setFont(12.0f);
        g.drawText("New Project", tile.translated(-10.0f, 132.0f).withWidth(200.0f), juce::Justification::centred, false);
        g.setColour(juce::Colour(0xffdfe5eb));
        g.fillRect(bounds.getX(), bounds.getBottom() - footerHeight, bounds.getWidth(), footerHeight);
        return;
    }

    const auto setup = juce::Rectangle<float>(bounds.getX() + 22.0f, bounds.getY() + 110.0f, bounds.getWidth() * 0.57f, 150.0f);
    const auto stage = juce::Rectangle<float>(setup.getRight() + 16.0f, setup.getY(), bounds.getRight() - setup.getRight() - 38.0f, 150.0f);
    const auto details = juce::Rectangle<float>(bounds.getX() + 22.0f, setup.getBottom() + 16.0f, bounds.getWidth() - 44.0f, 96.0f);
    g.setColour(juce::Colour(0xffcfd8e1)); g.fillRect(bounds.getCentreX() - 45.0f, bounds.getY() + 43.0f, 90.0f, 2.0f);
    g.setColour(panel); g.fillRoundedRectangle(setup, 7.0f); g.fillRoundedRectangle(details, 7.0f);
    g.setColour(stagePanel); g.fillRoundedRectangle(stage, 7.0f);
    g.setColour(muted); g.setFont(10.0f);
    g.drawText("SETUP", setup.getX() + 12.0f, setup.getY() + 9.0f, 80.0f, 14.0f, juce::Justification::centredLeft, false);
    g.drawText("LIVE STAGE", stage.getX() + 12.0f, stage.getY() + 9.0f, 100.0f, 14.0f, juce::Justification::centredLeft, false);
    g.drawText("DETAILS", details.getX() + 12.0f, details.getY() + 8.0f, 100.0f, 14.0f, juce::Justification::centredLeft, false);
    const auto setupColumnWidth = setup.getWidth() * 0.5f;
    for (size_t index = 0; index < performanceRoom.getMembers().size(); ++index)
    {
        const auto x = setup.getX() + 12.0f + static_cast<float>(index / 3) * setupColumnWidth;
        const auto y = setup.getY() + 38.0f + static_cast<float>(index % 3) * 34.0f;
        PerformanceRoomIconLibrary::draw(g, { x, y + 3.0f, 16.0f, 16.0f }, performanceRoom.getMembers()[index].role);
    }
    auto y = stage.getY() + 30.0f;
    for (const auto& member : performanceRoom.getMembers())
        for (int copy = 0; copy < member.quantity && y < stage.getBottom() - 20.0f; ++copy, y += 24.0f)
        {
            g.setColour(juce::Colours::white); g.fillRoundedRectangle(stage.getX() + 9.0f, y, stage.getWidth() - 18.0f, 21.0f, 4.0f);
            PerformanceRoomIconLibrary::draw(g, { stage.getX() + 16.0f, y + 2.0f, 16.0f, 16.0f }, member.role);
            g.setColour(text); g.setFont(11.0f); g.drawText(member.displayName + " " + juce::String(copy + 1), stage.getX() + 39.0f, y, stage.getWidth() - 52.0f, 21.0f, juce::Justification::centredLeft, true);
        }
    g.setColour(text); g.setFont(10.0f);
    const auto input = juce::Rectangle<float>(details.getX() + 12.0f, details.getY() + 42.0f, details.getWidth() * 0.46f, 26.0f);
    const auto output = juce::Rectangle<float>(details.getCentreX() + 4.0f, details.getY() + 42.0f, details.getWidth() * 0.46f, 26.0f);
    g.drawText("Audio Input", input.getX(), details.getY() + 25.0f, 180.0f, 13.0f, juce::Justification::centredLeft, false);
    g.drawText("Audio Output", output.getX(), details.getY() + 25.0f, 180.0f, 13.0f, juce::Justification::centredLeft, false);
    g.setColour(muted);
    g.drawText("Choose the active mic/line and speaker channels.", details.getX() + 12.0f, details.getY() + 73.0f, details.getWidth() - 250.0f, 16.0f, juce::Justification::centredLeft, true);
}

void StartupWorkflowPane::resized()
{
    const auto bounds = getDialogBounds();
    title.setBounds(bounds.getX(), bounds.getY(), bounds.getWidth(), 38);
    const auto splash = step == Step::splash, chooser = step == Step::chooseProject, room = step == Step::performanceRoom;
    progress.setBounds(bounds.reduced(24)); progress.setVisible(splash);
    newProject.setVisible(chooser); openProject.setVisible(chooser); enterPerformance.setVisible(room); cancel.setVisible(room);
    audioInputSelector.setVisible(room); audioOutputSelector.setVisible(room); configureAudioDevice.setVisible(room);
    for (size_t i = 0; i < recentProjectButtons.size(); ++i) recentProjectButtons[i].setVisible(chooser && i < static_cast<size_t>(recentProjects.size()));
    for (auto& button : performancePresets) button.setVisible(room);
    for (size_t i = 0; i < PerformanceRoomModel::memberCount; ++i)
        for (juce::Component* component : { static_cast<juce::Component*>(&setupLabels[i]), static_cast<juce::Component*>(&setupQuantities[i]), static_cast<juce::Component*>(&setupMinus[i]), static_cast<juce::Component*>(&setupPlus[i]) }) component->setVisible(room);
    if (chooser)
    {
        const auto contentX = bounds.getX() + 168, footerY = bounds.getBottom() - 52;
        newProject.setBounds(contentX + 46, bounds.getY() + 64, 180, 120); openProject.setBounds(contentX + 12, footerY + 14, 184, 24);
        for (size_t i = 0; i < recentProjectButtons.size(); ++i) recentProjectButtons[i].setBounds(bounds.getX() + 14, bounds.getY() + 98 + static_cast<int>(i) * 24, 140, 22);
        return;
    }
    if (! room) return;
    auto presets = juce::Rectangle<int>(bounds.getX() + 22, bounds.getY() + 58, bounds.getWidth() - 44, 34);
    const auto presetWidth = presets.getWidth() / static_cast<int>(performancePresets.size());
    for (auto& button : performancePresets) button.setBounds(presets.removeFromLeft(presetWidth).reduced(3, 0));
    const auto setup = juce::Rectangle<int>(bounds.getX() + 22, bounds.getY() + 110, juce::roundToInt(bounds.getWidth() * 0.57f), 150);
    const auto columnWidth = setup.getWidth() / 2;
    for (size_t i = 0; i < PerformanceRoomModel::memberCount; ++i)
    {
        const auto x = setup.getX() + static_cast<int>(i / 3) * columnWidth + 12;
        const auto y = setup.getY() + 34 + static_cast<int>(i % 3) * 34;
        setupLabels[i].setBounds(x + 22, y, 104, 24); setupQuantities[i].setBounds(x + 128, y + 1, 24, 22);
        setupMinus[i].setBounds(x + 155, y + 1, 21, 22); setupPlus[i].setBounds(x + 179, y + 1, 21, 22);
    }
    const auto details = juce::Rectangle<int>(bounds.getX() + 22, setup.getBottom() + 16, bounds.getWidth() - 44, 96);
    audioInputSelector.setBounds(details.getX() + 12, details.getY() + 42, juce::roundToInt(details.getWidth() * 0.46f), 26);
    audioOutputSelector.setBounds(details.getCentreX() + 4, details.getY() + 42, juce::roundToInt(details.getWidth() * 0.46f), 26);
    configureAudioDevice.setBounds(details.getRight() - 214, details.getY() + 72, 202, 20);
    const auto footer = bounds.withTrimmedTop(bounds.getHeight() - 46);
    cancel.setBounds(footer.getRight() - 78, footer.getY() + 8, 70, 30); enterPerformance.setBounds(footer.getRight() - 244, footer.getY() + 8, 158, 30);
}
