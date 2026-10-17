#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

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
    TrackInspectorComponent(TrackDataModel& model, AudioEngine& engine);
    void setSelectedTrack(int trackIndex);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    void applyViewState(const InspectorTrackViewState&);

    TrackDataModel& trackModel;
    AudioEngine& audioEngine;
    int selectedTrack = -1;
    juce::Label heading { {}, "TRACK" };
    juce::Label trackName;
    juce::Label inputDetails;
    juce::Label outputDetails;
    juce::ToggleButton recordEnable { "R" }, inputMonitoring { "I" }, mute { "M" }, solo { "S" }, soloSafe { "SAFE" };
    juce::Label monitorStatus;
};

class AudioFxSlotButton final : public juce::TextButton
{
public:
    std::function<void()> onContextMenu;
    std::function<void()> onBypassClick;

    void setBypassControlState(bool slotHasPlugIn, bool slotIsBypassed) noexcept
    {
        hasPlugIn = slotHasPlugIn;
        bypassed = slotIsBypassed;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        bypassPressed = hasPlugIn && ! event.mods.isPopupMenu()
                        && bypassBounds().contains(event.getPosition());
        if (bypassPressed)
        {
            repaint();
            return;
        }

        juce::TextButton::mouseDown(event);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            if (onContextMenu != nullptr) onContextMenu();
            return;
        }
        if (bypassPressed)
        {
            const auto activateBypass = bypassBounds().contains(event.getPosition());
            bypassPressed = false;
            repaint();
            if (activateBypass && onBypassClick != nullptr)
                onBypassClick();
            return;
        }

        // Option-click is retained as a power-user shortcut. The primary
        // bypass affordance is the hover control at the left edge.
        if (hasPlugIn && event.mods.isAltDown())
        {
            setState(juce::Button::buttonNormal);
            if (onBypassClick != nullptr) onBypassClick();
            return;
        }
        juce::TextButton::mouseUp(event);
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        juce::TextButton::mouseMove(event);
        repaint();
    }

    void mouseExit(const juce::MouseEvent& event) override
    {
        juce::TextButton::mouseExit(event);
        repaint();
    }

private:
    juce::Rectangle<int> bypassBounds() const noexcept
    {
        return getLocalBounds().removeFromLeft(20).reduced(2, 2);
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        juce::TextButton::paintButton(g, highlighted, down);
        if (! hasPlugIn || ! isMouseOverOrDragging(true))
            return;

        const auto control = bypassBounds().toFloat();
        const auto hovered = bypassBounds().contains(getMouseXYRelative());
        const auto colour = bypassed ? juce::Colour(0xff94a2aa) : juce::Colour(0xff55d7e8);
        g.setColour(juce::Colours::black.withAlpha(hovered ? 0.28f : 0.16f));
        g.fillRoundedRectangle(control, 3.0f);
        g.setColour(colour.withAlpha(hovered ? 1.0f : 0.76f));
        const auto ring = control.reduced(5.0f);
        g.drawEllipse(ring, 1.15f);
        const auto centreX = ring.getCentreX();
        g.drawLine(centreX, ring.getY() - 0.4f, centreX, ring.getCentreY() - 0.5f, 1.25f);
    }

    bool hasPlugIn = false;
    bool bypassed = false;
    bool bypassPressed = false;
};

enum class ChannelRole { selectedTrack, stereoOutput, auxBus };

class SendRouteSelector final : public juce::ComboBox
{
public:
    std::function<void()> onReveal;

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (getSelectedId() >= 2 && onReveal != nullptr
            && (event.mods.isShiftDown() || event.position.x < static_cast<float>(getWidth() - 28)))
        {
            onReveal();
            return;
        }
        juce::ComboBox::mouseDown(event);
    }
};

class ChannelStripComponent final : public juce::Component, private juce::Timer
{
public:
    ChannelStripComponent(ChannelRole role, TrackDataModel& model, AudioEngine& engine, PluginHostService& pluginHost);
    void setSelectedTrack(int trackIndex);
    void setAuxBus(BusId bus);
    void setViewedAux(BusId bus);
    void refresh();
    std::function<void(BusId)> onRevealAuxBus;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void synchroniseControlsFromState();
    void refreshAudioFx();
    void refreshFxScenes();
    void refreshRouting();
    void refreshChannelSelector();
    const TrackDataModel::FxRackSnapshot* activeFxRack() const noexcept;
    bool insertAudioFx(size_t slot, std::shared_ptr<AudioEffectProcessor> processor,
                       int targetTrack = -1);
    void showAudioFxMenu(size_t slot);
    void showPluginBrowser(size_t slot);
    void showPluginEditor(size_t slot);
    void openPluginEditor(size_t slot);
    BusId busForMenuItem(int itemId) const noexcept;
    void setSendRoute(size_t slot);
    void clearSendRoute(size_t slot);
    void showSendMenu();
    void refreshSendSummary();
    bool isSelectedTrackStrip() const noexcept { return channelRole == ChannelRole::selectedTrack; }
    bool isAuxBusStrip() const noexcept { return channelRole == ChannelRole::auxBus; }

    ChannelRole channelRole;
    TrackDataModel& trackModel;
    AudioEngine& audioEngine;
    PluginHostService& pluginHost;
    std::unique_ptr<juce::FileChooser> pluginFileChooser;
    std::vector<std::unique_ptr<juce::DocumentWindow>> pluginEditorWindows;
    int selectedTrack = -1;
    BusId selectedBus;
    BusId viewedAux;
    std::array<BusId, TrackDataModel::maxBuses> routeBusIds {};
    juce::Rectangle<int> audioFxRackBounds;
    juce::Rectangle<int> sceneBounds;
    juce::Rectangle<int> sendsBounds;
    juce::Rectangle<int> routingBounds;
    std::array<AudioFxSlotButton, TrackDataModel::maxFxSlots> audioFxSlots;
    juce::Label heading;
    juce::ComboBox channelSelector;
    juce::Label fxHeading { {}, "AUDIO FX" };
    juce::TextButton addAudioFx { "+ Add Audio FX" };
    juce::Label sceneHeading { {}, "SCENES" };
    std::array<juce::TextButton, TrackDataModel::maxFxScenes> sceneButtons;
    juce::TextButton captureScene { "+ Save Scene" };
    juce::Label sendsHeading { {}, "FX SENDS" };
    juce::TextButton sendSummary { "+ Add FX Send" };
    std::array<SendRouteSelector, TrackDataModel::maxSendsPerTrack> sendRoutes;
    std::array<juce::Slider, TrackDataModel::maxSendsPerTrack> sendLevels;
    std::array<juce::Label, TrackDataModel::maxSendsPerTrack> sendLevelReadouts;
    std::array<juce::ToggleButton, TrackDataModel::maxSendsPerTrack> sendPreFader;
    std::array<juce::TextButton, TrackDataModel::maxSendsPerTrack> clearSendButtons;
    juce::Label routingHeading { {}, "OUTPUT" };
    juce::ComboBox outputRoute;
    juce::Label panLabel { {}, "PAN" };
    juce::Label levelLabel { {}, "LEVEL" };
    juce::Label panValue;
    juce::Slider pan;
    juce::Slider fader;
    AudioPeakMeter meter;
};
