#include "ControlBar.h"
#include "BinaryData.h"
#include "../AudioEngine/AudioEngine.h"
#include "../Models/TrackDataModel.h"
#include "Theme/StudioForgeLookAndFeel.h"

#include <array>
#include <cmath>
#include <utility>

namespace
{
const auto darkPanel = juce::Colour(0xff353535);
const auto accentBlue = juce::Colour(0xff6f88a8);
const auto accentCyan = juce::Colour(0xffa7c5df);
constexpr float logoForegroundBrightness = 0.12f;
constexpr float logoAlphaScale = 3188.0f;
constexpr float idleLogoBrightnessBoost = 0.28f;
constexpr float illuminatedLogoBrightnessBoost = 0.72f;

juce::Rectangle<int> findVisibleLogoBounds(const juce::Image& source)
{
    juce::Rectangle<int> visibleBounds;
    for (int y = 0; y < source.getHeight(); ++y)
        for (int x = 0; x < source.getWidth(); ++x)
            if (source.getPixelAt(x, y).getBrightness() > logoForegroundBrightness)
                visibleBounds = visibleBounds.getUnion({ x, y, 1, 1 });

    return visibleBounds;
}

juce::Image createTransparentLogo(const juce::Image& source, float brightnessBoost)
{
    const auto visibleBounds = findVisibleLogoBounds(source);
    if (visibleBounds.isEmpty())
        return {};

    const auto croppedSource = source.getClippedImage(visibleBounds.expanded(4).getIntersection(source.getBounds()));
    juce::Image transparentLogo(juce::Image::ARGB, croppedSource.getWidth(), croppedSource.getHeight(), true);
    for (int y = 0; y < croppedSource.getHeight(); ++y)
        for (int x = 0; x < croppedSource.getWidth(); ++x)
        {
            const auto colour = croppedSource.getPixelAt(x, y);
            if (colour.getBrightness() <= logoForegroundBrightness)
                continue;

            const auto alpha = juce::jlimit(0, 255,
                juce::roundToInt((colour.getBrightness() - logoForegroundBrightness) * logoAlphaScale));
            transparentLogo.setPixelAt(x, y, colour.brighter(brightnessBoost)
                                                  .withAlpha(static_cast<uint8_t>(alpha)));
        }
    return transparentLogo;
}

juce::Image loadBrandLogoSource()
{
    return juce::ImageFileFormat::loadFrom(BinaryData::LiveDawLogo_png,
                                            BinaryData::LiveDawLogo_pngSize);
}

void drawConsoleFrame(juce::Graphics& g, juce::Rectangle<float> bounds, float radius,
                      juce::Colour fill = juce::Colour(0xff10171a))
{
    g.setColour(juce::Colours::black.withAlpha(0.50f));
    g.fillRoundedRectangle(bounds.translated(0.0f, 1.0f), radius);
    g.setGradientFill(juce::ColourGradient(fill.brighter(0.12f), bounds.getCentreX(), bounds.getY(),
                                            fill.darker(0.28f), bounds.getCentreX(), bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius - 0.5f, 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.78f));
    g.drawRoundedRectangle(bounds, radius, 1.0f);
}

struct ConsoleButtonSprites
{
    juce::Image countIn;
    juce::Image metronome;
};

const ConsoleButtonSprites& consoleButtonSprites()
{
    static const auto sprites = []
    {
        ConsoleButtonSprites result;
        juce::MemoryInputStream stream(BinaryData::countinmetronomeconsolev2_png,
                                       BinaryData::countinmetronomeconsolev2_pngSize, false);
        const auto sheet = juce::ImageFileFormat::loadFrom(stream);
        // The generated sheet has transparent padding around two square keys.
        // Keep the individual source keys so they can be painted dynamically.
        result.countIn = sheet.getClippedImage({ 160, 100, 710, 680 });
        result.metronome = sheet.getClippedImage({ 910, 100, 710, 680 });
        return result;
    }();
    return sprites;
}
}

WorkspaceModeButton::WorkspaceModeButton(Mode requestedMode, juce::String label)
    : juce::Button(std::move(label)), mode(requestedMode)
{
    setClickingTogglesState(false);
    setWantsKeyboardFocus(false);
}

AutoKeyButton::AutoKeyButton()
    : juce::Button("AUTO KEY")
{
    setWantsKeyboardFocus(false);
}

void AutoKeyButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    if (down)
        bounds = bounds.translated(0.0f, 1.0f);

    const auto face = highlighted ? juce::Colour(0xff26343a) : juce::Colour(0xff172126);
    drawConsoleFrame(g, bounds, 5.0f, face);

    // Match WorkspaceModeButton's inactive label token exactly so Auto Key
    // reads as one control family instead of a separate typography system.
    const auto ink = down ? juce::Colours::white : juce::Colour(0xffd1e0e3).withAlpha(0.82f);
    g.setColour(ink);
    g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    g.drawText("AUTO KEY", bounds.reduced(2.0f, 0.0f),
               juce::Justification::centred, false);
}

void WorkspaceModeButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    if (down) bounds = bounds.translated(0.0f, 1.0f);
    const auto active = getToggleState();
    // Workspace choices are one family: cyan means selected everywhere.
    // Amber remains reserved for the metronome, so Stage never looks like a
    // warning or an unrelated tool.
    auto face = active ? juce::Colour(0xff167d9c) : juce::Colour(0xff172126);
    if (mode == Mode::stage && active) face = juce::Colour(0xff197d93);
    if (mode == Mode::stream && active) face = juce::Colour(0xff14768c);
    if (highlighted && ! active) face = face.brighter(0.12f);
    // Preserve the individual console-key design. The only removed layer is
    // the large background slab behind the whole group.
    drawConsoleFrame(g, bounds, 5.0f, face);

    auto iconArea = bounds.removeFromTop(bounds.getHeight() * 0.58f).reduced(10.0f, 4.0f);
    const auto ink = active ? juce::Colour(0xffdcf9ff) : juce::Colour(0xffd1e0e3).withAlpha(0.82f);
    g.setColour(ink);
    if (mode == Mode::studio)
    {
        const auto x = iconArea.getCentreX();
        for (auto offset : { -7.0f, 0.0f, 7.0f })
        {
            g.drawLine(x + offset, iconArea.getY() + 1.0f, x + offset, iconArea.getBottom() - 1.0f, 1.5f);
            g.fillRoundedRectangle(x + offset - 2.6f, iconArea.getCentreY() - (offset == 0.0f ? 0.0f : 3.0f), 5.2f, 2.6f, 0.8f);
        }
    }
    else if (mode == Mode::stage)
    {
        const auto x = iconArea.getCentreX();
        const auto top = iconArea.getY() + 2.0f;
        const auto bottom = iconArea.getBottom() - 2.0f;
        // A compact stage riser and spot-light: deliberately geometric so it
        // reads at 42 px without the generic "person" silhouette.
        g.fillRoundedRectangle(x - 3.2f, top, 6.4f, 2.2f, 0.8f);
        g.drawLine(x, top + 3.5f, x, bottom - 6.0f, 1.35f);
        g.fillRoundedRectangle(x - 7.6f, bottom - 6.0f, 15.2f, 3.6f, 0.9f);
        g.drawLine(x - 10.0f, bottom, x + 10.0f, bottom, 1.35f);
        g.drawLine(x - 5.0f, top + 4.0f, x - 8.0f, top + 7.0f, 1.0f);
        g.drawLine(x + 5.0f, top + 4.0f, x + 8.0f, top + 7.0f, 1.0f);
    }
    else
    {
        const auto c = iconArea.getCentre();
        g.fillEllipse(c.x - 1.7f, c.y - 1.7f, 3.4f, 3.4f);
        g.drawEllipse(c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f, 1.35f);
        g.drawEllipse(c.x - 9.5f, c.y - 9.5f, 19.0f, 19.0f, 1.35f);
        g.drawLine(c.x - 11.5f, c.y + 8.2f, c.x - 6.5f, c.y + 5.0f, 1.2f);
        g.drawLine(c.x + 11.5f, c.y + 8.2f, c.x + 6.5f, c.y + 5.0f, 1.2f);
    }
    g.setColour(ink);
    g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    g.drawText(getButtonText(), bounds.withTop(iconArea.getBottom() - 1.0f).reduced(2.0f, 0.0f),
               juce::Justification::centred, false);
}

