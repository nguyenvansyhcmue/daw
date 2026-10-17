#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioPeakMeter.h"
#include "../Models/TrackDataModel.h"

class PluginHostService;

class AudioEngine;

class FxSlotButton final : public juce::TextButton
{
public:
    FxSlotButton() = default;
    std::function<void()> onRightClick;
    std::function<void()> onBypassClick;

    void setActive(bool shouldBeActive) noexcept
    {
        active = shouldBeActive;
        repaint();
    }

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
        if (event.mods.isRightButtonDown())
        {
            if (onRightClick != nullptr) onRightClick();
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

        if (hasPlugIn && event.mods.isAltDown())
        {
            setState(juce::Button::buttonNormal);
            if (onBypassClick != nullptr)
                onBypassClick();
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
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        // A real recessed insert well: the label/value stays native text and
        // receives the same hit testing as before.
        g.setColour(juce::Colour(0xff070c10));
        g.fillRoundedRectangle(bounds, 3.0f);

        auto inner = bounds.reduced(2.0f);
        juce::ColourGradient inset(juce::Colour(0xff10181d), inner.getX(), inner.getY(),
                                   juce::Colour(0xff1c262c), inner.getX(), inner.getBottom(), false);
        g.setGradientFill(inset);
        g.fillRoundedRectangle(inner, 2.5f);

        if (active)
        {
            g.setColour(juce::Colour(0xff40b5df).withAlpha(0.72f));
            g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
        }
        else
        {
            g.setColour(juce::Colour(0xff66757e).withAlpha(0.65f));
            g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
        }

        if (highlighted || down)
        {
            g.setColour(juce::Colours::white.withAlpha(down ? 0.14f : 0.08f));
            g.fillRoundedRectangle(inner, 2.5f);
        }
        g.setColour(juce::Colour(0xffd8e3e9).withAlpha(highlighted || down ? 0.96f : 0.78f));
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(getButtonText(), getLocalBounds().withTrimmedLeft(hasPlugIn ? 10 : 0), juce::Justification::centred, false);

        if (! hasPlugIn || ! isMouseOverOrDragging(true))
            return;

        const auto control = bypassBounds().toFloat();
        const auto controlHovered = bypassBounds().contains(getMouseXYRelative());
        const auto colour = bypassed ? juce::Colour(0xff94a2aa) : juce::Colour(0xff55d7e8);
        g.setColour(juce::Colours::black.withAlpha(controlHovered ? 0.28f : 0.16f));
        g.fillRoundedRectangle(control, 3.0f);
        g.setColour(colour.withAlpha(controlHovered ? 1.0f : 0.76f));
        const auto ring = control.reduced(5.0f);
        g.drawEllipse(ring, 1.15f);
        const auto centreX = ring.getCentreX();
        g.drawLine(centreX, ring.getY() - 0.4f, centreX, ring.getCentreY() - 0.5f, 1.25f);
    }

    juce::Rectangle<int> bypassBounds() const noexcept
    {
        return getLocalBounds().removeFromLeft(18).reduced(1, 1);
    }

    bool active = false;
    bool hasPlugIn = false;
    bool bypassed = false;
    bool bypassPressed = false;
};

// A bare, engraved route label. It deliberately has no button tile so the
// SEND bay remains hardware-like while still exposing a discoverable target
// selector for every aux control.
class SendRouteButton final : public juce::TextButton
{
private:
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat();
        if (highlighted || down)
        {
            g.setColour(juce::Colour(0xff42b6d8).withAlpha(down ? 0.15f : 0.09f));
            g.fillRoundedRectangle(bounds.reduced(1.0f), 2.0f);
        }
        g.setColour(juce::Colour(0xffb9c7ce).withAlpha(isEnabled() ? 0.86f : 0.42f));
        g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
        if (highlighted)
        {
            g.setColour(juce::Colour(0xff42b6d8).withAlpha(0.60f));
            g.drawHorizontalLine(static_cast<float>(getHeight() - 1), 2.0f, static_cast<float>(getWidth() - 2));
        }
    }
};

class MixerPane final : public juce::Component, private juce::Timer
{
public:
    explicit MixerPane(TrackDataModel* model = nullptr, AudioEngine* engine = nullptr,
                       PluginHostService* pluginHost = nullptr);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void refreshFromModel();

    [[nodiscard]] int getPreferredHeight() const noexcept { return dockHeight; }
    std::function<void(int)> onHeightChangeRequested;

private:
    // The channel desk and the master-output surface are separate workflows.
    // Keeping MASTER isolated preserves useful width for five source strips.
    enum class View { mixer, master };

