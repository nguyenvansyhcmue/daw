#include "InspectorPane.h"

#include "../AudioEngine/AudioEngine.h"
#include "../Plugins/PluginHostService.h"

namespace
{
constexpr int inspectorPadding = 6;
constexpr int sectionGap = 5;
constexpr int regionInspectorHeight = 46;
constexpr int trackInspectorHeight = 48;
}

InspectorPane::InspectorPane(TrackDataModel& model, AudioEngine& engine, PluginHostService& pluginHost)
    : regionInspector(model),
      trackInspector(model),
      selectedTrackChannel(ChannelRole::selectedTrack, model, engine, pluginHost),
      stereoOutputChannel(ChannelRole::stereoOutput, model, engine, pluginHost)
{
    addAndMakeVisible(regionInspector);
    addAndMakeVisible(trackInspector);
    addAndMakeVisible(selectedTrackChannel);
    addAndMakeVisible(stereoOutputChannel);
    stereoOutputChannel.refresh();
}

void InspectorPane::setSelectedTrack(int trackIndex)
{
    regionInspector.clearSelection();
    trackInspector.setSelectedTrack(trackIndex);
    selectedTrackChannel.setSelectedTrack(trackIndex);
}

void InspectorPane::setSelectedAudioClip(ClipId clip)
{
    regionInspector.setSelectedAudioClip(clip);
}

void InspectorPane::setSelectedMidiClip(MidiClipId clip)
{
    regionInspector.setSelectedMidiClip(clip);
}

void InspectorPane::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff202328));
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
}

void InspectorPane::resized()
{
    auto bounds = getLocalBounds().reduced(inspectorPadding);
    layoutContextInspectors(bounds);
    layoutChannelStrips(bounds);
}

void InspectorPane::layoutContextInspectors(juce::Rectangle<int> bounds)
{
    regionInspector.setBounds(bounds.removeFromTop(regionInspectorHeight));
    bounds.removeFromTop(sectionGap);
    trackInspector.setBounds(bounds.removeFromTop(trackInspectorHeight));
}

void InspectorPane::layoutChannelStrips(juce::Rectangle<int> bounds)
{
    bounds.removeFromTop(regionInspectorHeight + trackInspectorHeight + sectionGap * 2);
    auto selected = bounds.removeFromLeft(bounds.getWidth() / 2 - sectionGap / 2);
    bounds.removeFromLeft(sectionGap);
    selectedTrackChannel.setBounds(selected);
    stereoOutputChannel.setBounds(bounds);
}
