#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "InspectorComponents.h"

class AudioEngine;
class PluginHostService;

class InspectorPane final : public juce::Component
{
public:
    InspectorPane(TrackDataModel& model, AudioEngine& engine, PluginHostService& pluginHost);
    void setSelectedTrack(int trackIndex);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void layoutContextInspectors(juce::Rectangle<int> bounds);
    void layoutChannelStrips(juce::Rectangle<int> bounds);

    TrackInspectorComponent trackInspector;
    ChannelStripComponent selectedTrackChannel;
    ChannelStripComponent stereoOutputChannel;
};
