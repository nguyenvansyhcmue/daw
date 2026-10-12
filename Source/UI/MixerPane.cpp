#include "MixerPane.h"

#include <algorithm>

#include "../AudioEngine/AudioEngine.h"
#include "../Plugins/PluginHostService.h"
#include "PluginBrowserPanel.h"
#include "BinaryData.h"

namespace
{
class MixerPluginEditorHost final : public juce::Component
{
public:
    MixerPluginEditorHost(std::shared_ptr<AudioEffectProcessor> ownerIn, std::unique_ptr<juce::Component> editorIn)
        : owner(std::move(ownerIn)), editor(std::move(editorIn))
    {
        if (editor != nullptr) addAndMakeVisible(*editor);
    }
    void resized() override { if (editor != nullptr) editor->setBounds(getLocalBounds()); }
    void childBoundsChanged(juce::Component* child) override
    {
        if (child == editor.get() && (child->getWidth() != getWidth() || child->getHeight() != getHeight()))
            setSize(juce::jmax(1, child->getWidth()), juce::jmax(1, child->getHeight()));
    }
private:
    std::shared_ptr<AudioEffectProcessor> owner;
    std::unique_ptr<juce::Component> editor;
};

class MixerPluginEditorWindow final : public juce::DocumentWindow
{
public:
    using CloseCallback = std::function<void(juce::DocumentWindow*)>;
    MixerPluginEditorWindow(const juce::String& title, juce::Component* content,
                            juce::Component* centre, CloseCallback callbackIn)
        : juce::DocumentWindow(title, juce::Colour(0xff252a2e), juce::DocumentWindow::closeButton, true),
          callback(std::move(callbackIn))
    {
        setUsingNativeTitleBar(true); setContentOwned(content, true);
        setSize(content->getWidth(), content->getHeight()); setResizable(false, false);
        centreAroundComponent(centre, getWidth(), getHeight()); setVisible(true); toFront(true);
    }
    void closeButtonPressed() override
    {
        setVisible(false);
        if (callback != nullptr)
            juce::MessageManager::callAsync([close = callback, window = static_cast<juce::DocumentWindow*>(this)] { close(window); });
    }
private:
    CloseCallback callback;
};

const auto mixerBackground = juce::Colour(0xff171a1d);
const auto mixerToolbar = juce::Colour(0xff202428);
const auto channelFace = juce::Colour(0xff252a2e);
const auto channelRecess = juce::Colour(0xff121619);
const auto mixerBorder = juce::Colour(0xff3b4146);
const auto mixerText = juce::Colour(0xffe0e5e8);
const auto mixerSecondaryText = juce::Colour(0xff9aa4ab);
const auto mixerAccent = juce::Colour(0xff43b6d8);
const auto mixerSafe = juce::Colour(0xff4ab979);
const auto mixerDanger = juce::Colour(0xffc74b4d);
const auto mixerWarning = juce::Colour(0xffe3a83f);

// The engine owns linear gain; the console surface owns a calibrated dB
// travel. Keeping that conversion at the UI boundary makes each engraved dB
// mark coincide with the physical fader cap without changing audio routing.
constexpr double faderMinimumDb = -12.0;
constexpr double faderMaximumDb = 12.0;

double gainToFaderDb(float gain)
{
    if (gain <= juce::Decibels::decibelsToGain(static_cast<float>(faderMinimumDb)))
        return faderMinimumDb;
    return juce::jlimit(faderMinimumDb, faderMaximumDb,
                        static_cast<double>(juce::Decibels::gainToDecibels(gain)));
}

float faderDbToGain(double decibels)
{
    return decibels <= faderMinimumDb ? 0.0f
                                      : juce::Decibels::decibelsToGain(static_cast<float>(decibels));
}

template <typename BypassSetter>
bool toggleFxRackBypass(const TrackDataModel::FxRackSnapshot* rack, BypassSetter&& setBypass)
{
    if (rack == nullptr)
        return false;

    auto shouldBypass = false;
    auto hasProcessor = false;
    for (size_t slot = 0; slot < rack->processors.size(); ++slot)
    {
        if (rack->processors[slot] == nullptr)
            continue;
        hasProcessor = true;
        shouldBypass = shouldBypass || ! rack->bypass[slot];
    }

    if (! hasProcessor)
        return false;

    for (size_t slot = 0; slot < rack->processors.size(); ++slot)
        if (rack->processors[slot] != nullptr)
            setBypass(slot, shouldBypass);

    return true;
}

const juce::Drawable* skinDrawable(const char* data, int dataSize)
{
    static std::vector<std::unique_ptr<juce::Drawable>> cache;
    const auto key = juce::String::toHexString(reinterpret_cast<juce::int64>(data));
    for (const auto& drawable : cache)
        if (drawable != nullptr && drawable->getName() == key)
            return drawable.get();
    auto drawable = juce::Drawable::createFromImageData(data, static_cast<size_t>(dataSize));
    if (drawable == nullptr) return nullptr;
    drawable->setName(key);
    cache.push_back(std::move(drawable));
    return cache.back().get();
}

const juce::Drawable* mixerButtonSkin() { return skinDrawable(BinaryData::analog_button_neutral_svg, BinaryData::analog_button_neutral_svgSize); }
const juce::Drawable* insertSkin() { return skinDrawable(BinaryData::analog_insert_slot_empty_svg, BinaryData::analog_insert_slot_empty_svgSize); }
const juce::Drawable* faceplateSkin() { return skinDrawable(BinaryData::analog_panel_texture_svg, BinaryData::analog_panel_texture_svgSize); }
const juce::Drawable* nameplateSkin() { return skinDrawable(BinaryData::analog_channel_label_well_svg, BinaryData::analog_channel_label_well_svgSize); }
const juce::Drawable* faderCapSkin() { return skinDrawable(BinaryData::analog_fader_cap_svg, BinaryData::analog_fader_cap_svgSize); }
const juce::Drawable* faderRailSkin() { return skinDrawable(BinaryData::fader_rail_svg, BinaryData::fader_rail_svgSize); }
const juce::Drawable* meterOffSkin() { return skinDrawable(BinaryData::analog_meter_well_svg, BinaryData::analog_meter_well_svgSize); }
// SEND controls need their own tactile, low-glare hardware material.  A
// shrunken PAN knob made the four controls read as an undifferentiated row.
const juce::Drawable* smallKnobSkin() { return skinDrawable(BinaryData::analog_send_knob_generated_png, BinaryData::analog_send_knob_generated_pngSize); }
const juce::Drawable* panKnobSkin() { return skinDrawable(BinaryData::analog_knob_pan_svg, BinaryData::analog_knob_pan_svgSize); }
const juce::Drawable* screwSkin() { return skinDrawable(BinaryData::analog_screw_svg, BinaryData::analog_screw_svgSize); }

void drawSkin(juce::Graphics& g, const juce::Drawable* drawable, juce::Rectangle<float> bounds, float opacity = 1.0f)
{
    if (drawable != nullptr) drawable->drawWithin(g, bounds, juce::RectanglePlacement::stretchToFit, opacity);
}

class MixerConsoleLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        if (down) bounds = bounds.translated(0.0f, 1.0f);
        if (button.getButtonText() == "PWR")
        {
            const auto diameter = juce::jmin(bounds.getWidth(), bounds.getHeight()) - 4.0f;
            const auto circle = bounds.withSizeKeepingCentre(diameter, diameter);
            g.setColour(juce::Colour(0xff0d1215));
            g.fillEllipse(circle);
            g.setColour(highlighted ? mixerAccent.withAlpha(0.9f) : mixerBorder.withAlpha(0.95f));
            g.drawEllipse(circle, highlighted ? 1.5f : 1.0f);
            const auto glyph = circle.reduced(diameter * 0.27f);
            g.setColour(mixerText.withAlpha(button.isEnabled() ? 0.86f : 0.30f));
            g.drawEllipse(glyph, 1.25f);
            g.drawLine(glyph.getCentreX(), glyph.getY() - 1.0f, glyph.getCentreX(), glyph.getCentreY(), 1.25f);
            return;
        }
        drawSkin(g, mixerButtonSkin(), bounds, button.isEnabled() ? 1.0f : 0.42f);
        if (highlighted)
        {
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.drawRoundedRectangle(bounds.reduced(3.0f), 4.0f, 1.0f);
        }
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool highlighted, bool down) override
    {
        if (button.getButtonText() == "PWR")
            return;
        juce::LookAndFeel_V4::drawButtonText(g, button, highlighted, down);
    }
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down) override
    {
        drawButtonBackground(g, button, juce::Colours::transparentBlack, highlighted, down);
        const auto text = button.getButtonText();
        auto colour = mixerText.withAlpha(button.isEnabled() ? 0.90f : 0.35f);
        if (button.getToggleState())
        {
            const auto semanticColour = text == "R" ? mixerDanger
                                      : text == "I" ? mixerSafe
                                      : text == "M" ? mixerWarning : mixerAccent;
            const auto bounds = button.getLocalBounds().toFloat().reduced(4.0f);
            // Console controls keep their material dark. Colour is a state
            // cue, not a large block of paint competing with the channel.
            g.setColour(semanticColour.withAlpha(0.22f));
            g.fillRoundedRectangle(bounds, 3.5f);
            g.setColour(semanticColour.withAlpha(0.95f));
            g.drawRoundedRectangle(bounds, 3.5f, 1.0f);
            colour = semanticColour.brighter(0.45f).withAlpha(0.96f);
        }
        g.setColour(colour); g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold))); g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
    }
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float, float, float, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearBarVertical) { juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, 0.0f, 0.0f, 0.0f, style, slider); return; }
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
        const auto well = juce::Rectangle<float>(bounds.getCentreX() - 14.0f, bounds.getY() + 2.0f, 28.0f, bounds.getHeight() - 4.0f);
        g.setColour(juce::Colour(0xff080d10));
        g.fillRoundedRectangle(well, 6.0f);
        g.setColour(juce::Colour(0xff65747d).withAlpha(0.52f));
        g.drawRoundedRectangle(well, 6.0f, 1.0f);
        g.setColour(juce::Colours::black.withAlpha(0.72f));
        g.drawLine(well.getX() + 3.0f, well.getY() + 5.0f, well.getX() + 3.0f, well.getBottom() - 5.0f, 1.0f);
        g.drawLine(well.getRight() - 3.0f, well.getY() + 5.0f, well.getRight() - 3.0f, well.getBottom() - 5.0f, 1.0f);
        const auto rail = juce::Rectangle<float>(bounds.getCentreX() - 10.0f, bounds.getY() + 5.0f, 20.0f, bounds.getHeight() - 10.0f);
        drawSkin(g, faderRailSkin(), rail);
        const auto proportion = static_cast<float>(slider.valueToProportionOfLength(slider.getValue()));
        // The reference uses a narrow, short cap. Scaling it from the whole
        // strip made it look like a large white block on tall docks.
        const auto capWidth = juce::jlimit(26.0f, 34.0f, bounds.getWidth() * 0.48f);
        const auto capHeight = capWidth * 1.42f;
        const auto capY = rail.getY() + (1.0f - proportion) * rail.getHeight() - capHeight * 0.5f;
        drawSkin(g, faderCapSkin(), { bounds.getCentreX() - capWidth * 0.5f, capY, capWidth, capHeight });
    }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPosition, float, float, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
        const auto size = juce::jmin(bounds.getWidth(), bounds.getHeight()); const auto square = bounds.withSizeKeepingCentre(size, size);
        auto* knob = size >= 38.0f ? panKnobSkin() : smallKnobSkin(); if (knob == nullptr) return;
        const auto accentString = slider.getProperties().getWithDefault("consoleAccent", "").toString();
        const auto accent = accentString.isNotEmpty() ? juce::Colour::fromString(accentString) : mixerAccent;
        const auto ringColour = slider.isEnabled() ? accent.withAlpha(0.55f) : mixerSecondaryText.withAlpha(0.16f);
        g.setColour(juce::Colours::black.withAlpha(size > 38.0f ? 0.74f : 0.46f));
        g.fillEllipse(square.expanded(size > 38.0f ? 1.2f : 0.4f));
        // Send colours remain a subtle state halo instead of coloured plastic.
        g.setColour(ringColour.withAlpha(size > 38.0f ? ringColour.getFloatAlpha()
                                                    : ringColour.getFloatAlpha() * 0.55f));
        g.drawEllipse(square.reduced(size > 38.0f ? 1.0f : 0.35f), size > 38.0f ? 1.6f : 0.8f);
        if (size >= 38.0f)
        {
            g.setColour(mixerSecondaryText.withAlpha(0.42f));
            for (int tick = 0; tick <= 10; ++tick)
            {
                const auto angle = juce::degreesToRadians(-135.0f + 27.0f * static_cast<float>(tick));
                const auto centre = square.getCentre();
                const auto outer = centre.getPointOnCircumference(size * 0.47f, angle);
                const auto inner = centre.getPointOnCircumference(size * (tick % 5 == 0 ? 0.40f : 0.43f), angle);
                g.drawLine(inner.x, inner.y, outer.x, outer.y, tick % 5 == 0 ? 1.1f : 0.7f);
            }
        }
        juce::Graphics::ScopedSaveState state(g); g.addTransform(juce::AffineTransform::rotation((sliderPosition - 0.5f) * juce::MathConstants<float>::pi * 1.35f, square.getCentreX(), square.getCentreY())); drawSkin(g, knob, square);
    }
};

