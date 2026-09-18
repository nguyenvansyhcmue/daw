#pragma once

#include <array>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioPeakMeter.h"
#include "Inspector/InspectorViewState.h"
#include "../Models/TrackDataModel.h"

class AudioEngine;
class PluginHostService;

class RegionInspectorComponent final : public juce::Component
{
public:
    explicit RegionInspectorComponent(TrackDataModel& model);
    void setSelectedAudioClip(ClipId clip);
    void setSelectedMidiClip(MidiClipId clip);
    void clearSelection();
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void applyViewState(const InspectorRegionViewState&);

    TrackDataModel& trackModel;
    ClipId selectedAudioClip;
    MidiClipId selectedMidiClip;
    juce::Label heading { {}, "REGION" };
    juce::Label context;
    juce::Label timelineDetails;
    juce::Label modifierDetails;
};

class TrackInspectorComponent final : public juce::Component, private juce::Timer
{
public:
    explicit TrackInspectorComponent(TrackDataModel& model);
    void setSelectedTrack(int trackIndex);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    void applyViewState(const InspectorTrackViewState&);

    TrackDataModel& trackModel;
    int selectedTrack = -1;
    juce::Label heading { {}, "TRACK" };
    juce::Label trackName;
    juce::Label inputDetails;
    juce::Label outputDetails;
    juce::TextButton automationMode { "Automation: Read" };
    juce::TextButton writeVolumeAutomation { "+ Vol" };
    juce::TextButton writePanAutomation { "+ Pan" };
    juce::ToggleButton recordEnable { "R" }, inputMonitoring { "I" }, mute { "M" }, solo { "S" }, soloSafe { "SAFE" };
};

enum class ChannelRole { selectedTrack, stereoOutput };

class ChannelStripComponent final : public juce::Component, private juce::Timer
{
public:
    ChannelStripComponent(ChannelRole role, TrackDataModel& model, AudioEngine& engine, PluginHostService& pluginHost);
    void setSelectedTrack(int trackIndex);
    void refresh();
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void synchroniseControlsFromState();
    void refreshAudioFx();
    void refreshRouting();
    void showAudioFxMenu(size_t slot);
    void showPluginBrowser(size_t slot);
    void showPluginEditor(size_t slot);
    BusId busForMenuItem(int itemId) const noexcept;
    void setSendRoute(size_t slot);
    void clearSendRoute(size_t slot);
    void showSendMenu();
    void refreshSendSummary();
    bool isSelectedTrackStrip() const noexcept { return channelRole == ChannelRole::selectedTrack; }

    ChannelRole channelRole;
    TrackDataModel& trackModel;
    AudioEngine& audioEngine;
    PluginHostService& pluginHost;
    std::unique_ptr<juce::FileChooser> pluginFileChooser;
    int selectedTrack = -1;
    std::array<BusId, TrackDataModel::maxBuses> routeBusIds {};
    juce::Rectangle<int> audioFxRackBounds;
    juce::Rectangle<int> sendsBounds;
    juce::Rectangle<int> routingBounds;
    std::array<juce::TextButton, TrackDataModel::maxFxSlots> audioFxSlots;
    juce::Label heading;
    juce::Label fxHeading { {}, "AUDIO FX" };
    juce::TextButton addAudioFx { "+ Add Audio FX" };
    juce::Label sendsHeading { {}, "SENDS" };
    juce::TextButton sendSummary { "No Sends  +" };
    std::array<juce::ComboBox, TrackDataModel::maxSendsPerTrack> sendRoutes;
    std::array<juce::Slider, TrackDataModel::maxSendsPerTrack> sendLevels;
    std::array<juce::ToggleButton, TrackDataModel::maxSendsPerTrack> sendPreFader;
    std::array<juce::TextButton, TrackDataModel::maxSendsPerTrack> clearSendButtons;
    juce::Label routingHeading { {}, "OUTPUT" };
    juce::ComboBox outputRoute;
    juce::Label panLabel { {}, "PAN" };
    juce::Label levelLabel { {}, "LEVEL" };
    juce::Slider pan;
    juce::Slider fader;
    AudioPeakMeter meter;
};
