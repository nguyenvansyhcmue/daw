#include "StartupWorkflowPane.h"

namespace
{
const auto overlayColour = juce::Colour(0x99000000);
const auto dialogBackground = juce::Colour(0xfff7f8fa);
const auto sidebarBackground = juce::Colour(0xffe5e8eb);
const auto selectedBlue = juce::Colour(0xff3478d0);

void drawProjectGlyph(juce::Graphics& g, juce::Rectangle<float> bounds, bool liveLoops)
{
    g.setColour(liveLoops ? juce::Colour(0xff8a939d) : juce::Colour(0xff566574));
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(juce::Colours::white.withAlpha(0.94f));
    if (liveLoops)
    {
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                g.fillRoundedRectangle(bounds.getX() + 20.0f + column * 20.0f,
                                       bounds.getY() + 18.0f + row * 18.0f, 12.0f, 12.0f, 2.0f);
    }
    else
    {
        for (int row = 0; row < 3; ++row)
            g.fillRoundedRectangle(bounds.getX() + 19.0f, bounds.getY() + 17.0f + row * 17.0f,
                                   row == 0 ? 47.0f : 78.0f, 10.0f, 2.0f);
    }
    g.setColour(juce::Colours::white.withAlpha(0.82f));
    g.fillEllipse(bounds.getRight() - 25.0f, bounds.getBottom() - 25.0f, 19.0f, 19.0f);
    g.setColour(liveLoops ? juce::Colour(0xff68737e) : juce::Colour(0xff3e4b57));
    g.drawText("+", bounds.getRight() - 25.0f, bounds.getBottom() - 25.0f, 19.0f, 19.0f,
               juce::Justification::centred, false);
}

struct TrackCreationLayout
{
    juce::Rectangle<int> midiCard, audioCard;
    juce::Rectangle<int> midiTitle, audioTitle;
    juce::Rectangle<int> softwareInstrument, externalMidi, micOrLine, guitarOrBass;
    juce::Rectangle<int> details, footer;
};

TrackCreationLayout getTrackCreationLayout(juce::Rectangle<int> dialog)
{
    TrackCreationLayout layout;
    auto content = dialog.withTrimmedTop(36).reduced(22, 14);
    auto choices = content.removeFromTop(156);
    layout.midiCard = choices.removeFromLeft(choices.getWidth() / 2).reduced(4);
    layout.audioCard = choices.reduced(4);

    const auto setCardControls = [] (juce::Rectangle<int> card, juce::Rectangle<int>& title,
                                     juce::Rectangle<int>& first, juce::Rectangle<int>& second)
    {
        title = card.withTrimmedTop(48).withHeight(20);
        auto options = card.withTrimmedTop(76).reduced(12, 0);
        first = options.removeFromTop(28);
        second = options.withTrimmedTop(6).withHeight(28);
    };
    setCardControls(layout.midiCard, layout.midiTitle, layout.softwareInstrument, layout.externalMidi);
    setCardControls(layout.audioCard, layout.audioTitle, layout.micOrLine, layout.guitarOrBass);
    layout.details = content.withTrimmedTop(12).withHeight(116);
    layout.footer = dialog.withTrimmedTop(dialog.getHeight() - 44);
    return layout;
}

void drawTrackTypeCard(juce::Graphics& g, juce::Rectangle<float> bounds, juce::StringRef title,
                       juce::Colour accent, bool selected, bool audioType)
{
    g.setColour(juce::Colours::white);
    g.fillRoundedRectangle(bounds, 7.0f);
    g.setColour(selected ? accent : juce::Colour(0xffd7dde3));
    g.drawRoundedRectangle(bounds, 7.0f, selected ? 2.0f : 1.0f);

    const auto icon = juce::Rectangle<float>(bounds.getCentreX() - 18.0f, bounds.getY() + 12.0f, 36.0f, 36.0f);
    g.setColour(accent);
    g.fillEllipse(icon);
    g.setColour(juce::Colours::white);
    if (audioType)
    {
        for (int index = 0; index < 4; ++index)
        {
            const auto height = 8.0f + static_cast<float>((index % 2) * 8);
            g.fillRoundedRectangle(icon.getX() + 8.0f + static_cast<float>(index) * 5.0f,
                                   icon.getCentreY() - height * 0.5f, 2.0f, height, 1.0f);
        }
    }
    else
    {
        g.fillEllipse(icon.getX() + 10.0f, icon.getY() + 22.0f, 9.0f, 7.0f);
        g.fillRect(icon.getX() + 18.0f, icon.getY() + 10.0f, 3.0f, 16.0f);
        g.fillRect(icon.getX() + 20.0f, icon.getY() + 10.0f, 8.0f, 3.0f);
    }
    g.setColour(juce::Colour(0xff34414c));
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText(title, bounds.getX(), bounds.getY() + 49.0f, bounds.getWidth(), 18.0f,
               juce::Justification::centred, false);
}
}