MixerConsoleLookAndFeel& mixerConsoleLookAndFeel() { static MixerConsoleLookAndFeel instance; return instance; }

void styleToolbarButton(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff171c20));
    button.setColour(juce::TextButton::buttonOnColourId, mixerAccent.darker(0.35f));
    button.setColour(juce::TextButton::textColourOffId, mixerText.withAlpha(0.78f));
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

void styleChannelButton(juce::ToggleButton& button, juce::Colour active)
{
    button.setColour(juce::ToggleButton::textColourId, mixerText.withAlpha(0.92f));
    button.setColour(juce::ToggleButton::tickColourId, active);
}

juce::Colour trackAccent(const TrackDataModel::TrackState& track, int index)
{
    juce::ignoreUnused(track, index);
    // TrackState currently has no persisted colour field. Never fabricate one
    // from the strip index: all chrome remains neutral until the model owns a
    // real track tint.
    return juce::Colour(0xff59717e);
}
}

MixerPane::MixerPane(TrackDataModel* model, AudioEngine* engine, PluginHostService* pluginHost)
    : trackModel(model), audioEngine(engine), pluginHostService(pluginHost)
{
    startTimerHz(30);
    for (auto* button : { &mixerTab, &masterTab })
    {
        styleToolbarButton(*button);
        button->setLookAndFeel(&mixerConsoleLookAndFeel());
        addAndMakeVisible(*button);
    }
    mixerTab.onClick = [this] { setView(View::mixer); };
    masterTab.onClick = [this] { setView(View::master); };

    masterLabel.setText("MASTER", juce::dontSendNotification);
    masterLabel.setJustificationType(juce::Justification::centred);
    masterLabel.setColour(juce::Label::textColourId, mixerText);
    masterLabel.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    addAndMakeVisible(masterLabel);
    masterReadout.setJustificationType(juce::Justification::centred);
    masterReadout.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    masterReadout.setColour(juce::Label::backgroundColourId, channelRecess);
    masterReadout.setColour(juce::Label::textColourId, mixerText);
    addAndMakeVisible(masterReadout);
    addAndMakeVisible(masterMeter);
    masterFader.setRange(faderMinimumDb, faderMaximumDb, 0.1);
    masterFader.setLookAndFeel(&mixerConsoleLookAndFeel());
    masterFader.setSliderStyle(juce::Slider::LinearVertical);
    masterFader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    masterFader.setDoubleClickReturnValue(true, 0.0);
    masterFader.onValueChange = [this] { if (audioEngine != nullptr) audioEngine->setMasterGain(faderDbToGain(masterFader.getValue())); };
    addAndMakeVisible(masterFader);

    masterChainBypass.setButtonText("PWR");
    masterChainBypass.setTooltip("Bypass all Master Inserts");
    styleToolbarButton(masterChainBypass); masterChainBypass.setLookAndFeel(&mixerConsoleLookAndFeel());
    masterChainBypass.onClick = [this]
    {
        if (audioEngine == nullptr) return;
        toggleFxRackBypass(audioEngine->getMasterFxRackSnapshot(),
                           [this] (size_t slot, bool shouldBypass)
                           { audioEngine->setMasterFxBypassed(slot, shouldBypass); });
    };
    addAndMakeVisible(masterChainBypass);
    for (auto* button : { &masterMono, &masterDim, &masterMute })
    {
        styleChannelButton(*button, mixerAccent); button->setLookAndFeel(&mixerConsoleLookAndFeel()); addAndMakeVisible(*button);
    }
    masterMono.onClick = [this] { if (audioEngine != nullptr) audioEngine->setMasterMono(masterMono.getToggleState()); };
    masterDim.onClick = [this] { if (audioEngine != nullptr) audioEngine->setMasterDim(masterDim.getToggleState()); };
    masterMute.onClick = [this] { if (audioEngine != nullptr) audioEngine->setMasterMuted(masterMute.getToggleState()); };
    for (auto* knob : { &masterPan, &masterWidth })
    {
        knob->setLookAndFeel(&mixerConsoleLookAndFeel()); knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); knob->setDoubleClickReturnValue(true, 0.0); addAndMakeVisible(*knob);
    }
    masterPan.setRange(-1.0, 1.0, 0.01); masterPan.onValueChange = [this] { if (audioEngine != nullptr) audioEngine->setMasterPan(static_cast<float>(masterPan.getValue())); };
    masterWidth.setRange(0.0, 2.0, 0.01); masterWidth.setValue(1.0); masterWidth.onValueChange = [this] { if (audioEngine != nullptr) audioEngine->setMasterWidth(static_cast<float>(masterWidth.getValue())); };
    for (size_t index = 0; index < masterMidiLearnButtons.size(); ++index)
    {
        auto& button = masterMidiLearnButtons[index];
        button.setButtonText("LEARN"); button.setTooltip("Assign the next incoming MIDI CC to this Master control");
        styleToolbarButton(button); button.setLookAndFeel(&mixerConsoleLookAndFeel()); addAndMakeVisible(button);
        button.onClick = [this, index]
        {
            if (audioEngine != nullptr)
                audioEngine->beginMasterMidiLearn(static_cast<AudioEngine::MasterMidiTarget>(index));
        };
    }
    for (size_t slot = 0; slot < masterFxButtons.size(); ++slot)
    {
        auto& button = masterFxButtons[slot];
        button.setButtonText("+"); button.setTooltip("Add or manage Master Insert plug-in");
        button.onClick = [this, slot]
        {
            if (audioEngine != nullptr)
            {
                const auto* rack = audioEngine->getMasterFxRackSnapshot();
                if (rack != nullptr && rack->processors[slot] != nullptr && rack->processors[slot]->hasEditor())
                {
                    showMasterPluginEditor(slot);
                    return;
                }
            }
            showMasterPluginBrowser(slot);
        };
        button.onRightClick = [this, slot]
        {
            if (audioEngine == nullptr) return;
            const auto* rack = audioEngine->getMasterFxRackSnapshot();
            if (rack == nullptr || slot >= rack->processors.size() || rack->processors[slot] == nullptr) return;
            const auto bypassed = rack->bypass[slot];
            juce::PopupMenu menu;
            menu.addItem(1, bypassed ? "Enable plug-in" : "Bypass plug-in");
            menu.addItem(3, "Open Plug-in Editor", rack->processors[slot]->hasEditor());
            menu.addItem(2, "Remove plug-in");
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&masterFxButtons[slot]),
                [safeThis = juce::Component::SafePointer<MixerPane>(this), slot] (int result)
                {
                    if (safeThis == nullptr || safeThis->audioEngine == nullptr) return;
                    if (result == 1)
                    {
                        const auto* current = safeThis->audioEngine->getMasterFxRackSnapshot();
                        if (current != nullptr && slot < current->bypass.size())
                            safeThis->audioEngine->setMasterFxBypassed(slot, ! current->bypass[slot]);
                    }
                    else if (result == 2)
                        safeThis->audioEngine->setMasterFxProcessor(slot, nullptr);
                    else if (result == 3)
                        safeThis->showMasterPluginEditor(slot);
                    safeThis->refreshFromModel();
                });
        };
        addAndMakeVisible(button);
    }

    for (int i = 0; i < static_cast<int>(TrackDataModel::maxTracks); ++i)
    {
        labels[i].setJustificationType(juce::Justification::centred);
        labels[i].setColour(juce::Label::textColourId, mixerText);
        labels[i].setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        addAndMakeVisible(labels[i]);
        addAndMakeVisible(meters[i]);
        gainReadouts[i].setJustificationType(juce::Justification::centred);
        gainReadouts[i].setColour(juce::Label::backgroundColourId, channelRecess);
        gainReadouts[i].setColour(juce::Label::textColourId, mixerText);
        gainReadouts[i].setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        addAndMakeVisible(gainReadouts[i]);
        chainBypassButtons[i].setButtonText("PWR");
        chainBypassButtons[i].setTooltip("Bypass insert chain (unavailable while empty)");
        styleToolbarButton(chainBypassButtons[i]);
        chainBypassButtons[i].setLookAndFeel(&mixerConsoleLookAndFeel());
        chainBypassButtons[i].onClick = [this, i]
        {
            if (audioEngine == nullptr || trackModel == nullptr) return;
            toggleFxRackBypass(trackModel->getFxRackSnapshot(static_cast<size_t>(i)),
                               [this, i] (size_t slot, bool shouldBypass)
                               { audioEngine->setFxBypassed(static_cast<size_t>(i), slot, shouldBypass); });
        };
        addAndMakeVisible(chainBypassButtons[i]);

        auto& rackButton = fxRackButtons[static_cast<size_t>(i)];
        rackButton.setLookAndFeel(&mixerConsoleLookAndFeel());
        styleToolbarButton(rackButton);
        rackButton.setTooltip("Insert rack. Click to add the first plug-in or expand this channel's insert list.");
        rackButton.onClick = [this, i]
        {
            if (trackModel == nullptr || static_cast<size_t>(i) >= trackModel->getTrackCount()) return;
            if (activeFxCount(i) == 0)
            {
                const auto slot = firstVacantFxSlot(i);
                if (slot < TrackDataModel::maxFxSlots) showPluginBrowser(i, slot);
                return;
            }
            fxRacksExpanded[static_cast<size_t>(i)] = ! fxRacksExpanded[static_cast<size_t>(i)];
            requestHeight(fxRacksExpanded[static_cast<size_t>(i)] ? 620 : preferredHeight);
            resized();
        };
        addAndMakeVisible(rackButton);

        const auto configureTrackControl = [this, i] (juce::ToggleButton& control, const juce::String& text, juce::Colour colour, auto callback)
        {
            control.setButtonText(text); styleChannelButton(control, colour); control.setLookAndFeel(&mixerConsoleLookAndFeel()); control.onClick = callback; addAndMakeVisible(control);
        };
        configureTrackControl(armButtons[i], "R", mixerDanger, [this, i] { if (trackModel != nullptr) trackModel->setTrackArmed(static_cast<size_t>(i), armButtons[i].getToggleState()); });
        configureTrackControl(monitorButtons[i], "I", mixerSafe, [this, i]
        {
            if (trackModel == nullptr)
                return;

            const auto trackIndex = static_cast<size_t>(i);
            const auto shouldEnable = monitorButtons[i].getToggleState();
            trackModel->setTrackInputMonitoring(trackIndex, shouldEnable);
            if (! shouldEnable)
            {
                if (audioEngine != nullptr)
                    audioEngine->disableTrackMonitor(trackIndex);
                return;
            }

            // The console's I button is an audible live-monitor request, not
            // just a record-state toggle. The engine validates speaker risk
            // and grants the Master route before inserts are processed.
            if (audioEngine != nullptr)
                if (const auto result = audioEngine->enableTrackMonitor(trackIndex, MonitorDestination::master); result.failed())
                {
                    trackModel->setTrackInputMonitoring(trackIndex, false);
                    monitorButtons[i].setToggleState(false, juce::dontSendNotification);
                }
        });
        configureTrackControl(muteButtons[i], "M", mixerWarning, [this, i] { if (trackModel != nullptr) trackModel->setTrackMuted(static_cast<size_t>(i), muteButtons[i].getToggleState()); });
        configureTrackControl(soloButtons[i], "S", mixerAccent, [this, i] { if (trackModel != nullptr) trackModel->setTrackSolo(static_cast<size_t>(i), soloButtons[i].getToggleState()); });
        armButtons[i].setTooltip("Record arm"); monitorButtons[i].setTooltip("Input monitor request — uses configured cue/monitor destination"); muteButtons[i].setTooltip("Mute"); soloButtons[i].setTooltip("Solo");

        for (size_t slot = 0; slot < TrackDataModel::maxFxSlots; ++slot)
        {
            auto& button = fxButtons[static_cast<size_t>(i)][slot];
            button.setButtonText("+"); button.setTooltip("Add plug-in. Left-click an active slot to open its editor; right-click to bypass or remove it.");
            button.onClick = [this, i, slot]
            {
                if (trackModel == nullptr || static_cast<size_t>(i) >= trackModel->getTrackCount()) return;
                const auto* rack = trackModel->getFxRackSnapshot(static_cast<size_t>(i));
                const auto processor = rack != nullptr && slot < rack->processors.size() ? rack->processors[slot] : nullptr;
                if (processor != nullptr && processor->hasEditor())
                    showPluginEditor(i, slot);
                else if (processor == nullptr)
                    showPluginBrowser(i, slot);
            };
            button.onRightClick = [this, i, slot]
            {
                if (audioEngine == nullptr || trackModel == nullptr
                    || static_cast<size_t>(i) >= trackModel->getTrackCount()) return;
                const auto* rack = trackModel->getFxRackSnapshot(static_cast<size_t>(i));
                if (rack == nullptr || slot >= rack->processors.size() || rack->processors[slot] == nullptr) return;
                const auto bypassed = rack->bypass[slot];
                juce::PopupMenu menu;
                menu.addItem(1, bypassed ? "Enable plug-in" : "Bypass plug-in");
                menu.addItem(3, "Open Plug-in Editor", rack->processors[slot]->hasEditor());
                menu.addItem(2, "Remove plug-in");
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&fxButtons[static_cast<size_t>(i)][slot]),
                    [safeThis = juce::Component::SafePointer<MixerPane>(this), i, slot] (int result)
                    {
                        if (safeThis == nullptr || safeThis->audioEngine == nullptr) return;
                        if (result == 1)
                        {
                            const auto* current = safeThis->trackModel != nullptr
                                ? safeThis->trackModel->getFxRackSnapshot(static_cast<size_t>(i)) : nullptr;
                            if (current != nullptr && slot < current->bypass.size())
                                safeThis->audioEngine->setFxBypassed(static_cast<size_t>(i), slot, ! current->bypass[slot]);
                        }
                    else if (result == 2)
                        safeThis->audioEngine->setFxProcessor(static_cast<size_t>(i), slot, nullptr);
                    else if (result == 3)
                        safeThis->showPluginEditor(i, slot);
                    safeThis->refreshFromModel();
                    });
            };
            addAndMakeVisible(button);
        }
        faders[i].setRange(faderMinimumDb, faderMaximumDb, 0.1);
        faders[i].setLookAndFeel(&mixerConsoleLookAndFeel());
        faders[i].setSliderStyle(juce::Slider::LinearVertical);
        faders[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        faders[i].setDoubleClickReturnValue(true, 0.0);
        faders[i].onValueChange = [this, i] { if (trackModel != nullptr) trackModel->setTrackVolume(static_cast<size_t>(i), faderDbToGain(faders[i].getValue())); };
        addAndMakeVisible(faders[i]);
        panSliders[i].setRange(-1.0, 1.0, 0.01);
        panSliders[i].getProperties().set("consoleAccent", trackAccent(trackModel != nullptr && static_cast<size_t>(i) < trackModel->getTrackCount() ? trackModel->getTrack(static_cast<size_t>(i)) : TrackDataModel::TrackState {}, i).toDisplayString(true));
        panSliders[i].setLookAndFeel(&mixerConsoleLookAndFeel());
        panSliders[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        panSliders[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        panSliders[i].setDoubleClickReturnValue(true, 0.0);
        panSliders[i].onValueChange = [this, i] { if (trackModel != nullptr) trackModel->setTrackPan(static_cast<size_t>(i), static_cast<float>(panSliders[i].getValue())); };
        addAndMakeVisible(panSliders[i]);
        for (size_t slot = 0; slot < sendKnobs[i].size(); ++slot)
        {
            auto& knob = sendKnobs[i][slot];
            auto& routeButton = sendRouteButtons[i][slot];
            knob.setRange(0.0, TrackDataModel::maximumSendGain, 0.01);
            static constexpr std::array<juce::uint32, 4> sendColours { 0xff269fe8, 0xffc354df, 0xff45ce82, 0xffe0c537 };
            knob.getProperties().set("consoleAccent", juce::Colour(sendColours[slot]).toDisplayString(true));
            knob.setLookAndFeel(&mixerConsoleLookAndFeel());
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            knob.setDoubleClickReturnValue(true, 0.0);
            knob.setTooltip("AUX " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + static_cast<int>(slot)))
                            + " send — drag to create/adjust the route. AUX returns are summed into Master Output.");
            knob.onValueChange = [this, i, slot]
            {
                if (trackModel == nullptr || static_cast<size_t>(i) >= trackModel->getTrackCount()) return;
                const auto& track = trackModel->getTrack(static_cast<size_t>(i));
                auto targetBus = track.sends[slot].targetBus;
                // SEND A-D are real aux routes. On their first adjustment we
                // provision the matching engine bus, then persist the route;
                // the control must never look interactive while being inert.
                if (! targetBus.isValid())
                {
                    while (trackModel->getBuses().size() <= slot)
                    {
                        if (! trackModel->addBus().isValid()) return;
                    }
                    targetBus = trackModel->getBuses()[slot].id;
                }
                trackModel->setTrackSendRoute(track.id, slot, targetBus,
                                              static_cast<float>(sendKnobs[i][slot].getValue()),
                                              track.sends[slot].preFader);
            };
            addAndMakeVisible(knob);
            routeButton.setTooltip("Choose the real AUX bus destination for this send");
            routeButton.onClick = [this, i, slot] { showSendDestinationMenu(i, slot); };
            addAndMakeVisible(routeButton);
        }
    }

    setView(View::mixer);
    refreshFromModel();
}

