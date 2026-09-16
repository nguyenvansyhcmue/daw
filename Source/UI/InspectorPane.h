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
    void setSelectedAudioClip(ClipId clip);
    void setSelectedMidiClip(MidiClipId clip);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void layoutContextInspectors(juce::Rectangle<int> bounds);
    void layoutChannelStrips(juce::Rectangle<int> bounds);

    RegionInspectorComponent regionInspector;
    TrackInspectorComponent trackInspector;
    ChannelStripComponent selectedTrackChannel;
    ChannelStripComponent stereoOutputChannel;
};