TransportIconButton::TransportIconButton(Icon requestedIcon)
    : juce::Button({}), icon(requestedIcon)
{
    setWantsKeyboardFocus(false);
}

void TransportIconButton::setPulsePhase(float phase) noexcept
{
    phase = juce::jlimit(0.0f, 1.0f, phase);
    if (std::abs(pulsePhase - phase) < 0.01f)
        return;

    pulsePhase = phase;
    repaint();
}

void TransportIconButton::paintButton(juce::Graphics& graphics, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const auto active = getToggleState();
    const auto raised = true;
    if (icon == Icon::countIn || icon == Icon::metronome)
    {
        // These two controls use the dedicated console sprite, while the
        // interaction state remains live: the inactive key is recessed and
        // the selected key regains its full cyan/amber illumination.
        const auto& sprites = consoleButtonSprites();
        const auto& sprite = icon == Icon::countIn ? sprites.countIn : sprites.metronome;
        const auto inset = active ? 0.0f : 2.0f;
        const auto destination = bounds.reduced(inset).translated(0.0f, down ? 1.0f : 0.0f).toNearestInt();
        // Count-in deliberately has a wider hit area than its square key.
        // Anchor its visible key to the left of that area, beside the
        // metronome, instead of centring it in invisible empty space.
        const auto spriteSide = juce::jmin(destination.getWidth(), destination.getHeight());
        const auto spriteX = icon == Icon::countIn ? destination.getX()
                                                   : destination.getCentreX() - spriteSide / 2;
        const auto spriteY = destination.getCentreY() - spriteSide / 2;
        graphics.setOpacity(active ? 1.0f : (highlighted ? 0.72f : 0.52f));
        graphics.drawImageWithin(sprite, spriteX, spriteY, spriteSide, spriteSide,
                                 juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        graphics.setOpacity(1.0f);
        // The sprite already contains its intentional key frame. Do not add
        // a second hover rectangle around the entire hit target.
        return;
    }
    // Keep the hit target generous, but make the visible cap a compact key.
    // This avoids the tall, stretched-button look on a 48 px control bar.
    const auto keySize = juce::jmax(24.0f, juce::jmin(bounds.getWidth() - 3.0f, bounds.getHeight() - 12.0f));
    const auto keyBounds = juce::Rectangle<float>(keySize, keySize).withCentre(bounds.getCentre());
    const auto socket = keyBounds.expanded(1.2f);
    const auto pressOffset = down ? 1.0f : 0.0f;
    const auto face = keyBounds.translated(0.0f, pressOffset);
    const auto base = isColourSpecified(juce::TextButton::buttonColourId)
        ? findColour(juce::TextButton::buttonColourId)
        : juce::Colour(0xff151d20);

    auto activeColour = juce::Colour(0xff4a8197);
    if (icon == Icon::play) activeColour = juce::Colour(0xff35be54);
    if (icon == Icon::record) activeColour = juce::Colour(0xffd24954);
    if (icon == Icon::countIn) activeColour = juce::Colour(0xff35c8f4);
    if (icon == Icon::metronome) activeColour = juce::Colour(0xffe3a84b);

    if (icon == Icon::record && active)
    {
        const auto glow = 0.12f + pulsePhase * 0.18f;
        graphics.setColour(activeColour.brighter(0.35f).withAlpha(glow));
        graphics.fillRoundedRectangle(socket.expanded(2.0f), 7.5f);
    }

    graphics.setColour(juce::Colours::black.withAlpha(0.58f));
    graphics.fillRoundedRectangle(socket, 6.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.10f));
    graphics.drawRoundedRectangle(socket, 6.0f, 0.8f);

    auto cap = active ? activeColour : base;
    // Record must remain recognisable before recording begins; a dark-red
    // cap and red lamp are standard transport language in every DAW.
    if (icon == Icon::record && ! active)
        cap = juce::Colour(0xff4c1c23);
    if (down) cap = cap.darker(0.28f);
    else if (highlighted) cap = cap.brighter(0.10f);
    graphics.setGradientFill(juce::ColourGradient(cap.brighter(active ? 0.16f : 0.10f), face.getCentreX(), face.getY(),
                                                    cap.darker(down ? 0.26f : 0.15f), face.getCentreX(), face.getBottom(), false));
    graphics.fillRoundedRectangle(face, 4.8f);
    graphics.setColour(juce::Colours::white.withAlpha(active ? 0.30f : 0.19f));
    graphics.drawRoundedRectangle(face.reduced(0.45f), 4.4f, 0.8f);
    graphics.setColour(juce::Colours::black.withAlpha(0.58f));
    graphics.drawRoundedRectangle(face, 4.8f, 1.0f);

    const auto iconBounds = face.reduced(7.0f).translated(0.0f, down ? 0.35f : 0.0f);
    const auto centreX = iconBounds.getCentreX();
    const auto centreY = iconBounds.getCentreY();
    juce::Path path;
    const auto glyphColour = active
        ? juce::Colours::white.withAlpha(0.98f)
        : juce::Colour(0xffdbe8ec).withAlpha(highlighted ? 0.98f : 0.68f);
    graphics.setColour(glyphColour);

    const auto drawTriangle = [&graphics](float left, float top, float width, float height, bool pointsRight)
    {
        juce::Path triangle;
        if (pointsRight)
        {
            triangle.startNewSubPath(left, top);
            triangle.lineTo(left + width, top + height * 0.5f);
            triangle.lineTo(left, top + height);
        }
        else
        {
            triangle.startNewSubPath(left + width, top);
            triangle.lineTo(left, top + height * 0.5f);
            triangle.lineTo(left + width, top + height);
        }
        triangle.closeSubPath();
        graphics.fillPath(triangle);
    };

    switch (icon)
    {
        case Icon::goToBeginning:
            graphics.fillRoundedRectangle(iconBounds.getX(), iconBounds.getY() + 1.0f, 2.0f, iconBounds.getHeight() - 2.0f, 0.6f);
            drawTriangle(iconBounds.getX() + 4.0f, iconBounds.getY() + 1.4f,
                         iconBounds.getWidth() - 5.2f, iconBounds.getHeight() - 2.8f, false);
            break;

        case Icon::rewind:
            drawTriangle(iconBounds.getX(), iconBounds.getY() + 1.0f,
                         iconBounds.getWidth() * 0.58f, iconBounds.getHeight() - 2.0f, false);
            drawTriangle(iconBounds.getX() + iconBounds.getWidth() * 0.38f, iconBounds.getY() + 1.0f,
                         iconBounds.getWidth() * 0.58f, iconBounds.getHeight() - 2.0f, false);
            break;

        case Icon::play:
            drawTriangle(iconBounds.getX() + 2.5f, iconBounds.getY() + 1.0f,
                         iconBounds.getWidth() - 4.5f, iconBounds.getHeight() - 2.0f, true);
            break;

        case Icon::stop:
            graphics.fillRoundedRectangle(centreX - 4.6f, centreY - 4.6f, 9.2f, 9.2f, 1.5f);
            break;

        case Icon::forward:
            drawTriangle(iconBounds.getX() + iconBounds.getWidth() * 0.04f, iconBounds.getY() + 1.0f,
                         iconBounds.getWidth() * 0.58f, iconBounds.getHeight() - 2.0f, true);
            drawTriangle(iconBounds.getX() + iconBounds.getWidth() * 0.42f, iconBounds.getY() + 1.0f,
                         iconBounds.getWidth() * 0.58f, iconBounds.getHeight() - 2.0f, true);
            break;

        case Icon::record:
        {
            const auto lamp = active ? juce::Colour(0xffff6670) : juce::Colour(0xffe8424e);
            graphics.setColour(lamp.withAlpha(active ? 0.98f : 0.80f));
            graphics.drawEllipse(centreX - 6.0f, centreY - 6.0f, 12.0f, 12.0f, 1.1f);
            graphics.setColour(lamp);
            graphics.fillEllipse(centreX - 3.8f, centreY - 3.8f, 7.6f, 7.6f);
            break;
        }

        case Icon::metronome:
        {
            const auto top = iconBounds.getY() + 1.0f;
            const auto bottom = iconBounds.getBottom() - 1.0f;
            const auto left = iconBounds.getX() + 1.5f;
            const auto right = iconBounds.getRight() - 1.5f;
            path.startNewSubPath(centreX, top);
            path.lineTo(right, bottom);
            path.lineTo(left, bottom);
            path.closeSubPath();
            graphics.strokePath(path, juce::PathStrokeType(1.55f));
            graphics.drawLine(centreX - 1.2f, top + 3.0f, centreX + 1.2f, bottom - 3.0f, 1.5f);
            break;
        }

        case Icon::countIn:
            graphics.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::bold)));
            graphics.drawFittedText("1 2 3", iconBounds.toNearestInt(), juce::Justification::centred, 1);
            break;
    }
}