void MixerPane::setView(View newView)
{
    activeView = newView;
    mixerTab.setToggleState(activeView == View::mixer, juce::dontSendNotification);
    masterTab.setToggleState(activeView == View::master, juce::dontSendNotification);
    syncViewVisibility();
    resized();
    repaint();
}

void MixerPane::syncViewVisibility()
{
    const auto tracksVisible = activeView == View::mixer;
    const auto masterVisible = activeView == View::master;
    masterLabel.setVisible(masterVisible); masterReadout.setVisible(masterVisible); masterMeter.setVisible(false); masterFader.setVisible(masterVisible);
    masterChainBypass.setVisible(masterVisible);
    for (auto* button : { &masterMono, &masterDim, &masterMute }) button->setVisible(masterVisible);
    masterPan.setVisible(masterVisible); masterWidth.setVisible(masterVisible);
    for (auto& button : masterMidiLearnButtons) button.setVisible(masterVisible);
    for (auto& button : masterFxButtons) button.setVisible(masterVisible);
    for (size_t i = 0; i < labels.size(); ++i)
    {
        const auto visible = tracksVisible && static_cast<int>(i) < displayedTrackCount;
        labels[i].setVisible(visible); meters[i].setVisible(visible); faders[i].setVisible(visible); panSliders[i].setVisible(visible); gainReadouts[i].setVisible(visible); chainBypassButtons[i].setVisible(visible); armButtons[i].setVisible(visible); monitorButtons[i].setVisible(visible); muteButtons[i].setVisible(visible); soloButtons[i].setVisible(visible); fxRackButtons[i].setVisible(visible);
        for (auto& button : fxButtons[i]) button.setVisible(visible && fxRacksExpanded[i] && ! button.getBounds().isEmpty());
        for (auto& knob : sendKnobs[i]) knob.setVisible(visible);
        for (auto& button : sendRouteButtons[i]) button.setVisible(visible);
    }
}