    void timerCallback() override;
    void showPluginBrowser(int trackIndex, size_t slot);
    void showPluginEditor(int trackIndex, size_t slot);
    void openPluginEditorNow(int trackIndex, size_t slot);
    void showMasterPluginBrowser(size_t slot);
    void showMasterPluginEditor(size_t slot);
    void openMasterPluginEditorNow(size_t slot);
    void showBusPluginBrowser(size_t busIndex, size_t slot);
    void showBusPluginEditor(size_t busIndex, size_t slot);
    void openBusPluginEditorNow(size_t busIndex, size_t slot);
    void showSendDestinationMenu(int trackIndex, size_t slot);
    void setView(View newView);
    void syncViewVisibility();
    void requestHeight(int height);
    size_t firstVacantFxSlot(int trackIndex) const noexcept;
    size_t activeFxCount(int trackIndex) const noexcept;

    // Reference console proportions: a concise toolbar above a fixed-density
    // channel strip. Individual controls must never stretch just because the
    // user gives the dock more height.
    static constexpr int toolbarHeight = 46;
    static constexpr int preferredHeight = 456;

    TrackDataModel* trackModel = nullptr;
    AudioEngine* audioEngine = nullptr;
    PluginHostService* pluginHostService = nullptr;
    std::unique_ptr<juce::FileChooser> pluginFileChooser;
    std::vector<std::unique_ptr<juce::DocumentWindow>> pluginEditorWindows;
    std::array<AudioPeakMeter, TrackDataModel::maxTracks> meters;
    std::array<juce::Slider, TrackDataModel::maxTracks> faders;
    std::array<juce::Slider, TrackDataModel::maxTracks> panSliders;
    std::array<std::array<juce::Slider, 4>, TrackDataModel::maxTracks> sendKnobs;
    std::array<std::array<SendRouteButton, 4>, TrackDataModel::maxTracks> sendRouteButtons;
    std::array<juce::Label, TrackDataModel::maxTracks> gainReadouts;
    std::array<juce::TextButton, TrackDataModel::maxTracks> chainBypassButtons;
    std::array<juce::ToggleButton, TrackDataModel::maxTracks> armButtons;
    std::array<juce::ToggleButton, TrackDataModel::maxTracks> monitorButtons;
    std::array<juce::ToggleButton, TrackDataModel::maxTracks> muteButtons;
    std::array<juce::ToggleButton, TrackDataModel::maxTracks> soloButtons;
    std::array<std::array<FxSlotButton, TrackDataModel::maxFxSlots>, TrackDataModel::maxTracks> fxButtons;
    // The processor rack stays fixed-size and realtime safe; this is only a
    // compact Stage presentation of it. A strip expands on demand instead of
    // permanently consuming the mixer with eight empty insert wells.
    std::array<juce::TextButton, TrackDataModel::maxTracks> fxRackButtons;
    std::array<bool, TrackDataModel::maxTracks> fxRacksExpanded {};
    std::array<juce::Label, TrackDataModel::maxTracks> labels;
    std::array<juce::Rectangle<int>, TrackDataModel::maxTracks> trackStripBounds;
    // Aux returns use the same physical language as source channels. They are
    // indexed by the stable render-bus order, while every edit still resolves
    // through BusId so a deleted bus cannot change another return's target.
    std::array<AudioPeakMeter, TrackDataModel::maxBuses> busMeters;
    std::array<juce::Slider, TrackDataModel::maxBuses> busFaders;
    std::array<juce::Label, TrackDataModel::maxBuses> busLabels;
    std::array<juce::Label, TrackDataModel::maxBuses> busReadouts;
    std::array<juce::ToggleButton, TrackDataModel::maxBuses> busMuteButtons;
    std::array<std::array<FxSlotButton, TrackDataModel::maxFxSlots>, TrackDataModel::maxBuses> busFxButtons;
    std::array<juce::Rectangle<int>, TrackDataModel::maxBuses> busStripBounds;
    int displayedBusCount = -1;
    int displayedTrackCount = -1;
    juce::TextButton mixerTab { "MIXER" };
    juce::TextButton masterTab { "MASTER" };
    AudioPeakMeter masterMeter;
    juce::Slider masterFader;
    juce::Label masterReadout;
    juce::Label masterLabel;
    std::array<FxSlotButton, TrackDataModel::maxFxSlots> masterFxButtons;
    juce::TextButton masterChainBypass { "PWR" };
    juce::Slider masterPan, masterWidth;
    juce::ToggleButton masterMono { "MONO" }, masterDim { "DIM" }, masterMute { "MUTE" };
    std::array<juce::TextButton, 6> masterMidiLearnButtons;
    View activeView = View::mixer;
    int horizontalScroll = 0;
    int dockHeight = preferredHeight;
    bool resizingDock = false;
    int dragStartHeight = preferredHeight;
};