void DigitalTimeReadout::setText(juce::String newText, juce::NotificationType)
{
    if (text == newText) return;
    text = std::move(newText);
    repaint();
}

void DigitalTimeReadout::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour(0xff050e11));
    g.fillRoundedRectangle(bounds, 3.5f);
    g.setColour(juce::Colour(0xff0d363e));
    g.drawRoundedRectangle(bounds, 3.5f, 1.0f);

    const auto digitCount = 8; // HHMMSSCC; separators occupy their own narrow slots.
    const auto height = bounds.getHeight() * 0.52f;
    const auto width = height * 0.48f;
    const auto separatorWidth = width * 0.42f;
    const auto gap = width * 0.17f;
    const auto totalWidth = 8.0f * width + 7.0f * gap + 3.0f * separatorWidth;
    auto x = bounds.getCentreX() - totalWidth * 0.5f;
    const auto y = bounds.getCentreY() - height * 0.5f;
    const auto lit = juce::Colour(0xff62eafa);
    // Inactive LED segments should be nearly black, not a second visible
    // digit sitting behind the active one.
    const auto unlit = juce::Colour(0xff061417);

    const auto drawDigit = [&] (int value, float left)
    {
        static constexpr bool digitSegments[10][7] = {
            { true, true, true, true, true, true, false }, { false, true, true, false, false, false, false },
            { true, true, false, true, true, false, true }, { true, true, true, true, false, false, true },
            { false, true, true, false, false, true, true }, { true, false, true, true, false, true, true },
            { true, false, true, true, true, true, true }, { true, true, true, false, false, false, false },
            { true, true, true, true, true, true, true }, { true, true, true, true, false, true, true }
        };
        const auto thickness = juce::jmax(1.0f, width * 0.12f);
        const auto vertical = (height - thickness * 3.0f) * 0.5f;
        const auto horizontal = width - thickness * 1.6f;
        const std::array<juce::Rectangle<float>, 7> segmentBounds {
            juce::Rectangle<float>(left + thickness * 0.8f, y, horizontal, thickness),
            juce::Rectangle<float>(left + width - thickness, y + thickness * 0.55f, thickness, vertical),
            juce::Rectangle<float>(left + width - thickness, y + height - vertical - thickness * 0.55f, thickness, vertical),
            juce::Rectangle<float>(left + thickness * 0.8f, y + height - thickness, horizontal, thickness),
            juce::Rectangle<float>(left, y + height - vertical - thickness * 0.55f, thickness, vertical),
            juce::Rectangle<float>(left, y + thickness * 0.55f, thickness, vertical),
            juce::Rectangle<float>(left + thickness * 0.8f, y + height * 0.5f - thickness * 0.5f, horizontal, thickness)
        };
        for (size_t index = 0; index < segmentBounds.size(); ++index)
        {
            g.setColour(digitSegments[static_cast<size_t>(value)][index] ? lit : unlit);
            g.fillRoundedRectangle(segmentBounds[index], thickness * 0.35f);
        }
    };

    for (const auto character : text)
    {
        if (juce::CharacterFunctions::isDigit(character))
        {
            drawDigit(character - '0', x);
            x += width + gap;
        }
        else
        {
            g.setColour(lit.withAlpha(0.90f));
            if (character == ':')
            {
                g.fillEllipse(x + separatorWidth * 0.2f, y + height * 0.28f, separatorWidth * 0.58f, separatorWidth * 0.58f);
                g.fillEllipse(x + separatorWidth * 0.2f, y + height * 0.68f, separatorWidth * 0.58f, separatorWidth * 0.58f);
            }
            else
                g.fillEllipse(x + separatorWidth * 0.20f, y + height - separatorWidth * 0.72f, separatorWidth * 0.60f, separatorWidth * 0.60f);
            x += separatorWidth + gap;
        }
    }
}