void MixerPane::showPluginBrowser(int trackIndex, size_t slot)
{
    if (pluginHostService == nullptr || audioEngine == nullptr || trackIndex < 0) return;
    auto* panel = new PluginBrowserPanel(*pluginHostService, [safeThis = juce::Component::SafePointer<MixerPane>(this), trackIndex, slot] (const juce::PluginDescription& description)
    {
        if (safeThis == nullptr || safeThis->audioEngine == nullptr || safeThis->pluginHostService == nullptr) return;
        juce::String error;
        auto* device = safeThis->audioEngine->getAudioDeviceManager().getCurrentAudioDevice();
        const auto sampleRate = device != nullptr ? device->getCurrentSampleRate() : (safeThis->trackModel != nullptr ? safeThis->trackModel->getSampleRate() : 44100.0);
        const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
        if (auto effect = safeThis->pluginHostService->createEffect(description, sampleRate, blockSize, error))
        {
            const auto shouldOpenEditor = effect->hasEditor();
            safeThis->audioEngine->setFxProcessor(static_cast<size_t>(trackIndex), slot, std::move(effect));
            if (shouldOpenEditor)
                juce::MessageManager::callAsync([safeThis, trackIndex, slot]
                {
                    if (safeThis != nullptr)
                        safeThis->showPluginEditor(trackIndex, slot);
                });
        }
    });
    panel->setSize(PluginBrowserPanel::preferredWidth, PluginBrowserPanel::preferredHeight);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(panel); options.dialogTitle = "Plug-in Browser"; options.dialogBackgroundColour = channelFace; options.escapeKeyTriggersCloseButton = true; options.useNativeTitleBar = false; options.resizable = true; options.componentToCentreAround = this; options.launchAsync();
}

void MixerPane::showPluginEditor(int trackIndex, size_t slot)
{
    const juce::Component::SafePointer<MixerPane> safeThis(this);
    juce::MessageManager::callAsync([safeThis, trackIndex, slot]
    {
        if (safeThis != nullptr) safeThis->openPluginEditorNow(trackIndex, slot);
    });
}

void MixerPane::openPluginEditorNow(int trackIndex, size_t slot)
{
    if (audioEngine == nullptr || trackModel == nullptr || trackIndex < 0
        || static_cast<size_t>(trackIndex) >= trackModel->getTrackCount())
        return;

    const auto* rack = trackModel->getFxRackSnapshot(static_cast<size_t>(trackIndex));
    if (rack == nullptr || slot >= rack->processors.size() || rack->processors[slot] == nullptr)
        return;
    const auto processor = rack->processors[slot];
    if (! processor->hasEditor())
        return;
    // Opening an editor is the exact transition being investigated here. The
    // capture is rate-limited and lock-free on the audio callback.
    audioEngine->startAudioTrace();
    audioEngine->markAudioTraceIssue();
    auto editor = processor->createEditor();
    if (editor == nullptr)
        return;

    const auto editorBounds = editor->getBounds();
    auto* host = new MixerPluginEditorHost(processor, std::move(editor));
    host->setSize(juce::jmax(320, editorBounds.getWidth()), juce::jmax(220, editorBounds.getHeight()));
    const juce::Component::SafePointer<MixerPane> safeOwner(this);
    auto closeWindow = [safeOwner](juce::DocumentWindow* window)
    {
        if (safeOwner == nullptr) return;
        auto& windows = safeOwner->pluginEditorWindows;
        windows.erase(std::remove_if(windows.begin(), windows.end(), [window](const auto& owned)
        { return owned.get() == window; }), windows.end());
    };
    pluginEditorWindows.push_back(std::make_unique<MixerPluginEditorWindow>(processor->getName(), host, this,
                                                                              std::move(closeWindow)));
}

void MixerPane::showMasterPluginBrowser(size_t slot)
{
    if (pluginHostService == nullptr || audioEngine == nullptr || slot >= TrackDataModel::maxFxSlots) return;
    auto* panel = new PluginBrowserPanel(*pluginHostService, [safeThis = juce::Component::SafePointer<MixerPane>(this), slot] (const juce::PluginDescription& description)
    {
        if (safeThis == nullptr || safeThis->audioEngine == nullptr || safeThis->pluginHostService == nullptr) return;
        juce::String error;
        auto* device = safeThis->audioEngine->getAudioDeviceManager().getCurrentAudioDevice();
        const auto sampleRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;
        const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;
        if (auto effect = safeThis->pluginHostService->createEffect(description, sampleRate, blockSize, error))
            safeThis->audioEngine->setMasterFxProcessor(slot, std::move(effect));
    });
    panel->setSize(PluginBrowserPanel::preferredWidth, PluginBrowserPanel::preferredHeight);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(panel); options.dialogTitle = "Master Insert Plug-in Browser";
    options.dialogBackgroundColour = channelFace; options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true; options.resizable = true; options.componentToCentreAround = this; options.launchAsync();
}

void MixerPane::showMasterPluginEditor(size_t slot)
{
    const juce::Component::SafePointer<MixerPane> safeThis(this);
    juce::MessageManager::callAsync([safeThis, slot]
    {
        if (safeThis != nullptr) safeThis->openMasterPluginEditorNow(slot);
    });
}

void MixerPane::openMasterPluginEditorNow(size_t slot)
{
    if (audioEngine == nullptr) return;
    const auto* rack = audioEngine->getMasterFxRackSnapshot();
    if (rack == nullptr || slot >= rack->processors.size() || rack->processors[slot] == nullptr) return;
    const auto processor = rack->processors[slot];
    if (! processor->hasEditor()) return;
    audioEngine->startAudioTrace();
    audioEngine->markAudioTraceIssue();
    auto editor = processor->createEditor();
    if (editor == nullptr) return;
    const auto editorBounds = editor->getBounds();
    auto* host = new MixerPluginEditorHost(processor, std::move(editor));
    host->setSize(juce::jmax(320, editorBounds.getWidth()), juce::jmax(220, editorBounds.getHeight()));
    const juce::Component::SafePointer<MixerPane> safeOwner(this);
    auto closeWindow = [safeOwner](juce::DocumentWindow* window)
    {
        if (safeOwner == nullptr) return;
        auto& windows = safeOwner->pluginEditorWindows;
        windows.erase(std::remove_if(windows.begin(), windows.end(), [window](const auto& owned)
        { return owned.get() == window; }), windows.end());
    };
    pluginEditorWindows.push_back(std::make_unique<MixerPluginEditorWindow>(processor->getName(), host, this,
                                                                              std::move(closeWindow)));
}