StartupWorkflowPane::StartupWorkflowPane()
{
    addAndMakeVisible(title); addAndMakeVisible(emptyProject); addAndMakeVisible(audioRecordingProject);
    addAndMakeVisible(midiProductionProject); addAndMakeVisible(openProject); addAndMakeVisible(chooseProject);
    for (size_t index = 0; index < recentProjectButtons.size(); ++index)
    {
        auto& button = recentProjectButtons[index];
        addAndMakeVisible(button);
        button.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff59656f));
        button.onClick = [this, index]
        {
            if (index < static_cast<size_t>(recentProjects.size()) && onOpenRecentProject != nullptr)
                onOpenRecentProject(juce::File(recentProjects[static_cast<int>(index)]));
        };
    }
    addAndMakeVisible(midi); addAndMakeVisible(audio);
    addAndMakeVisible(softwareInstrument); addAndMakeVisible(externalMidi);
    addAndMakeVisible(micOrLine); addAndMakeVisible(guitarOrBass);
    addChildComponent(count);
    addAndMakeVisible(decreaseTrackCount); addAndMakeVisible(trackCountDisplay); addAndMakeVisible(increaseTrackCount);
    addAndMakeVisible(create); addAndMakeVisible(cancel); addAndMakeVisible(progress);
    title.setJustificationType(juce::Justification::centred);
    title.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, juce::Colours::white);
    for (auto* templateButton : { &emptyProject, &audioRecordingProject, &midiProductionProject })
    {
        templateButton->setButtonText({});
        templateButton->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        templateButton->setColour(juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
        templateButton->onStateChange = [this] { repaint(); };
    }
    openProject.setColour(juce::TextButton::buttonColourId, juce::Colour(0xfff8fbfd));
    openProject.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff3d454c));
    chooseProject.setColour(juce::TextButton::buttonColourId, selectedBlue);
    chooseProject.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    create.setColour(juce::TextButton::buttonColourId, selectedBlue);
    for (auto* typeButton : { &softwareInstrument, &externalMidi, &micOrLine, &guitarOrBass })
    {
        typeButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xfff7f8fa));
        typeButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xff33404b));
    }
    for (auto* cardButton : { &midi, &audio })
    {
        cardButton->setButtonText({});
        cardButton->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        cardButton->setColour(juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
        cardButton->onStateChange = [this] { repaint(); };
    }
    emptyProject.onClick = [this] { selectedProjectTemplate = ProjectTemplate::empty; repaint(); };
    audioRecordingProject.onClick = [this] { selectedProjectTemplate = ProjectTemplate::audioRecording; repaint(); };
    midiProductionProject.onClick = [this] { selectedProjectTemplate = ProjectTemplate::midiProduction; repaint(); };
    chooseProject.onClick = [this]
    {
        if (onTemplateSelected != nullptr)
            onTemplateSelected(selectedProjectTemplate);
    };
    openProject.onClick = [this] { if (onOpenProject) onOpenProject(); };
    midi.onClick = [this] { guitarInputSelected = false; setType(TrackType::instrument); };
    softwareInstrument.onClick = [this] { guitarInputSelected = false; setType(TrackType::instrument); };
    externalMidi.onClick = [this] { guitarInputSelected = false; setType(TrackType::externalMidi); };
    audio.onClick = [this] { guitarInputSelected = false; setType(TrackType::audio); };
    micOrLine.onClick = [this] { guitarInputSelected = false; setType(TrackType::audio); };
    guitarOrBass.onClick = [this] { guitarInputSelected = true; setType(TrackType::audio); };
    count.setRange(1, availableTrackSlots, 1);
    count.setValue(1);
    count.onValueChange = [this] { updateTrackCountDisplay(); };
    for (auto* button : { &decreaseTrackCount, &increaseTrackCount })
    {
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xffd2dbe5));
        button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xff34414c));
        button->setTooltip(button == &decreaseTrackCount ? "Remove one track" : "Add one track");
    }
    trackCountDisplay.setEnabled(false);
    trackCountDisplay.setColour(juce::TextButton::buttonColourId, juce::Colours::white);
    trackCountDisplay.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff34414c));
    decreaseTrackCount.onClick = [this] { count.setValue(juce::jmax(1.0, count.getValue() - 1.0)); };
    increaseTrackCount.onClick = [this] { count.setValue(juce::jmin(static_cast<double>(juce::jmax(1, availableTrackSlots)), count.getValue() + 1.0)); };
    updateTrackCountDisplay();
    create.onClick = [this]
    {
        if (onCreateTracks != nullptr && onCreateTracks(selectedType, juce::roundToInt(count.getValue())) > 0)
            setVisible(false);
    };
    cancel.onClick = [this]
    {
        if (returnToWorkspaceOnCancel)
        {
            setVisible(false);
            return;
        }
        step = Step::chooseProject;
        title.setText("Choose a Project", juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, juce::Colour(0xff2d343b));
        resized();
        repaint();
    };
    step = Step::splash; title.setText("StudioForge DAW", juce::dontSendNotification);
    setType(selectedType);
    progress.setJustificationType(juce::Justification::centred);
    progress.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.8f));
    setAlpha(0.0f);
    startTimerHz(60);
}
void StartupWorkflowPane::timerCallback()
{
    if (step == Step::splash && --splashFramesRemaining <= 0)
    {
        step = Step::chooseProject;
        title.setText("Choose a Project", juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, juce::Colour(0xff2d343b));
        currentAlpha = 0.76f;
        resized();
        repaint();
    }

    currentAlpha = juce::jmin(1.0f, currentAlpha + 0.08f);
    setAlpha(currentAlpha);
    if (currentAlpha >= 1.0f && step != Step::splash)
        stopTimer();
}
void StartupWorkflowPane::beginFadeIn()
{
    currentAlpha = 0.76f;
    setAlpha(currentAlpha);
    startTimerHz(60);
}
void StartupWorkflowPane::showTrackCreation(bool returnToWorkspace)
{
    returnToWorkspaceOnCancel = returnToWorkspace;
    step = Step::createTrack;
    title.setText("Create New Track", juce::dontSendNotification);
    title.setColour(juce::Label::textColourId, juce::Colour(0xff2d343b));
    resized();
    repaint();
    beginFadeIn();
}

