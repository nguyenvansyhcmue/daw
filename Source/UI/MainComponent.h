#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Models/TrackDataModel.h"
#include "../AudioEngine/AudioEngine.h"
#include "ArrangeWindow.h"
#include "AudioPeakMeter.h"
#include "ControlBar.h"
#include "MixerPane.h"
#include "PianoRoll.h"
#include "InspectorPane.h"
#include "AssetBrowserPane.h"
#include "PerformanceFooter.h"
#include "../Plugins/PluginHostService.h"

class LogicProLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LogicProLookAndFeel();

    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
    void drawRotarySlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;
    void drawLinearSlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float minSliderPos,
                          float maxSliderPos,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

class MainComponent final : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool handleGlobalKeyPress(const juce::KeyPress& key);

    juce::Result saveProject(const juce::File& file);
    juce::Result loadProject(const juce::File& file, juce::StringArray* missingMediaReferences = nullptr);
    juce::Result createNewProject();
    bool isProjectDirty() const noexcept;
    void markProjectSaved() noexcept;
    bool undoEdit();
    bool redoEdit();
    void setInspectorPanelVisible(bool visible);
    void setBrowserPanelVisible(bool visible);
    void setMixerPanelVisible(bool visible);
    bool addTrackFromCommand();
    void setWorkspaceTrackHeight(int height);
    bool isInspectorPanelVisible() const noexcept { return inspectorPane.isVisible(); }
    bool isBrowserPanelVisible() const noexcept { return assetBrowser.isVisible(); }
    bool isMixerPanelVisible() const noexcept { return mixerPane.isVisible(); }

    TrackDataModel& getTrackDataModel() noexcept { return trackDataModel; }

private:
    LogicProLookAndFeel lookAndFeel;
    TrackDataModel trackDataModel;
    AudioEngine audioEngine { &trackDataModel };
    PluginHostService pluginHost;
    ControlBar controlBar { &trackDataModel, &audioEngine };
    ArrangeWindow arrangeWindow { &trackDataModel };
    InspectorPane inspectorPane { &trackDataModel, &audioEngine };
    AssetBrowserPane assetBrowser;
    PerformanceFooter performanceFooter { &audioEngine };
    MixerPane mixerPane { &trackDataModel, &audioEngine, &pluginHost };
    PianoRoll pianoRoll { &trackDataModel };
    std::unique_ptr<juce::FileChooser> bounceFileChooser;
    uint64_t savedProjectRevision = 0;
};