void MixerPane::showSendDestinationMenu(int trackIndex, size_t slot)
{
    if (trackModel == nullptr || trackIndex < 0 || static_cast<size_t>(trackIndex) >= trackModel->getTrackCount()
        || slot >= sendRouteButtons[static_cast<size_t>(trackIndex)].size()) return;

    const auto& track = trackModel->getTrack(static_cast<size_t>(trackIndex));
    const auto currentBus = track.sends[slot].targetBus;
    juce::PopupMenu menu;
    menu.addSectionHeader("SEND " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + static_cast<int>(slot))) + " DESTINATION");
    menu.addItem(1, "Create AUX " + juce::String(static_cast<int>(slot + 1)) + " → Master Output");
    const auto& buses = trackModel->getBuses();
    for (size_t busIndex = 0; busIndex < buses.size(); ++busIndex)
        menu.addItem(static_cast<int>(10 + busIndex), "AUX " + juce::String(static_cast<int>(busIndex + 1)) + " → Master Output",
                     true, buses[busIndex].id == currentBus);
    if (currentBus.isValid())
    {
        menu.addSeparator();
        menu.addItem(2, "Disconnect send");
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&sendRouteButtons[static_cast<size_t>(trackIndex)][slot]),
        [safeThis = juce::Component::SafePointer<MixerPane>(this), trackIndex, slot] (int result)
    {
        if (safeThis == nullptr || safeThis->trackModel == nullptr || result == 0) return;
        const auto count = safeThis->trackModel->getTrackCount();
        if (trackIndex < 0 || static_cast<size_t>(trackIndex) >= count) return;
        const auto& selectedTrack = safeThis->trackModel->getTrack(static_cast<size_t>(trackIndex));
        if (result == 2)
        {
            safeThis->trackModel->clearTrackSendRoute(selectedTrack.id, slot);
        }
        else
        {
            BusId target;
            if (result == 1)
                target = safeThis->trackModel->addBus();
            else
            {
                const auto busIndex = static_cast<size_t>(result - 10);
                const auto& buses = safeThis->trackModel->getBuses();
                if (busIndex < buses.size()) target = buses[busIndex].id;
            }
            if (target.isValid())
                safeThis->trackModel->setTrackSendRoute(selectedTrack.id, slot, target,
                    static_cast<float>(safeThis->sendKnobs[static_cast<size_t>(trackIndex)][slot].getValue()),
                    selectedTrack.sends[slot].preFader);
        }
        safeThis->refreshFromModel();
    });
}

void MixerPane::refreshFromModel()
{
    const auto count = trackModel != nullptr ? trackModel->getTrackCount() : 0;
    const auto layoutChanged = displayedTrackCount != static_cast<int>(count);
    displayedTrackCount = static_cast<int>(count);
    for (size_t index = 0; index < count; ++index)
    {
        const auto& track = trackModel->getTrack(index); const auto gain = track.volume.load(std::memory_order_relaxed);
        labels[index].setText(track.name.isNotEmpty() ? track.name : "Channel " + juce::String(static_cast<int>(index + 1)), juce::dontSendNotification);
        faders[index].setValue(gainToFaderDb(gain), juce::dontSendNotification);
        gainReadouts[index].setText(gain <= 0.0001f ? "-∞" : juce::String(juce::Decibels::gainToDecibels(gain), 1), juce::dontSendNotification);
        panSliders[index].setValue(track.pan.load(std::memory_order_relaxed), juce::dontSendNotification);
        armButtons[index].setToggleState(track.armed.load(std::memory_order_relaxed), juce::dontSendNotification); monitorButtons[index].setToggleState(track.inputMonitoring.load(std::memory_order_relaxed), juce::dontSendNotification); muteButtons[index].setToggleState(track.muted.load(std::memory_order_relaxed), juce::dontSendNotification); soloButtons[index].setToggleState(track.solo.load(std::memory_order_relaxed), juce::dontSendNotification);
        const auto rack = trackModel->getFxRackSnapshot(index); auto hasProcessor = false;
        for (size_t slot = 0; slot < fxButtons[index].size(); ++slot)
        {
            const auto processor = rack != nullptr ? rack->processors[slot] : nullptr;
            const auto bypassed = processor != nullptr && rack->bypass[slot];
            fxButtons[index][slot].setButtonText(processor != nullptr
                                                     ? (bypassed ? "OFF  " : "") + processor->getName().substring(0, 18)
                                                     : "+");
            fxButtons[index][slot].setTooltip(processor != nullptr
                                                  ? (bypassed ? "Plug-in bypassed. Right-click to enable or remove it."
                                                              : "Plug-in active. Right-click to bypass or remove it.")
                                                  : "Add plug-in. Left-click to insert a plug-in.");
            fxButtons[index][slot].setActive(processor != nullptr && ! bypassed);
            hasProcessor = hasProcessor || processor != nullptr;
        }
        const auto count = activeFxCount(static_cast<int>(index));
        fxRackButtons[index].setButtonText(count == 0 ? "+ INSERT" : "FX " + juce::String(static_cast<int>(count))
            + (fxRacksExpanded[index] ? "  \u25be" : "  \u25b8"));
        chainBypassButtons[index].setEnabled(hasProcessor);
        for (size_t slot = 0; slot < sendKnobs[index].size(); ++slot)
        {
            const auto& send = track.sends[slot];
            auto busNumber = 0;
            if (send.targetBus.isValid())
            {
                const auto& buses = trackModel->getBuses();
                const auto found = std::find_if(buses.begin(), buses.end(), [&send] (const auto& bus) { return bus.id == send.targetBus; });
                if (found != buses.end()) busNumber = static_cast<int>(std::distance(buses.begin(), found)) + 1;
            }
            sendRouteButtons[index][slot].setButtonText(busNumber > 0 ? "AUX " + juce::String(busNumber) : "ASSIGN");
            sendKnobs[index][slot].setEnabled(true);
            sendKnobs[index][slot].setValue(send.targetBus.isValid() ? send.level : 0.0f, juce::dontSendNotification);
        }
    }
    if (audioEngine != nullptr)
    {
        const auto gain = audioEngine->getMasterGain();
        masterFader.setValue(gainToFaderDb(gain), juce::dontSendNotification);
        masterReadout.setText(gain <= 0.0001f ? "-∞ dB" : juce::String(juce::Decibels::gainToDecibels(gain), 1) + " dB", juce::dontSendNotification);
        const auto* rack = audioEngine->getMasterFxRackSnapshot();
        auto hasProcessor = false;
        for (size_t slot = 0; slot < masterFxButtons.size(); ++slot)
        {
            const auto processor = rack != nullptr ? rack->processors[slot] : nullptr;
            const auto bypassed = processor != nullptr && rack->bypass[slot];
            masterFxButtons[slot].setButtonText(processor != nullptr
                                                     ? (bypassed ? "OFF  " : "") + processor->getName().substring(0, 26)
                                                     : "+ Add plug-in");
            masterFxButtons[slot].setTooltip(processor != nullptr
                                                  ? (bypassed ? "Master plug-in bypassed. Right-click to enable or remove it."
                                                              : "Master plug-in active. Right-click to bypass or remove it.")
                                                  : "Add a Master plug-in. Left-click to insert a plug-in.");
            masterFxButtons[slot].setActive(processor != nullptr && ! bypassed);
            hasProcessor = hasProcessor || processor != nullptr;
        }
        masterChainBypass.setEnabled(hasProcessor);
        masterPan.setValue(audioEngine->getMasterPan(), juce::dontSendNotification);
        masterWidth.setValue(audioEngine->getMasterWidth(), juce::dontSendNotification);
        masterMono.setToggleState(audioEngine->isMasterMono(), juce::dontSendNotification);
        masterDim.setToggleState(audioEngine->isMasterDim(), juce::dontSendNotification);
        masterMute.setToggleState(audioEngine->isMasterMuted(), juce::dontSendNotification);
        for (size_t index = 0; index < masterMidiLearnButtons.size(); ++index)
        {
            const auto cc = audioEngine->getMasterMidiCc(static_cast<AudioEngine::MasterMidiTarget>(index));
            masterMidiLearnButtons[index].setButtonText(cc >= 0 ? "CC " + juce::String(cc & 0xff) : "LEARN");
        }
    }
    syncViewVisibility(); if (layoutChanged) resized();
}

void MixerPane::timerCallback()
{
    if (audioEngine == nullptr) return;
    refreshFromModel();
    for (size_t i = 0; i < meters.size(); ++i) meters[i].updatePeak(audioEngine->getTrackPeak(i));
    masterMeter.updatePeak(audioEngine->getMasterPeak());
}