void StartupWorkflowPane::setRecentProjects(juce::StringArray recentProjectPaths)
{
    recentProjects = std::move(recentProjectPaths);
    for (size_t index = 0; index < recentProjectButtons.size(); ++index)
    {
        const auto visible = index < static_cast<size_t>(recentProjects.size());
        recentProjectButtons[index].setButtonText(visible
            ? juce::File(recentProjects[static_cast<int>(index)]).getFileNameWithoutExtension() : juce::String {});
    }
    resized();
    repaint();
}

void StartupWorkflowPane::setAvailableTrackSlots(int slots)
{
    availableTrackSlots = juce::jmax(0, slots);
    const auto maximum = juce::jmax(1, availableTrackSlots);
    count.setRange(1, maximum, 1);
    count.setValue(juce::jmin(count.getValue(), static_cast<double>(maximum)), juce::dontSendNotification);
    decreaseTrackCount.setEnabled(availableTrackSlots > 0 && count.getValue() > 1.0);
    increaseTrackCount.setEnabled(availableTrackSlots > 0 && count.getValue() < static_cast<double>(maximum));
    create.setEnabled(availableTrackSlots > 0);
    updateTrackCountDisplay();
    repaint();
}

void StartupWorkflowPane::updateTrackCountDisplay()
{
    const auto current = juce::roundToInt(count.getValue());
    trackCountDisplay.setButtonText(juce::String(current));
    decreaseTrackCount.setEnabled(availableTrackSlots > 0 && current > 1);
    increaseTrackCount.setEnabled(availableTrackSlots > 0 && current < juce::jmax(1, availableTrackSlots));
}

