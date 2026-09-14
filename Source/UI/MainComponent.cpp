#include "MainComponent.h"
#include "../Project/ProjectSerializer.h"
#include "../Project/ProjectState.h"

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
const auto darkBackground = juce::Colour(0xff252526);
const auto panelBackground = juce::Colour(0xff343434);
const auto accentBlue = juce::Colour(0xff6f88a8);
const auto accentCyan = juce::Colour(0xffa7c5df);
}

LogicProLookAndFeel::LogicProLookAndFeel()
{
    setColour(juce::TextButton::buttonOnColourId, accentCyan);
    setColour(juce::ComboBox::backgroundColourId, panelBackground);
    setColour(juce::Slider::backgroundColourId, panelBackground);
    setColour(juce::Slider::trackColourId, accentBlue);
    setColour(juce::Slider::thumbColourId, accentCyan);
}

void LogicProLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                               juce::Button& button,
                                               const juce::Colour& backgroundColour,
                                               bool shouldDrawButtonAsHighlighted,
                                               bool shouldDrawButtonAsDown)
{
    const auto base = button.getToggleState() ? accentCyan.withAlpha(0.85f) : backgroundColour;
    const auto bounds = button.getLocalBounds().toFloat();
    auto gradient = juce::ColourGradient(base, bounds.getX(), bounds.getY(), accentBlue, bounds.getRight(), bounds.getBottom(), false);

    g.setGradientFill(gradient);
    g.fillRoundedRectangle(bounds.reduced(2.0f), 8.0f);

}

void LogicProLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                          int x,
                                          int y,
                                          int width,
                                          int height,
                                          float sliderPos,
                                          float rotaryStartAngle,
                                          float rotaryEndAngle,
                                          juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 10.0f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * sliderPos;

    g.setColour(juce::Colour(0xff2b2b2b));
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0, rotaryStartAngle, angle, true);
    g.setColour(accentCyan);
    g.strokePath(arc, juce::PathStrokeType(3.0f));

    const auto knobCentre = juce::Point<float>(centre.x + std::cos(angle) * (radius * 0.75f), centre.y + std::sin(angle) * (radius * 0.75f));
    g.setColour(juce::Colour(0xff00a8ff));
    g.fillEllipse(knobCentre.x - 5.0f, knobCentre.y - 5.0f, 10.0f, 10.0f);
}

void LogicProLookAndFeel::drawLinearSlider(juce::Graphics& g,
                                          int x,
                                          int y,
                                          int width,
                                          int height,
                                          float sliderPos,
                                          float minSliderPos,
                                          float maxSliderPos,
                                          juce::Slider::SliderStyle style,
                                          juce::Slider& slider)
{
    auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    g.setColour(panelBackground);
    g.fillRoundedRectangle(area.reduced(2.0f), 6.0f);

    g.setColour(accentBlue);
    const float trackWidth = area.getWidth() * 0.75f;
    const float trackX = area.getX() + area.getWidth() * 0.125f;
    const float trackY = area.getCentreY() - 2.0f;
    g.fillRect(trackX, trackY, trackWidth, 4.0f);

    const float knobX = juce::jlimit(trackX, trackX + trackWidth, trackX + trackWidth * sliderPos);
    g.setColour(accentCyan);
    g.fillEllipse(knobX - 8.0f, area.getCentreY() - 8.0f, 16.0f, 16.0f);
}

void LogicProLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const auto area = button.getLocalBounds().toFloat();
    g.setColour(button.getToggleState() ? accentCyan : juce::Colour(0xff565656));
    g.fillRoundedRectangle(area.reduced(2.0f), 6.0f);
    g.setColour(juce::Colours::white);
    g.drawText(button.getButtonText(), area.reduced(4.0f), juce::Justification::centred, true);
}

MainComponent::MainComponent()
{
    audioEngine.initialise();
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(controlBar);
    addAndMakeVisible(arrangeWindow);
    addChildComponent(inspectorPane);
    addChildComponent(assetBrowser);
    addAndMakeVisible(performanceFooter);
    addChildComponent(mixerPane);
    addChildComponent(pianoRoll);

    // Keep the selected channel's controls in view by default, matching the
    // arrangement-first workflow rather than starting with a blank left rail.
    inspectorPane.setVisible(true);
    controlBar.setInspectorVisible(true);
    assetBrowser.setVisible(true);
    controlBar.setBrowserVisible(true);

    controlBar.onMixerToggle = [this]
    {
        const auto visible = ! mixerPane.isVisible();
        mixerPane.setVisible(visible);
        controlBar.setMixerVisible(visible);
        resized();
    };
    controlBar.onInspectorToggle = [this]
    {
        const auto visible = ! inspectorPane.isVisible();
        inspectorPane.setVisible(visible);
        controlBar.setInspectorVisible(visible);
        resized();
    };
    controlBar.onPianoRollToggle = [this]
    {
        const auto visible = ! pianoRoll.isVisible();
        pianoRoll.setVisible(visible);
        controlBar.setPianoRollVisible(visible);
        resized();
    };
    controlBar.onBrowserToggle = [this]
    {
        const auto visible = ! assetBrowser.isVisible();
        assetBrowser.setVisible(visible);
        controlBar.setBrowserVisible(visible);
        resized();
    };
    arrangeWindow.onTrackSelected = [this](int track) { inspectorPane.setSelectedTrack(track); };
    arrangeWindow.onMidiClipSelected = [this](MidiClipId clip)
    {
        pianoRoll.setActiveClip(clip);
        pianoRoll.setVisible(true);
        controlBar.setPianoRollVisible(true);
        resized();
    };
    assetBrowser.onAudioFileActivated = [this](const juce::File& file)
    {
        arrangeWindow.importAudioFile(file, 0, trackDataModel.getPlayheadPosition());
    };
    performanceFooter.onBounceRequested = [this]
    {
        bounceFileChooser = std::make_unique<juce::FileChooser>("Bounce mixdown", juce::File {}, "*.wav");
        bounceFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                           | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& chooser)
            {
                const auto output = chooser.getResult();
                if (output != juce::File {})
                    audioEngine.renderOfflineWav(output.withFileExtension(".wav"), trackDataModel.getSampleRate());
                bounceFileChooser.reset();
            });
    };

    trackDataModel.ensureTrackCount(TrackDataModel::maxTracks);
    inspectorPane.setSelectedTrack(0);
    markProjectSaved();
}