void MixerPane::paint(juce::Graphics& g)
{
    g.fillAll(mixerBackground);
    const auto toolbar = getLocalBounds().removeFromTop(toolbarHeight).toFloat();
    g.setGradientFill(juce::ColourGradient(mixerToolbar.brighter(0.08f), toolbar.getX(), toolbar.getY(), mixerToolbar.darker(0.18f), toolbar.getX(), toolbar.getBottom(), false)); g.fillRect(toolbar);
    g.setColour(juce::Colours::white.withAlpha(0.11f)); g.drawHorizontalLine(0, toolbar.getX(), toolbar.getRight()); g.setColour(mixerAccent.withAlpha(0.58f)); g.drawHorizontalLine(toolbar.getBottom() - 1.0f, toolbar.getX(), toolbar.getRight());
    g.setColour(juce::Colour(0xff0c1012)); g.fillRect(0, toolbarHeight - 3, getWidth(), 3); g.setColour(mixerText.withAlpha(0.40f)); g.fillRoundedRectangle(8.0f, 6.0f, 3.0f, toolbar.getHeight() - 12.0f, 1.5f);
    const auto drawStrip = [&] (juce::Rectangle<int> bounds, juce::Colour accent)
    {
        if (bounds.isEmpty() || ! getLocalBounds().intersects(bounds)) return;
        const auto card = bounds.toFloat().reduced(2.0f, 2.0f);
        drawSkin(g, faceplateSkin(), card);
        g.setGradientFill(juce::ColourGradient(channelFace.brighter(0.035f).withAlpha(0.44f), card.getX(), card.getY(), channelFace.darker(0.16f).withAlpha(0.44f), card.getX(), card.getBottom(), false)); g.fillRoundedRectangle(card, 5.0f);
        g.setColour(mixerBorder.withAlpha(0.92f)); g.drawRoundedRectangle(card, 5.0f, 1.0f); g.setColour(juce::Colours::white.withAlpha(0.08f)); g.drawHorizontalLine(card.getY() + 1.0f, card.getX() + 4.0f, card.getRight() - 4.0f); g.setColour(accent.withAlpha(0.30f)); g.fillRoundedRectangle(card.getX() + 1.0f, card.getY() + 1.0f, card.getWidth() - 2.0f, 2.0f, 1.0f);
        drawSkin(g, screwSkin(), { card.getX() + 7.0f, card.getY() + 7.0f, 10.0f, 10.0f }, 0.58f);
        drawSkin(g, screwSkin(), { card.getRight() - 17.0f, card.getY() + 7.0f, 10.0f, 10.0f }, 0.58f);
    };
    if (activeView == View::mixer)
    {
        const auto verticallyCompact = getHeight() < 390;
        for (size_t i = 0; i < trackStripBounds.size() && static_cast<int>(i) < displayedTrackCount; ++i)
        {
            drawStrip(trackStripBounds[i], trackAccent(trackModel->getTrack(i), static_cast<int>(i)));
            const auto accent = trackAccent(trackModel->getTrack(i), static_cast<int>(i));
            const auto nameplate = labels[i].getBounds().toFloat();
            drawSkin(g, nameplateSkin(), nameplate);
            // A restrained, nearly-black nameplate keeps the strip premium.
            // The track colour is retained as a fine edge and number cue.
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1f282e), nameplate.getX(), nameplate.getY(),
                                                    juce::Colour(0xff172026), nameplate.getRight(), nameplate.getBottom(), false));
            g.fillRoundedRectangle(nameplate.reduced(1.5f), 4.0f);
            g.setColour(accent.withAlpha(0.70f));
            g.drawRoundedRectangle(nameplate.reduced(1.0f), 4.0f, 1.0f);
            g.setColour(accent.withAlpha(0.86f));
            g.fillRoundedRectangle(nameplate.getX() + 2.0f, nameplate.getY() + 3.0f, 3.0f, nameplate.getHeight() - 6.0f, 1.0f);
            g.setColour(juce::Colours::white.withAlpha(0.80f));
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            g.drawText(juce::String(static_cast<int>(i + 1)), nameplate.withWidth(26.0f), juce::Justification::centred);
            for (size_t slot = 0; slot < fxButtons[i].size(); ++slot)
                if (! fxButtons[i][slot].getBounds().isEmpty())
                    drawSkin(g, insertSkin(), fxButtons[i][slot].getBounds().toFloat());
            drawSkin(g, meterOffSkin(), meters[i].getBounds().toFloat(), 0.62f);
            const auto faderBounds = faders[i].getBounds();
            const auto drawScale = [&] (const juce::String& label, float decibels)
            {
                const auto proportion = static_cast<float>(faders[i].valueToProportionOfLength(decibels));
                const auto y = juce::jlimit(faderBounds.getY() + 8, faderBounds.getBottom() - 8,
                                             juce::roundToInt(faderBounds.getY() + (1.0f - proportion) * faderBounds.getHeight()));
                g.setColour(label == "0" ? mixerText.withAlpha(0.96f) : mixerSecondaryText.withAlpha(0.76f));
                g.setFont(juce::Font(juce::FontOptions(label == "0" ? 9.5f : 7.8f, juce::Font::bold)));
                // Engraving and its tick live on the same (right) side of
                // the travel, exactly like a physical console scale.
                // Keep the entire engraving inside the fader module.  The
                // previous 24px text cell was allowed to fall beyond the
                // channel card at standard five-strip density.
                g.drawText(label, faderBounds.getRight() + 4, y - 5, 16, 10, juce::Justification::centred, false);
                g.setColour(mixerSecondaryText.withAlpha(label == "0" ? 0.72f : 0.34f));
                g.drawHorizontalLine(static_cast<float>(y), static_cast<float>(faderBounds.getRight() + 1), static_cast<float>(faderBounds.getRight() + 3));
            };
            // Fine ticks every 3 dB and labelled 6 dB intervals form a
            // symmetric, readable console scale around unity at the centre.
            for (int decibels = -12; decibels <= 12; decibels += 3)
            {
                if (decibels % 6 == 0) continue;
                const auto proportion = static_cast<float>(faders[i].valueToProportionOfLength(decibels));
                const auto y = faderBounds.getY() + (1.0f - proportion) * faderBounds.getHeight();
                g.setColour(mixerSecondaryText.withAlpha(0.34f));
                g.drawHorizontalLine(y, static_cast<float>(faderBounds.getRight() + 1), static_cast<float>(faderBounds.getRight() + 3));
            }
            drawScale("+12", 12.0f); drawScale("+6", 6.0f); drawScale("0", 0.0f); drawScale("-6", -6.0f); drawScale("-12", -12.0f);
            const auto fxHeight = 84;
            const auto firstSlot = fxButtons[i][0].getBounds();
            // The painted rack must use the exact component width, otherwise
            // the final SEND pot can appear to touch the bay edge on a wide
            // display. It also creates a deliberate physical gutter before
            // the meter / fader module.
            auto inserts = juce::Rectangle<int>(firstSlot.getX(), firstSlot.getY() - 16, firstSlot.getWidth(), fxHeight);
            g.setColour(mixerSecondaryText); g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold))); g.drawText("PLUG-INS", inserts.getX() + 4, inserts.getY() + 3, inserts.getWidth() - 8, 13, juce::Justification::centredLeft, false);
            if (! verticallyCompact)
            {
                auto sends = inserts.withY(inserts.getBottom() + 10).withHeight(78);
                g.drawText("AUX SENDS", sends.getX() + 2, sends.getY(), sends.getWidth(), 12, juce::Justification::centredLeft, false);
                const auto sendPanel = sends.withTop(sends.getY() + 13).toFloat();
                // Recessed, faceplate-coloured bay: the pots now feel mounted
                // to the console rather than pasted into a black rectangle.
                juce::ColourGradient sendSurface(juce::Colour(0xff28333a), sendPanel.getX(), sendPanel.getY(),
                                                  juce::Colour(0xff182126), sendPanel.getX(), sendPanel.getBottom(), false);
                g.setGradientFill(sendSurface); g.fillRoundedRectangle(sendPanel, 4.0f);
                g.setColour(mixerBorder.withAlpha(0.78f)); g.drawRoundedRectangle(sendPanel, 4.0f, 1.0f);
                g.setColour(juce::Colours::white.withAlpha(0.07f)); g.drawHorizontalLine(sendPanel.getY() + 1.0f, sendPanel.getX() + 4.0f, sendPanel.getRight() - 4.0f);
                g.setColour(juce::Colours::black.withAlpha(0.18f));
                g.drawRoundedRectangle(sendPanel.reduced(4.0f), 2.5f, 1.0f);
                // Four restrained screws give the send bay a mounted metal
                // face rather than the look of a flat software rectangle.
                drawSkin(g, screwSkin(), { sendPanel.getX() + 5.0f, sendPanel.getY() + 5.0f, 8.0f, 8.0f }, 0.62f);
                drawSkin(g, screwSkin(), { sendPanel.getRight() - 13.0f, sendPanel.getY() + 5.0f, 8.0f, 8.0f }, 0.62f);
                drawSkin(g, screwSkin(), { sendPanel.getX() + 5.0f, sendPanel.getBottom() - 13.0f, 8.0f, 8.0f }, 0.48f);
                drawSkin(g, screwSkin(), { sendPanel.getRight() - 13.0f, sendPanel.getBottom() - 13.0f, 8.0f, 8.0f }, 0.48f);
                g.setColour(mixerBorder.withAlpha(0.30f));
                for (int divider = 1; divider < 4; ++divider)
                {
                    const auto x = sendKnobs[i][static_cast<size_t>(divider)].getBounds().getX() - 4;
                    g.drawVerticalLine(x, static_cast<float>(sends.getY() + 20), static_cast<float>(sends.getBottom() - 14));
                }
                const auto& track = trackModel->getTrack(i);
                for (int slot = 0; slot < 4; ++slot)
                {
                    const auto route = static_cast<size_t>(slot);
                    const auto knobBounds = sendKnobs[i][route].getBounds();
                    const auto& send = track.sends[route];
                    const auto amount = send.targetBus.isValid() ? send.level : 0.0f;
                    const auto amountText = ! send.targetBus.isValid() ? "OFF"
                        : amount <= 0.0001f ? "−∞"
                        : juce::String(juce::Decibels::gainToDecibels(amount), 1);
                    // Values are engravings, not mini LCD buttons. This keeps
                    // the four sends readable at full-mixer scale.
                    g.setColour(send.targetBus.isValid() ? mixerText.withAlpha(0.92f) : mixerSecondaryText.withAlpha(0.62f));
                    g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
                    g.drawText(amountText, knobBounds.getCentreX() - 18, knobBounds.getBottom() + 10, 36, 10,
                               juce::Justification::centred, false);
                }
                const auto panBounds = panSliders[i].getBounds();
                const auto panTextY = panBounds.getY() - 14;
                g.setColour(mixerSecondaryText); g.drawText("PAN", inserts.getX() + 2, panTextY, 34, 12, juce::Justification::centredLeft, false);
                g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
                g.drawText("L", panBounds.getCentreX() - 28, panBounds.getBottom() + 2, 10, 9, juce::Justification::centred, false);
                g.drawText("C", panBounds.getCentreX() - 5, panBounds.getBottom() + 2, 10, 9, juce::Justification::centred, false);
                g.drawText("R", panBounds.getCentreX() + 18, panBounds.getBottom() + 2, 10, 9, juce::Justification::centred, false);
            }
        }
    }
    else
    {
        auto content = getLocalBounds().withTrimmedTop(toolbarHeight).reduced(14, 12);
        const auto inserts = content.removeFromLeft(juce::roundToInt(content.getWidth() * 0.56f)); content.removeFromLeft(12);
        const auto output = content;
        const auto drawPanel = [&] (juce::Rectangle<int> bounds)
        {
            drawStrip(bounds, mixerAccent);
            g.setColour(juce::Colours::black.withAlpha(0.18f));
            g.fillRoundedRectangle(bounds.toFloat().reduced(8.0f), 6.0f);
        };
        drawPanel(inserts); drawPanel(output);
        const auto drawBay = [&] (juce::Rectangle<int> bounds, const juce::String& title)
        {
            auto bay = bounds.toFloat().reduced(1.0f);
            g.setColour(juce::Colour(0xff121a1f).withAlpha(0.74f));
            g.fillRoundedRectangle(bay, 5.0f);
            g.setColour(mixerBorder.withAlpha(0.55f));
            g.drawRoundedRectangle(bay, 5.0f, 1.0f);
            g.setColour(mixerSecondaryText.withAlpha(0.7f));
            g.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
            g.drawText(title, bounds.getX() + 10, bounds.getY() + 6, bounds.getWidth() - 20, 12, juce::Justification::centredLeft, false);
        };
        auto bays = output.reduced(12, 42);
        const auto imageBay = bays.removeFromLeft(juce::roundToInt(bays.getWidth() * 0.24f));
        bays.removeFromLeft(8);
        const auto gainBay = bays.removeFromLeft(juce::roundToInt(bays.getWidth() * 0.34f));
        bays.removeFromLeft(8);
        const auto monitorBay = bays;
        drawBay(imageBay, "STEREO IMAGE");
        drawBay(gainBay, "GAIN / METERS");
        drawBay(monitorBay, "MONITORING");
        const auto heading = [&] (const juce::String& text, juce::Rectangle<int> bounds)
        {
            g.setColour(mixerText.withAlpha(0.90f)); g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
            g.drawText(text, bounds, juce::Justification::centredLeft, false);
        };
        heading("MASTER INSERTS", inserts.reduced(18, 13).withHeight(22));
        heading("MASTER OUTPUT", output.reduced(18, 13).withHeight(22));
        g.setColour(mixerSecondaryText.withAlpha(0.7f));
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText("PROCESSING ORDER  1 → 8", inserts.reduced(22, 36).withHeight(15), juce::Justification::centredLeft, false);
        g.drawText("POST-INSERT / STEREO", monitorBay.reduced(12, 0).withY(output.getY() + 12).withHeight(16), juce::Justification::centredRight, false);
        for (size_t slot = 0; slot < masterFxButtons.size(); ++slot)
        {
            const auto row = masterFxButtons[slot].getBounds();
            if (row.isEmpty()) continue;
            g.setColour(mixerSecondaryText.withAlpha(0.65f));
            g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
            g.drawText(juce::String(slot + 1), row.getX() - 20, row.getY(), 16, row.getHeight(), juce::Justification::centred, false);
            g.setColour(mixerSecondaryText.withAlpha(0.42f));
            g.drawText(slot == 0 ? "SOURCE" : slot == masterFxButtons.size() - 1 ? "FINAL" : "INSERT",
                       row.getRight() - 58, row.getY(), 54, row.getHeight(), juce::Justification::centredRight, false);
        }
        const auto drawMeterPair = [&] (juce::Rectangle<int> bounds, float left, float right)
        {
            const auto drawMeter = [&] (juce::Rectangle<int> meter, float peak)
            {
                g.setColour(juce::Colour(0xff070d10)); g.fillRoundedRectangle(meter.toFloat(), 3.0f);
                const auto db = juce::jlimit(-60.0f, 0.0f, juce::Decibels::gainToDecibels(juce::jmax(peak, 1.0e-6f)));
                const auto height = meter.getHeight() * (db + 60.0f) / 60.0f;
                auto fill = meter.withTrimmedTop(meter.getHeight() - juce::roundToInt(height)).reduced(3, 2);
                g.setGradientFill(juce::ColourGradient(juce::Colour(0xff38d9ed), fill.getX(), fill.getBottom(), juce::Colour(0xff1381be), fill.getX(), fill.getY(), false));
                g.fillRect(fill);
                g.setColour(mixerBorder.withAlpha(0.78f)); g.drawRoundedRectangle(meter.toFloat(), 3.0f, 1.0f);
            };
            const auto meterWidth = juce::jmax(12, bounds.getWidth() / 3);
            drawMeter(bounds.removeFromLeft(meterWidth), left); bounds.removeFromLeft(5); drawMeter(bounds.removeFromLeft(meterWidth), right);
        };
        if (audioEngine != nullptr)
        {
            // The meters belong to the fader module.  Anchor them to the
            // fader's right edge instead of the panel edge so the gain path
            // reads as one piece of hardware at every window width.
            const auto meterWidth = juce::jmin(58, juce::jmax(38, gainBay.getWidth() - 54));
            auto outMeters = juce::Rectangle<int>(gainBay.getCentreX() - meterWidth / 2,
                                                  gainBay.getY() + 40, meterWidth, 92);
            drawMeterPair(outMeters, audioEngine->getMasterLeftPeak(), audioEngine->getMasterRightPeak());
            g.setColour(mixerSecondaryText.withAlpha(0.82f));
            g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
            g.drawText("L", outMeters.getX() + 1, outMeters.getBottom() + 8, 18, 12, juce::Justification::centred, false);
            g.drawText("R", outMeters.getX() + outMeters.getWidth() / 2 + 1, outMeters.getBottom() + 8, 18, 12, juce::Justification::centred, false);
            g.drawText("PEAK", masterMono.getX(), masterMono.getY() - 20, 40, 12, juce::Justification::left, false);
            const auto peak = juce::jmax(audioEngine->getMasterLeftPeak(), audioEngine->getMasterRightPeak());
            const auto peakDb = peak > 0.000001f ? juce::Decibels::gainToDecibels(peak) : -96.0f;
            g.setColour(mixerText); g.drawText(juce::String(peakDb, 1) + " dB", masterMono.getX() + 42, masterMono.getY() - 20, 72, 12, juce::Justification::left, false);
        }
        g.setColour(mixerSecondaryText.withAlpha(0.8f));
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText("PAN", masterPan.getX(), masterPan.getY() - 18, masterPan.getWidth(), 12, juce::Justification::centred, false);
        g.drawText("WIDTH", masterWidth.getX(), masterWidth.getY() - 18, masterWidth.getWidth(), 12, juce::Justification::centred, false);
        g.drawText("CENTER / IMAGE", masterPan.getX() - 8, masterPan.getBottom() + 8,
                   masterWidth.getRight() - masterPan.getX() + 16, 12, juce::Justification::centred, false);
        g.setColour(mixerText.withAlpha(0.86f));
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        const juce::String panText = std::abs(masterPan.getValue()) < 0.01
            ? juce::String("C")
            : juce::String(masterPan.getValue() < 0.0 ? "L " : "R ") + juce::String(std::abs(masterPan.getValue()), 2);
        g.drawText(panText, masterPan.getX() - 4, masterPan.getBottom() + 24, masterPan.getWidth() + 8, 14, juce::Justification::centred, false);
        g.drawText(juce::String(masterWidth.getValue() * 100.0, 0) + " %", masterWidth.getX() - 4, masterWidth.getBottom() + 24, masterWidth.getWidth() + 8, 14, juce::Justification::centred, false);
        g.setColour(mixerSecondaryText); g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawFittedText("Add EQ, limiter, saturation or any AU/VST3 processor in the order it should process the Master bus.",
                         inserts.reduced(22, 54).withTrimmedBottom(30), juce::Justification::centred, 3);
        g.setColour(mixerSecondaryText.withAlpha(0.65f));
        g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
        g.drawText("OUTPUT MODE", masterMono.getX(), masterMono.getY() - 37, masterMute.getRight() - masterMono.getX(), 12, juce::Justification::centred, false);
    }
}

