#pragma once

#include <array>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioPeakMeter.h"
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
    TrackDataModel& trackModel;
    MidiClipId selectedMidiClip;
    juce::Label heading { {}, "REGION" };
    juce::Label context;
};

class TrackInspectorComponent final : public juce::Component
{
public:
    explicit TrackInspectorComponent(TrackDataModel& model);
    void setSelectedTrack(int trackIndex);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void refresh();

    TrackDataModel& trackModel;
    int selectedTrack = -1;
    juce::Label heading { {}, "TRACK" };
    juce::Label trackName;
    juce::ToggleButton recordEnable { "R" }, inputMonitoring { "I" }, mute { "M" }, solo { "S" };
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
    void refreshAudioFx();
    void refreshRouting();
    void showAudioFxMenu(size_t slot);
    BusId busForMenuItem(int itemId) const noexcept;
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
    juce::ComboBox sendRoute;
    juce::Slider sendLevel;
    juce::Label routingHeading { {}, "OUTPUT" };
    juce::ComboBox outputRoute;
    juce::Slider pan;
    juce::Slider fader;
    AudioPeakMeter meter;
};