MainComponent::~MainComponent()
{
    setLookAndFeel(nullptr);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    return handleGlobalKeyPress(key);
}

bool MainComponent::handleGlobalKeyPress(const juce::KeyPress& key)
{
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;

    if (key.isKeyCode(juce::KeyPress::spaceKey))
    {
        controlBar.togglePlayback();
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::returnKey))
    {
        controlBar.stopPlayback();
        trackDataModel.setPlayheadPosition(0.0);
        return true;
    }
    if (key.getKeyCode() == 'r' || key.getKeyCode() == 'R')
    {
        controlBar.toggleRecording();
        return true;
    }

    return false;
}

juce::Result MainComponent::saveProject(const juce::File& file)
{
    const auto result = ProjectSerializer::save(trackDataModel, file);
    if (result.wasOk())
        markProjectSaved();
    return result;
}

juce::Result MainComponent::loadProject(const juce::File& file, juce::StringArray* missingMediaReferences)
{
    audioEngine.setPlaybackState(false);
    const auto result = ProjectSerializer::load(trackDataModel, file, missingMediaReferences);
    if (result.wasOk())
    {
        inspectorPane.setSelectedTrack(0);
        mixerPane.refreshFromModel();
        arrangeWindow.repaint();
        pianoRoll.repaint();
        markProjectSaved();
    }
    return result;
}

juce::Result MainComponent::createNewProject()
{
    audioEngine.setPlaybackState(false);
    ProjectState state;
    state.tempoMap.push_back({ 0.0, 120.0 });
    for (uint64_t id = 1; id <= TrackDataModel::maxTracks; ++id)
        state.tracks.push_back({ { id } });

    const auto result = trackDataModel.applyProjectState(state);
    if (result.wasOk())
    {
        inspectorPane.setSelectedTrack(0);
        mixerPane.refreshFromModel();
        arrangeWindow.repaint();
        pianoRoll.repaint();
        markProjectSaved();
    }
    return result;
}

bool MainComponent::isProjectDirty() const noexcept
{
    return trackDataModel.getProjectRevision() != savedProjectRevision;
}

void MainComponent::markProjectSaved() noexcept
{
    savedProjectRevision = trackDataModel.getProjectRevision();
}

bool MainComponent::undoEdit()
{
    const auto changed = trackDataModel.undo();
    if (changed) { mixerPane.refreshFromModel(); arrangeWindow.repaint(); }
    return changed;
}

bool MainComponent::redoEdit()
{
    const auto changed = trackDataModel.redo();
    if (changed) { mixerPane.refreshFromModel(); arrangeWindow.repaint(); }
    return changed;
}

void MainComponent::setInspectorPanelVisible(bool visible)
{
    inspectorPane.setVisible(visible); controlBar.setInspectorVisible(visible); resized();
}

void MainComponent::setBrowserPanelVisible(bool visible)
{
    assetBrowser.setVisible(visible); controlBar.setBrowserVisible(visible); resized();
}

void MainComponent::setMixerPanelVisible(bool visible)
{
    mixerPane.setVisible(visible); controlBar.setMixerVisible(visible); resized();
}

bool MainComponent::addTrackFromCommand()
{
    if (! trackDataModel.addTrack().isValid())
        return false;
    arrangeWindow.repaint();
    mixerPane.refreshFromModel();
    return true;
}

void MainComponent::setWorkspaceTrackHeight(int height)
{
    trackDataModel.setTrackHeight(height);
    arrangeWindow.repaint();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(darkBackground);
    auto glow = juce::ColourGradient(accentBlue, 0.0f, 0.0f, accentCyan, getWidth(), 0.0f, false);
    g.setGradientFill(glow);
    g.fillRect(0.0f, 0.0f, static_cast<float>(getWidth()), 2.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    controlBar.setBounds(area.removeFromTop(54));
    performanceFooter.setBounds(area.removeFromBottom(38));

    // Reserve bottom panes before assigning the arrange area.  Giving ArrangeWindow
    // the full bounds first made these panes overlay its tracks instead of docking.
    if (mixerPane.isVisible())
        mixerPane.setBounds(area.removeFromBottom(210));
    if (pianoRoll.isVisible())
        pianoRoll.setBounds(area.removeFromBottom(250));
    if (inspectorPane.isVisible())
        inspectorPane.setBounds(area.removeFromLeft(230));
    if (assetBrowser.isVisible())
        assetBrowser.setBounds(area.removeFromRight(250));

    arrangeWindow.setBounds(area);
}