void MixerPane::resized()
{
    auto area = getLocalBounds(); auto toolbar = area.removeFromTop(toolbarHeight);
    mixerTab.setBounds(toolbar.removeFromLeft(86).reduced(5, 7)); toolbar.removeFromLeft(6);
    masterTab.setBounds(toolbar.removeFromLeft(86).reduced(3, 7));
    trackStripBounds.fill({});
    const auto content = area.reduced(6, 7); const auto verticallyCompact = content.getHeight() < 334; const auto gap = 6;
    // Standard density always attempts to show the five primary tracks as a
    // complete console, as in the reference; horizontal scrolling starts only
    // after the useful minimum strip width is reached.
    const auto standardWidth = juce::jlimit(220, 314, (content.getWidth() - gap * 4) / 5);
    const auto stripWidth = standardWidth;
    if (activeView == View::mixer)
    {
        for (int i = 0; i < displayedTrackCount; ++i)
        {
            const auto strip = juce::Rectangle<int>(content.getX() + i * (stripWidth + gap) - horizontalScroll, content.getY(), stripWidth, content.getHeight()); trackStripBounds[static_cast<size_t>(i)] = strip; auto inner = strip.reduced(12, 10);
            labels[i].setBounds(inner.removeFromBottom(25)); auto controls = inner.removeFromBottom(39); const auto controlWidth = controls.getWidth() / 5;
            armButtons[i].setBounds(controls.removeFromLeft(controlWidth).reduced(2, 4));
            monitorButtons[i].setBounds(controls.removeFromLeft(controlWidth).reduced(2, 4));
            muteButtons[i].setBounds(controls.removeFromLeft(controlWidth).reduced(2, 4));
            soloButtons[i].setBounds(controls.removeFromLeft(controlWidth).reduced(2, 4));
            chainBypassButtons[i].setBounds(controls.reduced(2, 4));
            // Keep the physical fader length within console proportions on
            // every dock height. The old layout let this column fill all
            // leftover space, producing the visibly stretched strips.
            // A dedicated separation slot keeps the meter/fader hardware from
            // visually touching the plug-in rack. The narrower module also
            // shifts the entire volume section a little to the right.
            auto faderColumn = inner.removeFromRight(90);
            inner.removeFromRight(10);
            // Align the gain hardware with the first insert row, leaving the
            // screw/header area clean and visually separate from the meter.
            faderColumn.removeFromTop(32);
            const auto faderHeight = juce::jmax(80, faderColumn.getHeight() - 6);
            faderColumn = faderColumn.withHeight(faderHeight);
            const auto readoutArea = faderColumn.removeFromBottom(21);
            meters[i].setBounds(faderColumn.removeFromLeft(15).reduced(0, 3));
            // Leave a precise right-side engraving channel for the dB scale;
            // all numbers now remain inside the hardware module.
            // The 6px air gap is intentional: meter and fader read as one
            // gain module without visually fusing into a single black bar.
            const auto faderArea = faderColumn.withTrimmedLeft(6).withTrimmedRight(41).reduced(0, 3);
            faders[i].setBounds(faderArea);
            // The gain LCD belongs to the fader, not to the strip edge.
            // Keeping it centered below the rail creates a compact console
            // unit at every dock width.
            gainReadouts[i].setBounds(faderArea.getCentreX() - 23, readoutArea.getY() + 2, 46, 17);
            // Offset the full control rack together. Its PAN section consumes
            // the remaining height so the lower half never becomes dead space.
            inner.removeFromTop(12);
            const auto expandedRack = fxRacksExpanded[static_cast<size_t>(i)] && ! verticallyCompact;
            auto fxArea = inner.removeFromTop(expandedRack ? 164 : 38);
            fxRackButtons[static_cast<size_t>(i)].setBounds(fxArea.getX(), fxArea.getY(), fxArea.getWidth(), 22);
            const auto slotArea = fxArea.withTrimmedTop(26);
            const auto rowHeight = expandedRack ? juce::jmax(16, slotArea.getHeight() / static_cast<int>(TrackDataModel::maxFxSlots)) : 0;
            for (size_t slot = 0; slot < fxButtons[static_cast<size_t>(i)].size(); ++slot)
                fxButtons[static_cast<size_t>(i)][slot].setBounds(expandedRack
                    ? juce::Rectangle<int>(slotArea.getX(), slotArea.getY() + static_cast<int>(slot) * rowHeight,
                                             slotArea.getWidth(), rowHeight - 2)
                    : juce::Rectangle<int>());
            if (verticallyCompact)
            {
                for (auto& knob : sendKnobs[static_cast<size_t>(i)]) knob.setBounds({});
                for (auto& button : sendRouteButtons[static_cast<size_t>(i)]) button.setBounds({});
                panSliders[i].setBounds({});
            }
            else
            {
                auto sends = inner.removeFromTop(82); sends.removeFromTop(21);
                // Match the four painted mounting cells exactly.  The old
                // broad bounds put the last pot visually against the edge.
                const auto knobArea = sends.reduced(8, 0);
                const auto cellWidth = knobArea.getWidth() / 4;
                const auto knobWidth = juce::jlimit(32, 40, cellWidth - 8);
                for (size_t slot = 0; slot < sendKnobs[static_cast<size_t>(i)].size(); ++slot)
                {
                    const auto cell = juce::Rectangle<int>(knobArea.getX() + static_cast<int>(slot) * cellWidth, knobArea.getY(), cellWidth, sends.getHeight());
                    sendKnobs[static_cast<size_t>(i)][slot].setBounds(cell.withSizeKeepingCentre(knobWidth, knobWidth));
                    const auto knobBounds = sendKnobs[static_cast<size_t>(i)][slot].getBounds();
                    sendRouteButtons[static_cast<size_t>(i)][slot].setBounds(knobBounds.getCentreX() - 20, knobBounds.getBottom(), 40, 10);
                }
                // The remaining rack height belongs to PAN, so the channel
                // retains a deliberate vertical rhythm instead of a dead
                // rectangle between the pan and its bottom action row.
                auto panArea = inner.reduced(0, 4);
                const auto panSize = 66;
                panSliders[i].setBounds(panArea.getCentreX() - panSize / 2,
                                        panArea.getBottom() - panSize - 17,
                                        panSize, panSize);
            }
        }
    }
    else
    {
        auto content = getLocalBounds().withTrimmedTop(toolbarHeight).reduced(14, 12);
        auto inserts = content.removeFromLeft(juce::roundToInt(content.getWidth() * 0.56f)); content.removeFromLeft(12);
        auto output = content;
        const auto slots = inserts.reduced(24, 58);
        const auto rowHeight = juce::jmax(34, (slots.getHeight() - 18) / static_cast<int>(masterFxButtons.size()));
        for (size_t i = 0; i < masterFxButtons.size(); ++i)
            masterFxButtons[i].setBounds(slots.getX(), slots.getY() + static_cast<int>(i) * rowHeight, slots.getWidth(), rowHeight - 8);
        masterChainBypass.setBounds(inserts.getRight() - 54, inserts.getY() + 12, 32, 28);
        auto bays = output.reduced(12, 42);
        const auto imageBay = bays.removeFromLeft(juce::roundToInt(bays.getWidth() * 0.24f));
        bays.removeFromLeft(8);
        const auto gainBay = bays.removeFromLeft(juce::roundToInt(bays.getWidth() * 0.34f));
        bays.removeFromLeft(8);
        const auto monitorBay = bays;
        auto faderArea = gainBay.reduced(12, 42);
        faderArea.setX(gainBay.getX() + gainBay.getWidth() / 2 - 42);
        faderArea.setWidth(76);
        faderArea.setY(gainBay.getY() + 148);
        faderArea.setHeight(juce::jmax(80, gainBay.getBottom() - faderArea.getY() - 34));
        masterFader.setBounds(faderArea);
        const auto knobX = imageBay.getCentreX() - 27;
        masterPan.setBounds(knobX, imageBay.getY() + 58, 54, 54);
        masterWidth.setBounds(knobX, imageBay.getY() + 174, 54, 54);
        masterMidiLearnButtons[1].setBounds(masterPan.getX() + 2,
                                            masterPan.getBottom() + 22,
                                            50, 16);
        masterMidiLearnButtons[2].setBounds(masterWidth.getX() + 2,
                                            masterWidth.getBottom() + 22,
                                            50, 16);
        const auto buttonsY = monitorBay.getBottom() - 48;
        const auto buttonWidth = juce::jmax(40, (monitorBay.getWidth() - 54) / 3);
        const auto buttonX = monitorBay.getX() + 18;
        masterMono.setBounds(buttonX, buttonsY, buttonWidth, 26);
        masterDim.setBounds(buttonX + 9 + buttonWidth, buttonsY, buttonWidth, 26);
        masterMute.setBounds(buttonX + 18 + buttonWidth * 2, buttonsY, buttonWidth, 26);
        masterMidiLearnButtons[3].setBounds(masterMono.getX(), masterMono.getY() - 20,
                                            buttonWidth, 16);
        masterMidiLearnButtons[4].setBounds(masterDim.getX(), masterDim.getY() - 20,
                                            buttonWidth, 16);
        masterMidiLearnButtons[5].setBounds(masterMute.getX(), masterMute.getY() - 20,
                                            buttonWidth, 16);
        masterReadout.setBounds(masterFader.getBounds().getCentreX() - 36, masterFader.getBottom() + 7, 72, 18);
        masterMidiLearnButtons[0].setBounds(masterFader.getX() + 8,
                                            masterReadout.getBottom() + 6,
                                            60, 16);
        masterLabel.setBounds(output.getX() + 12, output.getBottom() - 20, output.getWidth() - 24, 16);
        masterMeter.setBounds({});
    }
    syncViewVisibility();
}