ControlBar::ControlBar(TrackDataModel* model, AudioEngine* engine)
    : transportLCD(model), trackModel(model), audioEngine(engine)
{
    const auto logoSource = loadBrandLogoSource();
    brandLogo = createTransparentLogo(logoSource, idleLogoBrightnessBoost);
    illuminatedBrandLogo = createTransparentLogo(logoSource, illuminatedLogoBrightnessBoost);

    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(recordButton);
    addAndMakeVisible(countInButton);
    addAndMakeVisible(studioModeButton);
    addAndMakeVisible(stageModeButton);
    addAndMakeVisible(streamModeButton);
    addAndMakeVisible(countInBarsBox);
    addAndMakeVisible(punchButton);
    addAndMakeVisible(cycleButton);
    addAndMakeVisible(metronomeButton);
    addAndMakeVisible(bpmSlider);
    addAndMakeVisible(inspectorButton);
    addAndMakeVisible(mixerButton);
    addAndMakeVisible(pianoRollButton);
    addAndMakeVisible(browserButton);
    addAndMakeVisible(addTrackButton);
    addAndMakeVisible(autoKeyButton);
    addAndMakeVisible(goToBeginningButton);
    addAndMakeVisible(rewindButton);
    addAndMakeVisible(forwardButton);
    addAndMakeVisible(transportLCD);
    addAndMakeVisible(masterMeter);
    addAndMakeVisible(pointerTool);
    addAndMakeVisible(scissorsTool);
    addAndMakeVisible(eraserTool);
    inspectorButton.setClickingTogglesState(true);
    mixerButton.setClickingTogglesState(true);
    pianoRollButton.setClickingTogglesState(true);
    browserButton.setClickingTogglesState(true);
    inspectorButton.setTooltip("Show or hide Inspector");
    mixerButton.setTooltip("Show or hide Mixer");
    pianoRollButton.setTooltip("Show or hide Piano Roll");
    browserButton.setTooltip("Show or hide Media Browser");
    addTrackButton.setTooltip("Create new track");
    autoKeyButton.setTooltip("Analyze a beat or YouTube URL, then lock Key and Scale for Auto-Tune");
    inspectorButton.onClick = [this] { if (onInspectorToggle != nullptr) onInspectorToggle(); };
    mixerButton.onClick = [this] { if (onMixerToggle != nullptr) onMixerToggle(); };
    pianoRollButton.onClick = [this] { if (onPianoRollToggle != nullptr) onPianoRollToggle(); };
    browserButton.onClick = [this] { if (onBrowserToggle != nullptr) onBrowserToggle(); };
    autoKeyButton.onClick = [this] { if (onAutoKeyRequested != nullptr) onAutoKeyRequested(); };
    addTrackButton.onClick = [this]
    {
        // The modal owns type/count choices so the in-project command uses the
        // same validated creation flow as a new empty project.
        if (onCreateTrack != nullptr)
            onCreateTrack(TrackType::audio);
    };
    goToBeginningButton.setTooltip("Go to beginning (Return)");
    rewindButton.setTooltip("Move playhead back one bar");
    forwardButton.setTooltip("Move playhead forward one bar");
    goToBeginningButton.onClick = [this] { goToBeginning(); };
    rewindButton.onClick = [this] { rewindOneBar(); };
    forwardButton.onClick = [this] { forwardOneBar(); };
    pointerTool.setClickingTogglesState(true);
    scissorsTool.setClickingTogglesState(true);
    eraserTool.setClickingTogglesState(true);
    pointerTool.onClick = [this]
    {
        if (trackModel != nullptr) trackModel->setActiveTool(TrackDataModel::EditTool::Pointer);
        pointerTool.setToggleState(true, juce::dontSendNotification);
        scissorsTool.setToggleState(false, juce::dontSendNotification);
        eraserTool.setToggleState(false, juce::dontSendNotification);
    };
    scissorsTool.onClick = [this]
    {
        if (trackModel != nullptr) trackModel->setActiveTool(TrackDataModel::EditTool::Scissors);
        pointerTool.setToggleState(false, juce::dontSendNotification);
        scissorsTool.setToggleState(true, juce::dontSendNotification);
        eraserTool.setToggleState(false, juce::dontSendNotification);
    };
    eraserTool.onClick = [this]
    {
        if (trackModel != nullptr) trackModel->setActiveTool(TrackDataModel::EditTool::Eraser);
        pointerTool.setToggleState(false, juce::dontSendNotification);
        scissorsTool.setToggleState(false, juce::dontSendNotification);
        eraserTool.setToggleState(true, juce::dontSendNotification);
    };
    pointerTool.setToggleState(true, juce::dontSendNotification);
    playButton.onClick = [this]
    {
        if (audioEngine != nullptr)
            audioEngine->setPlaybackState(true);
    };
    stopButton.onClick = [this]
    {
        stopPlayback();
    };
    recordButton.onClick = [this] { toggleRecording(); };
    const auto configureModeButton = [this] (auto& button, WorkspaceMode mode, const juce::String& tooltip)
    {
        button.setTooltip(tooltip);
        button.onClick = [this, mode] { setWorkspaceMode(mode); };
    };
    configureModeButton(studioModeButton, WorkspaceMode::studio,
                        "Studio: recording, editing, mixing and mastering.");
    configureModeButton(stageModeButton, WorkspaceMode::stage,
                        "Stage: low-latency live-performance workspace.");
    configureModeButton(streamModeButton, WorkspaceMode::stream,
                        "Stream: vocal, backing and broadcast-output workspace.");
    countInButton.setClickingTogglesState(true);
    countInButton.setTooltip("Count-in before recording");
    countInButton.onClick = [this]
    {
        if (onCountInChanged != nullptr)
            onCountInChanged(countInButton.getToggleState());
    };
    countInBarsBox.addItem("1", 1);
    countInBarsBox.addItem("2", 2);
    countInBarsBox.setSelectedId(1, juce::dontSendNotification);
    countInBarsBox.setTooltip("Count-in length");
    countInBarsBox.onChange = [this]
    {
        if (onCountInBarsChanged != nullptr)
            onCountInBarsChanged(countInBarsBox.getSelectedId());
    };
    punchButton.setClickingTogglesState(true);
    punchButton.setTooltip("Enable punch recording. Uses the cycle range, or one bar at the playhead.");
    punchButton.onClick = [this]
    {
        if (onPunchChanged != nullptr)
            onPunchChanged(punchButton.getToggleState());
    };
    cycleButton.setClickingTogglesState(true);
    cycleButton.setTooltip("Repeat the active cycle range (C)");
    cycleButton.onClick = [this] { toggleCycle(); };
    metronomeButton.setClickingTogglesState(true);
    metronomeButton.setTooltip("Enable metronome click (K)");
    metronomeButton.onClick = [this]
    {
        if (audioEngine != nullptr)
            audioEngine->setMetronomeEnabled(metronomeButton.getToggleState());
    };

    for (auto* button : { &goToBeginningButton, &rewindButton, &playButton, &stopButton, &forwardButton, &recordButton,
                          &countInButton, &metronomeButton })
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff243139));
    punchButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffde9b42).withAlpha(0.18f));
    inspectorButton.setColour(juce::TextButton::buttonColourId, darkPanel);
    mixerButton.setColour(juce::TextButton::buttonColourId, darkPanel);
    pianoRollButton.setColour(juce::TextButton::buttonColourId, darkPanel);
    bpmSlider.setRange(60.0, 220.0, 1.0);
    bpmSlider.setValue(trackModel != nullptr ? trackModel->getBpm() : 120.0);
    bpmSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    bpmSlider.setColour(juce::Slider::trackColourId, accentBlue);
    bpmSlider.setColour(juce::Slider::thumbColourId, accentCyan);
    bpmSlider.onValueChange = [this]
    {
        if (audioEngine != nullptr)
            audioEngine->setTempo(bpmSlider.getValue());
        if (trackModel != nullptr)
            bpmLabel.setText(juce::String(juce::roundToInt(trackModel->getBpm())) + " BPM",
                             juce::dontSendNotification);
    };

    metronomeButton.setToggleState(audioEngine != nullptr && audioEngine->isMetronomeEnabled(), juce::dontSendNotification);
    setWorkspaceMode(WorkspaceMode::studio);
    bpmLabel.setText("120 BPM", juce::dontSendNotification);
    bpmLabel.setJustificationType(juce::Justification::centredRight);
    bpmLabel.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    bpmLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(bpmLabel);

    addAndMakeVisible(transportTimeLabel);

    startTimerHz(30);
}

