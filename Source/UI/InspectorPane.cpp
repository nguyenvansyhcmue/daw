#include "InspectorPane.h"

#include "../AudioEngine/AudioEngine.h"
#include "../Plugins/PluginHostService.h"
#include "Theme/StudioForgeLookAndFeel.h"

namespace
{
constexpr int inspectorPadding = 4;
constexpr int sectionGap = 3;
constexpr int trackInspectorHeight = 104;
}

InspectorPane::InspectorPane(TrackDataModel& model, AudioEngine& engine, PluginHostService& pluginHost)
    : trackInspector(model),
      selectedTrackChannel(ChannelRole::selectedTrack, model, engine, pluginHost),
      stereoOutputChannel(ChannelRole::stereoOutput, model, engine, pluginHost)
{
    addAndMakeVisible(trackInspector);
    addAndMakeVisible(selectedTrackChannel);
    addAndMakeVisible(stereoOutputChannel);
    stereoOutputChannel.refresh();
}

void InspectorPane::setSelectedTrack(int trackIndex)
{
    trackInspector.setSelectedTrack(trackIndex);
    selectedTrackChannel.setSelectedTrack(trackIndex);
}

void InspectorPane::paint(juce::Graphics& g)
{
    g.fillAll(StudioForgeTheme::workspaceBackground);
    g.setColour(StudioForgeTheme::separator);
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
    trackInspector.setBounds(bounds.removeFromTop(trackInspectorHeight));
}

void InspectorPane::layoutChannelStrips(juce::Rectangle<int> bounds)
{
    bounds.removeFromTop(trackInspectorHeight + sectionGap);
    auto selected = bounds.removeFromLeft(bounds.getWidth() / 2 - sectionGap / 2);
    bounds.removeFromLeft(sectionGap);
    selectedTrackChannel.setBounds(selected);
    stereoOutputChannel.setBounds(bounds);
}