void StartupWorkflowPane::setAudioDeviceChannels(int inputChannels, int outputChannels)
{
    audioInputChannels = juce::jmax(0, inputChannels);
    audioOutputChannels = juce::jmax(0, outputChannels);
    repaint();
}
void StartupWorkflowPane::setType(TrackType type)
{
    selectedType = type;
    const auto midiActive = type == TrackType::instrument || type == TrackType::externalMidi;
    const auto audioActive = type == TrackType::audio;
    const auto inactive = juce::Colour(0xfff7f8fa);

    const auto setOption = [inactive] (juce::TextButton& button, bool active, juce::Colour colour)
    {
        button.setColour(juce::TextButton::buttonColourId, active ? colour : inactive);
        button.setColour(juce::TextButton::textColourOffId, active ? juce::Colours::white : juce::Colour(0xff46525c));
    };
    setOption(softwareInstrument, type == TrackType::instrument, juce::Colour(0xff24945b));
    setOption(externalMidi, type == TrackType::externalMidi, juce::Colour(0xff7559b2));
    setOption(micOrLine, audioActive && !guitarInputSelected, juce::Colour(0xff3478d0));
    setOption(guitarOrBass, audioActive && guitarInputSelected, juce::Colour(0xff3478d0));
    repaint();
}
juce::Rectangle<int> StartupWorkflowPane::getDialogBounds() const
{
    const auto chooser = step == Step::chooseProject;
    const auto splash = step == Step::splash;
    const auto width = chooser ? juce::jlimit(640, 780, getWidth() - 96)
                               : splash ? juce::jlimit(420, 560, getWidth() - 120)
                                        : juce::jlimit(620, 720, getWidth() - 96);
    const auto height = chooser ? juce::jlimit(420, 480, getHeight() - 90)
                                : splash ? juce::jlimit(180, 260, getHeight() - 100)
                                         : juce::jlimit(400, 430, getHeight() - 90);
    return getLocalBounds().withSizeKeepingCentre(width, height);
}