void ControlBar::setInspectorVisible(bool visible) noexcept
{
    inspectorButton.setToggleState(visible, juce::dontSendNotification);
}

void ControlBar::setWorkspaceMode(WorkspaceMode mode) noexcept
{
    if (workspaceMode == mode
        && ((mode == WorkspaceMode::studio && studioModeButton.getToggleState())
            || (mode == WorkspaceMode::stage && stageModeButton.getToggleState())
            || (mode == WorkspaceMode::stream && streamModeButton.getToggleState())))
        return;

    workspaceMode = mode;
    const auto apply = [mode] (auto& button, WorkspaceMode candidate, juce::Colour accent)
    {
        const auto active = mode == candidate;
        button.setToggleState(active, juce::dontSendNotification);
        juce::ignoreUnused(accent);
    };
    apply(studioModeButton, WorkspaceMode::studio, StudioForgeTheme::accentBlue);
    apply(stageModeButton, WorkspaceMode::stage, juce::Colour(0xffc97839));
    apply(streamModeButton, WorkspaceMode::stream, juce::Colour(0xff3d9fc6));
    if (onWorkspaceModeChanged != nullptr)
        onWorkspaceModeChanged(mode);
    repaint();
}

void ControlBar::setMixerVisible(bool visible) noexcept
{
    mixerButton.setToggleState(visible, juce::dontSendNotification);
}

void ControlBar::setPianoRollVisible(bool visible) noexcept
{
    pianoRollButton.setToggleState(visible, juce::dontSendNotification);
}

void ControlBar::setBrowserVisible(bool visible) noexcept
{
    browserButton.setToggleState(visible, juce::dontSendNotification);
}

void ControlBar::togglePlayback() noexcept
{
    if (audioEngine != nullptr)
        audioEngine->setPlaybackState(! audioEngine->isPlaying());
}

void ControlBar::stopPlayback() noexcept
{
    if (audioEngine != nullptr)
        audioEngine->setPlaybackState(false);
}

void ControlBar::goToBeginning() noexcept
{
    if (audioEngine != nullptr)
        audioEngine->setPlaybackState(false);
    if (trackModel != nullptr)
        trackModel->setPlayheadPosition(0.0);
}

void ControlBar::rewindOneBar() noexcept
{
    if (trackModel == nullptr)
        return;
    const auto samplesPerBar = trackModel->getSampleRate() * 60.0
        / juce::jmax(1.0, trackModel->getTempoAtSample(trackModel->getPlayheadPosition()))
        * trackModel->getTimeSignatureNumerator();
    trackModel->setPlayheadPosition(trackModel->getPlayheadPosition() - samplesPerBar);
}

void ControlBar::forwardOneBar() noexcept
{
    if (trackModel == nullptr)
        return;
    const auto samplesPerBar = trackModel->getSampleRate() * 60.0
        / juce::jmax(1.0, trackModel->getTempoAtSample(trackModel->getPlayheadPosition()))
        * trackModel->getTimeSignatureNumerator();
    trackModel->setPlayheadPosition(trackModel->getPlayheadPosition() + samplesPerBar);
}

void ControlBar::toggleCycle() noexcept
{
    if (trackModel == nullptr)
        return;
    if (trackModel->isCycleActive())
    {
        trackModel->clearCycle();
        return;
    }
    const auto start = trackModel->getPlayheadPosition();
    const auto length = trackModel->getSampleRate() * 60.0 / juce::jmax(1.0, trackModel->getBpm())
        * trackModel->getTimeSignatureNumerator();
    trackModel->setCycle(start, start + length);
}

void ControlBar::toggleRecording()
{
    if (onRecordRequested != nullptr)
    {
        onRecordRequested();
        return;
    }

    if (audioEngine == nullptr)
        return;

    if (audioEngine->isRecording())
    {
        audioEngine->stopRecording();
    }
    else
    {
        if (trackModel == nullptr || trackModel->getTrackCount() == 0)
            return;
        const auto armedTrack = trackModel != nullptr ? trackModel->getFirstArmedTrackIndex() : -1;
        const auto targetTrack = static_cast<size_t>(juce::jmax(0, armedTrack));
        audioEngine->startRecording(targetTrack, juce::File::getSpecialLocation(juce::File::tempDirectory)
                                                  .getChildFile("StudioForgeRecording.wav"));
    }

    recordButton.setToggleState(audioEngine->isRecording() || audioEngine->isMidiRecording(), juce::dontSendNotification);
}

void ControlBar::setRecordActive(bool active) noexcept
{
    recordButton.setToggleState(active, juce::dontSendNotification);
}

void ControlBar::setRecordCountdown(bool active) noexcept
{
    recordButton.setTooltip(active ? "Recording starts after count-in" : "Record");
    recordButton.setToggleState(active, juce::dontSendNotification);
}

void ControlBar::timerCallback()
{
    const auto isPerforming = audioEngine != nullptr
        && (audioEngine->isPlaying() || audioEngine->isRecording() || audioEngine->isMidiRecording());
    updateLogoPlaybackGlow(isPerforming);

    if (audioEngine != nullptr)
        masterMeter.updateStereoPeak(audioEngine->getMasterLeftPeak(), audioEngine->getMasterRightPeak());
    playButton.setToggleState(audioEngine != nullptr && audioEngine->isPlaying(), juce::dontSendNotification);
    const auto recordingOrCountingIn = audioEngine != nullptr
        && (audioEngine->isRecording() || audioEngine->isMidiRecording() || recordButton.getToggleState());
    const auto pulse = recordingOrCountingIn
        ? 0.5f + 0.5f * std::sin(static_cast<float>(juce::Time::getMillisecondCounterHiRes() * 0.009))
        : 0.0f;
    recordButton.setPulsePhase(pulse);

    if (trackModel == nullptr)
        return;

    const auto sampleRate = trackModel->getSampleRate();
    const auto totalCentiseconds = sampleRate > 0.0
        ? static_cast<long long>(trackModel->getPlayheadPosition() * 100.0 / sampleRate + 0.5)
        : 0LL;
    const auto totalSeconds = totalCentiseconds / 100;
    transportTimeLabel.setText(juce::String::formatted("%02d:%02d:%02d.%02d",
                                  static_cast<int>(totalSeconds / 3600),
                                  static_cast<int>((totalSeconds / 60) % 60),
                                  static_cast<int>(totalSeconds % 60),
                                  static_cast<int>(totalCentiseconds % 100)),
                               juce::dontSendNotification);
    punchButton.setToggleState(trackModel->isPunchActive(), juce::dontSendNotification);
    cycleButton.setToggleState(trackModel->isCycleActive(), juce::dontSendNotification);
    metronomeButton.setToggleState(audioEngine != nullptr && audioEngine->isMetronomeEnabled(), juce::dontSendNotification);
    if (referenceConsoleStrip.isValid())
        repaint();
}