void MixerPane::mouseDown(const juce::MouseEvent& event) { if (event.position.y <= 6.0f) { resizingDock = true; dragStartHeight = getHeight(); } }
void MixerPane::mouseDrag(const juce::MouseEvent& event) { if (resizingDock) requestHeight(dragStartHeight - juce::roundToInt(event.getDistanceFromDragStartY())); }
void MixerPane::mouseUp(const juce::MouseEvent&) { resizingDock = false; }
void MixerPane::mouseDoubleClick(const juce::MouseEvent& event) { if (event.position.y <= 8.0f) requestHeight(preferredHeight); }
void MixerPane::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.position.y < toolbarHeight || activeView == View::master) return;
    const auto delta = wheel.deltaX != 0.0f ? wheel.deltaX : (event.mods.isShiftDown() ? wheel.deltaY : 0.0f); if (delta == 0.0f) return;
    const auto stripWidth = juce::jlimit(220, 314, (getWidth() - 12 - 6 * 4) / 5);
    const auto maximum = juce::jmax(0, (stripWidth + 6) * displayedTrackCount - getWidth() + 12);
    horizontalScroll = juce::jlimit(0, maximum, horizontalScroll - juce::roundToInt(delta * 180.0f)); resized();
}
void MixerPane::requestHeight(int height)
{
    dockHeight = juce::jlimit(150, 760, height);
    if (onHeightChangeRequested != nullptr) onHeightChangeRequested(dockHeight);
}

size_t MixerPane::firstVacantFxSlot(int trackIndex) const noexcept
{
    if (trackModel == nullptr || trackIndex < 0 || static_cast<size_t>(trackIndex) >= trackModel->getTrackCount())
        return 0;
    if (const auto* rack = trackModel->getFxRackSnapshot(static_cast<size_t>(trackIndex)); rack != nullptr)
        for (size_t slot = 0; slot < rack->processors.size(); ++slot)
            if (rack->processors[slot] == nullptr)
                return slot;
    return TrackDataModel::maxFxSlots;
}

size_t MixerPane::activeFxCount(int trackIndex) const noexcept
{
    if (trackModel == nullptr || trackIndex < 0 || static_cast<size_t>(trackIndex) >= trackModel->getTrackCount())
        return 0;
    const auto* rack = trackModel->getFxRackSnapshot(static_cast<size_t>(trackIndex));
    if (rack == nullptr)
        return 0;
    return static_cast<size_t>(std::count_if(rack->processors.begin(), rack->processors.end(),
        [] (const auto& processor) { return processor != nullptr; }));
}