void StartupWorkflowPane::paint(juce::Graphics& g)
{
    g.fillAll(overlayColour);
    const auto dialog = getDialogBounds().toFloat();

    if (step == Step::splash)
    {
        g.setColour(juce::Colour(0xff202a31));
        g.fillRoundedRectangle(dialog, 12.0f);
        g.setColour(juce::Colour(0xff4b9dea));
        g.fillRoundedRectangle(dialog.withHeight(4.0f), 2.0f);
        return;
    }

    g.setColour(dialogBackground);
    g.fillRoundedRectangle(dialog, 10.0f);
    g.setColour(juce::Colours::black.withAlpha(0.16f));
    g.drawRoundedRectangle(dialog, 10.0f, 1.0f);

    if (step == Step::createTrack)
    {
        const auto layout = getTrackCreationLayout(getDialogBounds());
        const auto midiActive = selectedType == TrackType::instrument || selectedType == TrackType::externalMidi;
        const auto midiHovered = midi.isMouseOver(true);
        const auto audioHovered = audio.isMouseOver(true);
        drawTrackTypeCard(g, layout.midiCard.toFloat(), "MIDI", juce::Colour(0xff32a56a), midiActive || midiHovered, false);
        drawTrackTypeCard(g, layout.audioCard.toFloat(), "Audio", juce::Colour(0xff3f82d3),
                          selectedType == TrackType::audio || audioHovered, true);

        const auto details = layout.details.toFloat();
        g.setColour(juce::Colour(0xffe6e9ed));
        g.fillRoundedRectangle(details, 5.0f);
        g.setColour(juce::Colour(0xff6c7680));
        g.setFont(11.0f);
        g.drawText("Details", details.getX() + 12.0f, details.getY() + 8.0f, 140.0f, 18.0f,
                   juce::Justification::centredLeft, false);
        const auto leftLabel = selectedType == TrackType::instrument ? "Instrument" : selectedType == TrackType::audio ? "Audio Input" : "External MIDI";
        const auto leftValue = selectedType == TrackType::instrument ? "StudioForge Tone Generator"
            : selectedType == TrackType::audio
                ? (audioInputChannels > 0 ? (guitarInputSelected ? "Guitar or Bass - device input (" : "Mic or Line - device input (")
                                                    + juce::String(audioInputChannels) + " ch)"
                                          : "No audio input available")
                : "No external MIDI device configured";
        g.setColour(juce::Colour(0xff424b54));
        g.setFont(11.0f);
        g.drawText(leftLabel, details.getX() + 12.0f, details.getY() + 34.0f, 220.0f, 16.0f,
                   juce::Justification::centredLeft, false);
        g.setColour(juce::Colours::white);
        g.fillRoundedRectangle(details.getX() + 12.0f, details.getY() + 52.0f, details.getWidth() * 0.46f, 26.0f, 4.0f);
        g.setColour(juce::Colour(0xff4d5862));
        g.drawText(leftValue, details.getX() + 20.0f, details.getY() + 52.0f, details.getWidth() * 0.46f - 16.0f, 26.0f,
                   juce::Justification::centredLeft, true);
        g.drawText("Audio Output", details.getX() + details.getWidth() * 0.54f, details.getY() + 34.0f,
                   180.0f, 16.0f, juce::Justification::centredLeft, false);
        g.setColour(juce::Colours::white);
        g.fillRoundedRectangle(details.getX() + details.getWidth() * 0.54f, details.getY() + 52.0f,
                               details.getWidth() * 0.42f, 26.0f, 4.0f);
        g.setColour(juce::Colour(0xff4d5862));
        g.drawText(audioOutputChannels > 0 ? "Stereo Out (" + juce::String(audioOutputChannels) + " ch)" : "No output available",
                   details.getX() + details.getWidth() * 0.54f + 8.0f, details.getY() + 52.0f,
                   details.getWidth() * 0.42f - 16.0f, 26.0f, juce::Justification::centredLeft, true);
        const auto counterWidth = 170 + 8 + 24 + 34 + 24;
        const auto counterX = layout.footer.getCentreX() - counterWidth / 2;
        g.setColour(juce::Colour(0xff6c7680));
        g.drawText(availableTrackSlots > 0 ? "Number of tracks to create:" : "Track limit reached (14 tracks)",
                   static_cast<float>(counterX), static_cast<float>(layout.footer.getY() + 14), 170.0f, 16.0f,
                   juce::Justification::centredLeft, false);
        return;
    }

    if (step != Step::chooseProject)
        return;

    const auto footerHeight = 52.0f;
    const auto sidebar = juce::Rectangle<float>(dialog.getX(), dialog.getY() + 36.0f, 168.0f,
                                                dialog.getHeight() - 36.0f - footerHeight);
    g.setColour(sidebarBackground);
    g.fillRect(sidebar);
    g.setColour(juce::Colours::black.withAlpha(0.12f));
    g.drawVerticalLine(juce::roundToInt(sidebar.getRight()), sidebar.getY(), sidebar.getBottom());

    const std::array<juce::String, 1> navigation { "New Project" };
    for (size_t index = 0; index < navigation.size(); ++index)
    {
        const auto row = juce::Rectangle<float>(sidebar.getX() + 10.0f, sidebar.getY() + 14.0f + static_cast<float>(index) * 27.0f,
                                                sidebar.getWidth() - 20.0f, 23.0f);
        if (index == 0)
        {
            g.setColour(juce::Colour(0xffd0d6dc));
            g.fillRoundedRectangle(row, 4.0f);
        }
        g.setColour(index == 0 ? juce::Colour(0xff303940) : juce::Colour(0xff8b949c));
        g.setFont(index == 0 ? 12.0f : 10.0f);
        g.drawText(navigation[index], row.reduced(10.0f, 0.0f), juce::Justification::centredLeft, false);
    }
    if (! recentProjects.isEmpty())
    {
        g.setColour(juce::Colour(0xff77818a));
        g.setFont(10.0f);
        g.drawText("RECENT", sidebar.getX() + 18.0f, sidebar.getY() + 66.0f,
                   sidebar.getWidth() - 36.0f, 16.0f, juce::Justification::centredLeft, false);
    }

    const auto content = juce::Rectangle<float>(sidebar.getRight(), dialog.getY() + 36.0f,
                                                dialog.getRight() - sidebar.getRight(), dialog.getHeight() - 92.0f);
    const std::array<ProjectTemplate, 3> templates { ProjectTemplate::empty, ProjectTemplate::audioRecording, ProjectTemplate::midiProduction };
    for (size_t index = 0; index < templates.size(); ++index)
    {
        const auto tile = juce::Rectangle<float>(content.getX() + 26.0f + static_cast<float>(index) * 142.0f,
                                                 content.getY() + 28.0f, 124.0f, 100.0f);
        const auto selected = templates[index] == selectedProjectTemplate;
        if (selected)
        {
            g.setColour(selectedBlue);
            g.drawRoundedRectangle(tile.expanded(3.0f), 8.0f, 2.0f);
        }
        drawProjectGlyph(g, tile, templates[index] == ProjectTemplate::midiProduction);
        g.setColour(selected ? selectedBlue : juce::Colour(0xff4f5962));
        g.setFont(12.0f);
        g.drawText(ProjectTemplates::getName(templates[index]), tile.translated(-10.0f, 114.0f).withWidth(144.0f),
                   juce::Justification::centred, false);
    }

    const auto detailsY = dialog.getBottom() - footerHeight;
    g.setColour(juce::Colour(0xffdfe5eb));
    g.fillRect(dialog.getX(), detailsY, dialog.getWidth(), footerHeight);
    g.setColour(juce::Colour(0xff68737c));
    g.setFont(11.0f);
    g.drawText("›  Details", dialog.getX() + 12.0f, detailsY + 7.0f, 180.0f, 18.0f, juce::Justification::centredLeft, false);
    g.drawText(ProjectTemplates::getDescription(selectedProjectTemplate), dialog.getCentreX() - 140.0f, detailsY + 7.0f,
               300.0f, 18.0f, juce::Justification::centred, false);
    g.setColour(juce::Colour(0xffdfe5eb));
    g.fillRect(dialog.getX() + 1.0f, detailsY + 1.0f, 172.0f, 30.0f);
    g.setColour(juce::Colour(0xff68737c));
    g.drawText("Details", dialog.getX() + 12.0f, detailsY + 7.0f, 120.0f, 18.0f,
               juce::Justification::centredLeft, false);
}
void StartupWorkflowPane::resized()
{
    auto card = getDialogBounds();
    title.setBounds(card.removeFromTop(36));
    const auto splash=step==Step::splash, chooser=step==Step::chooseProject, creating=!chooser && !splash;
    emptyProject.setVisible(chooser); audioRecordingProject.setVisible(chooser); midiProductionProject.setVisible(chooser);
    openProject.setVisible(chooser); chooseProject.setVisible(chooser);
    midi.setVisible(creating); audio.setVisible(creating);
    softwareInstrument.setVisible(creating); externalMidi.setVisible(creating);
    micOrLine.setVisible(creating); guitarOrBass.setVisible(creating);
    count.setVisible(false);
    decreaseTrackCount.setVisible(creating); trackCountDisplay.setVisible(creating); increaseTrackCount.setVisible(creating);
    create.setVisible(creating); cancel.setVisible(creating); progress.setVisible(splash);
    for (size_t index = 0; index < recentProjectButtons.size(); ++index)
        recentProjectButtons[index].setVisible(chooser && index < static_cast<size_t>(recentProjects.size()));
    progress.setBounds(card.reduced(24));
    if (chooser)
    {
        const auto contentX = card.getX() + 168;
        const auto footerTop = card.getBottom() - 52;
        emptyProject.setBounds(contentX + 20, card.getY() + 28, 130, 108);
        audioRecordingProject.setBounds(contentX + 162, card.getY() + 28, 130, 108);
        midiProductionProject.setBounds(contentX + 304, card.getY() + 28, 130, 108);
        openProject.setBounds(contentX + 12, footerTop + 25, 184, 24);
        chooseProject.setBounds(card.getRight() - 94, footerTop + 23, 82, 26);
        for (size_t index = 0; index < recentProjectButtons.size(); ++index)
            recentProjectButtons[index].setBounds(card.getX() + 14, card.getY() + 98 + static_cast<int>(index) * 24,
                                                  140, 22);
    }
    else
    {
        const auto layout = getTrackCreationLayout(getDialogBounds());
        // The family selectors deliberately cover each complete card.  The
        // option buttons were added after them and remain on top, so a click
        // on an option chooses that detail while any other card click selects
        // the MIDI or Audio family.
        midi.setBounds(layout.midiCard);
        audio.setBounds(layout.audioCard);
        softwareInstrument.setBounds(layout.softwareInstrument);
        externalMidi.setBounds(layout.externalMidi);
        micOrLine.setBounds(layout.micOrLine);
        guitarOrBass.setBounds(layout.guitarOrBass);
        const auto footer = layout.footer;
        const auto counterWidth = 170 + 8 + 24 + 34 + 24;
        const auto counterX = footer.getCentreX() - counterWidth / 2;
        decreaseTrackCount.setBounds(counterX + 178, footer.getY() + 10, 24, 24);
        trackCountDisplay.setBounds(counterX + 202, footer.getY() + 10, 34, 24);
        increaseTrackCount.setBounds(counterX + 236, footer.getY() + 10, 24, 24);
        cancel.setBounds(footer.getRight() - 78, footer.getY() + 8, 72, 30);
        create.setBounds(footer.getRight() - 168, footer.getY() + 8, 80, 30);
    }
}