void ControlBar::updateLogoPlaybackGlow(bool shouldGlow) noexcept
{
    constexpr float glowStep = 0.10f;
    const auto targetGlow = shouldGlow ? 1.0f : 0.0f;
    const auto nextGlow = logoPlaybackGlow + (targetGlow - logoPlaybackGlow) * glowStep;
    if (std::abs(nextGlow - logoPlaybackGlow) < 0.002f)
        return;

    logoPlaybackGlow = nextGlow;
    // The rendered logo is wider than its historical 126 px repaint region.
    // Invalidating only the lettering left the trailing flourish after the
    // final "w" stale during the idle/illuminated crossfade. Repaint the
    // complete logo lane so every glyph and its tail receives the same glow.
    repaint(0, 0, juce::jmin(getWidth(), 220), getHeight());
}

void ControlBar::paint(juce::Graphics& g)
{
    if (referenceConsoleStrip.isValid())
    {
        g.fillAll(juce::Colour(0xff202426));
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(referenceConsoleStrip, referenceConsoleBounds,
                    juce::RectanglePlacement::stretchToFit, false);

        // The faceplate artwork supplies the physical panel.  Only the glass
        // readouts are redrawn, exactly inside their original apertures.
        constexpr float referenceWidth = 1916.0f;
        constexpr float referenceHeight = 130.0f;
        const auto xScale = referenceConsoleBounds.getWidth() / referenceWidth;
        const auto yScale = referenceConsoleBounds.getHeight() / referenceHeight;
        const auto aperture = [this, xScale, yScale] (float x, float y, float width, float height)
        {
            return juce::Rectangle<float>(referenceConsoleBounds.getX() + x * xScale,
                                          referenceConsoleBounds.getY() + y * yScale,
                                          width * xScale, height * yScale);
        };
        const auto clearLcd = [&g] (juce::Rectangle<float> bounds)
        {
            g.setColour(juce::Colour(0xff061215));
            g.fillRect(bounds);
        };
        const auto lcdText = [&g, yScale] (const juce::String& text, juce::Rectangle<float> bounds, float sourceFontSize)
        {
            g.setColour(juce::Colour(0xff55ddf4));
            g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                                    sourceFontSize * yScale, juce::Font::bold)));
            g.drawText(text, bounds.toNearestInt(), juce::Justification::centred, false);
        };

        if (trackModel != nullptr)
        {
            const auto barBeat = trackModel->getBarBeatInfo(trackModel->getPlayheadPosition());
            const auto tempo = trackModel->getTempoAtSample(trackModel->getPlayheadPosition());
            const auto clearAndDraw = [&] (const juce::String& value, float x, float width, float fontSize)
            {
                const auto bounds = aperture(x, 52, width, 48);
                clearLcd(bounds);
                lcdText(value, bounds, fontSize);
            };
            clearAndDraw(juce::String::formatted("%03d", barBeat.bars + 1), 663, 63, 30.0f);
            clearAndDraw(juce::String::formatted("%02d", barBeat.beats + 1), 739, 52, 30.0f);
            clearAndDraw(juce::String::formatted("%03d", barBeat.pulses), 813, 65, 30.0f);
            clearAndDraw(juce::String(tempo, 2), 920, 67, 21.0f);
            clearAndDraw(juce::String(trackModel->getTimeSignatureNumerator()) + "/4", 1019, 63, 21.0f);

            const auto sampleRate = trackModel->getSampleRate();
            const auto centiseconds = sampleRate > 0.0
                ? static_cast<long long>(trackModel->getPlayheadPosition() * 100.0 / sampleRate + 0.5) : 0LL;
            const auto seconds = centiseconds / 100;
            const auto timeBounds = aperture(1570, 45, 157, 47);
            clearLcd(timeBounds);
            lcdText(juce::String::formatted("%02d:%02d:%02d.%02d", static_cast<int>(seconds / 3600),
                                             static_cast<int>((seconds / 60) % 60), static_cast<int>(seconds % 60),
                                             static_cast<int>(centiseconds % 100)), timeBounds, 22.0f);
        }

        const auto drawMeterLane = [&] (juce::Rectangle<float> lane, float peak)
        {
            g.setColour(juce::Colour(0xff071114));
            g.fillRect(lane);
            constexpr int segments = 11;
            const auto segmentWidth = lane.getWidth() / static_cast<float>(segments);
            for (int index = 0; index < segments; ++index)
            {
                const auto threshold = static_cast<float>(index + 1) / static_cast<float>(segments);
                const auto active = peak >= threshold;
                const auto colour = index >= 9 ? juce::Colour(0xffffbe24)
                                  : index >= 7 ? juce::Colour(0xffb7e629)
                                               : juce::Colour(0xff34de60);
                const auto segment = juce::Rectangle<float>(lane.getX() + index * segmentWidth, lane.getY(),
                                                            segmentWidth - xScale * 2.0f, lane.getHeight());
                g.setColour(active ? colour : juce::Colour(0xff132429));
                g.fillRect(segment);
            }
        };
        const auto leftPeak = audioEngine != nullptr ? juce::jlimit(0.0f, 1.0f, audioEngine->getMasterLeftPeak()) : 0.0f;
        const auto rightPeak = audioEngine != nullptr ? juce::jlimit(0.0f, 1.0f, audioEngine->getMasterRightPeak()) : 0.0f;
        drawMeterLane(aperture(1800, 34, 86, 19), leftPeak);
        drawMeterLane(aperture(1800, 68, 86, 19), rightPeak);
        return;
    }
    const auto area = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a5053), area.getCentreX(), area.getY(),
                                            juce::Colour(0xff1a1e20), area.getCentreX(), area.getBottom(), false));
    g.fillAll();
    g.setColour(juce::Colours::black.withAlpha(0.88f));
    g.drawLine(area.getX(), area.getY(), area.getRight(), area.getY(), 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.17f));
    g.drawLine(area.getX(), area.getY() + 1.0f, area.getRight(), area.getY() + 1.0f, 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.85f));
    g.drawLine(area.getX(), area.getBottom() - 1.0f, area.getRight(), area.getBottom() - 1.0f, 1.0f);

    // The Live DAW marque occupies the fixed left edge. Its idle and lit
    // variants crossfade during playback/recording without affecting the
    // centred console layout.
    const auto logoHeight = juce::jmin(52, juce::jmax(32, getHeight() - 12));
    // Leave a deliberate breathing gap before the first workspace key. The
    // source logo contains transparent flourish pixels, so its visual edge is
    // wider than the nominal bounds and can otherwise touch Studio.
    const auto logoBounds = juce::Rectangle<int>(12, (getHeight() - logoHeight) / 2, 132, logoHeight);
    if (brandLogo.isValid())
    {
        // Standby is deliberately subdued. The illuminated layer below is
        // only blended in by timerCallback while playback/recording is real.
        g.setOpacity(0.36f);
        g.drawImageWithin(brandLogo, logoBounds.getX(), logoBounds.getY(), logoBounds.getWidth(), logoBounds.getHeight(),
                          juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    }
    if (illuminatedBrandLogo.isValid() && logoPlaybackGlow > 0.002f)
    {
        g.setOpacity(logoPlaybackGlow);
        g.drawImageWithin(illuminatedBrandLogo, logoBounds.getX(), logoBounds.getY(), logoBounds.getWidth(), logoBounds.getHeight(),
                          juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
        g.setOpacity(1.0f);
    }

    // No enclosing black slabs: LCD and meter paint their own purposeful
    // wells, while every button stays visually weightless until interaction.
}

void ControlBar::resized()
{
    if (referenceConsoleStrip.isValid())
    {
        constexpr float referenceWidth = 1916.0f;
        constexpr float referenceHeight = 130.0f;
        const auto aspectRatio = referenceWidth / referenceHeight;
        const auto artHeight = juce::jmin(static_cast<float>(getHeight()), static_cast<float>(getWidth()) / aspectRatio);
        const auto artWidth = artHeight * aspectRatio;
        referenceConsoleBounds = { (static_cast<float>(getWidth()) - artWidth) * 0.5f,
                                   (static_cast<float>(getHeight()) - artHeight) * 0.5f,
                                   artWidth, artHeight };
        const auto xScale = referenceConsoleBounds.getWidth() / referenceWidth;
        const auto yScale = referenceConsoleBounds.getHeight() / referenceHeight;
        const auto referenceBounds = [this, xScale, yScale] (float x, float y, float width, float height)
        {
            return juce::Rectangle<float>(referenceConsoleBounds.getX() + x * xScale,
                                          referenceConsoleBounds.getY() + y * yScale,
                                          width * xScale, height * yScale).toNearestInt();
        };

        // The logo is part of the reference artwork, so create breathing room
        // by shifting only the three workspace mode buttons. Keep the utility
        // controls at their original anchors to preserve the LCD spacing.
        constexpr float workspaceNudge = 28.0f;
        studioModeButton.setBounds(referenceBounds(20 + workspaceNudge, 21, 104, 85));
        stageModeButton.setBounds(referenceBounds(126 + workspaceNudge, 21, 99, 85));
        streamModeButton.setBounds(referenceBounds(228 + workspaceNudge, 21, 99, 85));
        metronomeButton.setBounds(referenceBounds(349, 21, 61, 65));
        countInButton.setBounds(referenceBounds(423, 21, 122, 65));
        autoKeyButton.setBounds(referenceBounds(552, 21, 88, 65));
        countInBarsBox.setVisible(false);
        transportLCD.setBounds(referenceBounds(650, 18, 453, 94));
        goToBeginningButton.setBounds(referenceBounds(1120, 21, 68, 65));
        rewindButton.setBounds(referenceBounds(1190, 21, 68, 65));
        playButton.setBounds(referenceBounds(1260, 21, 68, 65));
        stopButton.setBounds(referenceBounds(1330, 21, 68, 65));
        forwardButton.setBounds(referenceBounds(1400, 21, 68, 65));
        recordButton.setBounds(referenceBounds(1470, 21, 68, 65));
        transportTimeLabel.setBounds(referenceBounds(1545, 21, 205, 65));
        // Keep the L/R meter visually inside the faceplate instead of
        // touching its right edge; the source artwork has a narrow safety
        // margin that is easy to lose when the overlay is scaled.
        masterMeter.setBounds(referenceBounds(1755, 21, 149, 65));
        for (auto* component : { static_cast<juce::Component*>(&inspectorButton), static_cast<juce::Component*>(&mixerButton),
                                 static_cast<juce::Component*>(&pianoRollButton), static_cast<juce::Component*>(&browserButton),
                                 static_cast<juce::Component*>(&addTrackButton), static_cast<juce::Component*>(&punchButton),
                                 static_cast<juce::Component*>(&cycleButton), static_cast<juce::Component*>(&bpmSlider),
                                 static_cast<juce::Component*>(&bpmLabel), static_cast<juce::Component*>(&pointerTool),
                                 static_cast<juce::Component*>(&scissorsTool), static_cast<juce::Component*>(&eraserTool) })
            component->setVisible(false);
        autoKeyButton.setVisible(true);
        return;
    }
    // Keep the console as a centred instrument, not a row stretched to the
    // window edges.  This preserves the deliberate density of the reference
    // layout on ultrawide displays.
    if (getWidth() >= 1000)
    {
        // The console is an instrument panel, not a toolbar.  At desktop
        // widths it needs enough physical height for the glyphs and LED
        // numerals to read at a glance.
        // On Retina displays this is measured in logical points. Keep the
        // oversized layout for a truly wide canvas so the L/R meter fits.
        const auto wide = getWidth() >= 1750;
        const auto controlHeight = wide ? 58 : 48;
        const auto controlY = (getHeight() - controlHeight) / 2;
        const auto modeWidth = wide ? 86 : 72;
        const auto lcdWidth = wide ? 390 : 330;
        const auto timeWidth = wide ? 182 : 158;
        const auto meterWidth = wide ? 158 : 140;
        const auto transportKeyWidth = wide ? 44 : 36;
        const auto transportGap = wide ? 5 : 4;
        const auto transportWidth = transportKeyWidth * 6 + transportGap * 5;
        const auto metroWidth = wide ? 52 : 44;
        const auto countInWidth = wide ? 96 : 84;
        const auto outerGap = wide ? 30 : 24;
        constexpr auto modeGap = 9;
        constexpr auto countInGap = 4;
        const auto clusterNudge = wide ? 10 : 8;
        const auto countInKeyWidth = juce::jmin(countInWidth, controlHeight);
        // The Count-in component used to reserve a wide invisible tail after
        // its square 1234 key. Shift that reclaimed space into the left group
        // so the visible Count-in -> LCD gap equals LCD -> transport.
        const auto clusterRightShift = countInWidth - clusterNudge - countInKeyWidth + 14;
        const auto autoKeyWidth = wide ? 88 : 76;
        const auto leftWidth = modeWidth * 2 + (modeWidth + 10) + metroWidth + countInWidth + autoKeyWidth
            + modeGap * 3 + countInGap + 8;
        constexpr auto meterRightInset = 12;
        const auto consoleWidth = leftWidth + outerGap + lcdWidth + outerGap + transportWidth
            + 18 + timeWidth + 18 + meterWidth + meterRightInset;
        // Align the musical LCD with the start of the arrange ruler rather
        // than mathematical window centre; the left inspector is visually
        // heavier than the right edge of the arrange view.
        const auto consoleRightNudge = wide ? 70 : 62;
        const auto consoleX = juce::jmax(12, (getWidth() - consoleWidth) / 2 + consoleRightNudge);
        auto cursor = consoleX + clusterNudge + clusterRightShift;

        inspectorButton.setVisible(false); mixerButton.setVisible(false); pianoRollButton.setVisible(false);
        browserButton.setVisible(false); addTrackButton.setVisible(false); cycleButton.setVisible(false);
        punchButton.setVisible(false); bpmSlider.setVisible(false); bpmLabel.setVisible(false);
        pointerTool.setVisible(false); scissorsTool.setVisible(false); eraserTool.setVisible(false);
        studioModeButton.setVisible(true); stageModeButton.setVisible(true); streamModeButton.setVisible(true);
        metronomeButton.setVisible(true); countInButton.setVisible(true);
        studioModeButton.setBounds(cursor, controlY, modeWidth, controlHeight); cursor += modeWidth + modeGap;
        stageModeButton.setBounds(cursor, controlY, modeWidth, controlHeight); cursor += modeWidth + modeGap;
        streamModeButton.setBounds(cursor, controlY, modeWidth + 10, controlHeight); cursor += modeWidth + 10 + modeGap;
        metronomeButton.setBounds(cursor, controlY, metroWidth, controlHeight); cursor += metroWidth + countInGap;
        countInButton.setBounds(cursor, controlY, countInKeyWidth, controlHeight);
        cursor += countInKeyWidth + 8;
        autoKeyButton.setBounds(cursor, controlY, autoKeyWidth, controlHeight);
        autoKeyButton.setVisible(true);
        countInBarsBox.setVisible(false);
        cursor = consoleX + leftWidth + outerGap;
        transportLCD.setBounds(cursor, controlY, lcdWidth, controlHeight); cursor += lcdWidth + outerGap;
        goToBeginningButton.setBounds(cursor, controlY, transportKeyWidth, controlHeight); cursor += transportKeyWidth + transportGap;
        rewindButton.setBounds(cursor, controlY, transportKeyWidth, controlHeight); cursor += transportKeyWidth + transportGap;
        playButton.setBounds(cursor, controlY, transportKeyWidth, controlHeight); cursor += transportKeyWidth + transportGap;
        stopButton.setBounds(cursor, controlY, transportKeyWidth, controlHeight); cursor += transportKeyWidth + transportGap;
        forwardButton.setBounds(cursor, controlY, transportKeyWidth, controlHeight); cursor += transportKeyWidth + transportGap;
        recordButton.setBounds(cursor, controlY, transportKeyWidth, controlHeight); cursor += transportKeyWidth + 18;
        transportTimeLabel.setBounds(cursor, controlY, timeWidth, controlHeight); cursor += timeWidth + 18;
        // Keep the meter well visually separated from the window edge. The
        // transport/time cluster remains fixed; only the meter is nudged left.
        masterMeter.setBounds(cursor - 12, controlY + 5, juce::jmax(132, meterWidth - 4), controlHeight - 10);
        masterMeter.setVisible(true);
        return;
    }

    const auto bounds = getLocalBounds().reduced(10, 7);
    const auto buttonHeight = 42;

    inspectorButton.setVisible(false);
    mixerButton.setVisible(false);
    pianoRollButton.setVisible(false);
    browserButton.setVisible(false);
    addTrackButton.setVisible(false);
    autoKeyButton.setVisible(true);

    const auto rightControlsWidth = 0;
    const auto rightX = bounds.getRight() - rightControlsWidth;
    cycleButton.setVisible(false);
    countInButton.setVisible(true);
    punchButton.setVisible(false);
    metronomeButton.setVisible(true);
    countInBarsBox.setVisible(true);
    bpmSlider.setVisible(false);
    bpmLabel.setVisible(false);

    const auto wideLayout = getWidth() >= 1700;
    const auto lcdWidth = wideLayout ? 360 : 260;
    const auto meterWidth = wideLayout ? 176 : 0;
    constexpr auto performanceControlsWidth = 460;
    const auto transportWidth = 6 * 42 + 5 * 4;
    const auto timeWidth = 150;
    // The musical LCD is the optical centre of the console. Everything else
    // is arranged around it, rather than centring the uneven whole row.
    const auto lcdX = wideLayout ? bounds.getCentreX() - lcdWidth / 2 : bounds.getX() + performanceControlsWidth;
    const auto clusterX = lcdX - performanceControlsWidth - 16;
    studioModeButton.setBounds(clusterX, bounds.getY(), 72, buttonHeight);
    stageModeButton.setBounds(clusterX + 76, bounds.getY(), 72, buttonHeight);
    streamModeButton.setBounds(clusterX + 152, bounds.getY(), 78, buttonHeight);
    metronomeButton.setBounds(clusterX + 238, bounds.getY(), 42, buttonHeight);
    countInButton.setBounds(clusterX + 284, bounds.getY(), 42, buttonHeight);
    countInBarsBox.setBounds(clusterX + 330, bounds.getY() + 4, 42, buttonHeight - 8);
    autoKeyButton.setBounds(clusterX + 376, bounds.getY(), 80, buttonHeight);
    transportLCD.setBounds(lcdX, bounds.getY(), lcdWidth, buttonHeight);
    const auto transportX = lcdX + lcdWidth + 16;
    goToBeginningButton.setBounds(transportX, bounds.getY(), 42, buttonHeight);
    rewindButton.setBounds(transportX + 46, bounds.getY(), 42, buttonHeight);
    playButton.setBounds(transportX + 92, bounds.getY(), 42, buttonHeight);
    stopButton.setBounds(transportX + 138, bounds.getY(), 42, buttonHeight);
    forwardButton.setBounds(transportX + 184, bounds.getY(), 42, buttonHeight);
    recordButton.setBounds(transportX + 230, bounds.getY(), 42, buttonHeight);
    transportTimeLabel.setBounds(transportX + transportWidth + 16, bounds.getY(), timeWidth, buttonHeight);
    masterMeter.setBounds(transportX + transportWidth + timeWidth + 32, bounds.getY() + 4, meterWidth, buttonHeight - 8);
    masterMeter.setVisible(wideLayout);
    pointerTool.setBounds(bounds.getX() + 174, bounds.getY(), 68, buttonHeight);
    scissorsTool.setBounds(bounds.getX() + 246, bounds.getY(), 78, buttonHeight);
    eraserTool.setBounds(bounds.getX() + 328, bounds.getY(), 64, buttonHeight);

    // The transport has priority on compact windows; tools remain accessible
    // through their shortcuts rather than overlapping its LCD and buttons.
    pointerTool.setVisible(false);
    scissorsTool.setVisible(false);
    eraserTool.setVisible(false);
}
