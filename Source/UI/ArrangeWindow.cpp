#include "ArrangeWindow.h"

#include "../Media/Import/AudioImportSupport.h"
#include "../Media/MediaReloadService.h"
#include "../AudioEngine/AudioEngine.h"
#include "PerformanceRoomIconLibrary.h"
#include "Theme/StudioForgeLookAndFeel.h"
#include "Waveform/WaveformRenderer.h"
#include "TimelineNavigation/TimelineZoomPolicy.h"

#include <cmath>

namespace
{
const auto panelBackground = juce::Colour(0xff222222);
const auto panelAlt = juce::Colour(0xff303030);
const auto accentBlue = juce::Colour(0xff8098b5);
const auto accentCyan = juce::Colour(0xffb7cbe0);
constexpr double fineSnapInBeats = 1.0 / 16.0;
constexpr double tickSnapInBeats = 1.0 / 960.0;
constexpr float loopLocatorLaneHeight = 12.0f;
constexpr int timelineRulerHeight = 42;
constexpr double loopHandleWidth = 9.0;
constexpr float loopDragThreshold = 3.0f;
// Give the edges a forgiving hit target.  At the default zoom a 20-second
// region is wide, but at a fit-to-project zoom its visible edge can be only a
// few pixels from the pointer.  The edge gets priority over a region move.
constexpr float clipTrimHandleWidth = 16.0f;
constexpr float clipFadeHandleWidth = 12.0f;
constexpr float trackResizeHandleHeight = 5.0f;
// A console can span a large display.  A modest gain makes middle-mouse
// navigation cover that canvas without the hand having to travel its full
// width; Shift is the deliberate fast-navigation modifier.
constexpr double timelineDragPanGain = 1.65;
constexpr double timelineDragPanFastGain = 3.0;

juce::MouseCursor cursorForAudioRegionZone(AudioRegionMouseZone zone)
{
    switch (audioRegionCursorIntent(zone))
    {
        case AudioRegionCursorIntent::move: return juce::MouseCursor::DraggingHandCursor;
        case AudioRegionCursorIntent::trim: return juce::MouseCursor::LeftRightResizeCursor;
        // JUCE has no fade-specific system cursor. A pointing hand separates
        // the explicit Option/Alt fade gesture from ordinary edge trimming.
        case AudioRegionCursorIntent::fade: return juce::MouseCursor::PointingHandCursor;
        case AudioRegionCursorIntent::normal: break;
    }
    return juce::MouseCursor::NormalCursor;
}

juce::String zoomReadoutText(double zoom)
{
    return "Z " + juce::String(zoom, zoom < 10.0 ? 2 : zoom < 100.0 ? 1 : 0) + "x";
}

double snappedEditSample(const TrackDataModel& model, double rawSample,
                         juce::ModifierKeys modifiers) noexcept
{
    // Shift temporarily bypasses beat snap for sample-accurate alignment.
    return modifiers.isShiftDown() ? juce::jmax(0.0, rawSample)
                                  : model.getSnappedSamplePosition(rawSample, fineSnapInBeats);
}

bool isAudioTrack(const TrackDataModel& model, int trackIndex) noexcept
{
    return trackIndex >= 0
        && trackIndex < static_cast<int>(model.getTrackCount())
        && model.getTrack(static_cast<size_t>(trackIndex)).type == TrackType::audio;
}

double midiClipDurationSamples(const MidiClipState& clip, double sampleRate) noexcept
{
    if (clip.durationSamples > 0.0)
        return clip.durationSamples;

    auto duration = sampleRate * 0.5;
    for (const auto& note : clip.notes)
        duration = juce::jmax(duration, note.startSample + note.durationSamples);
    return duration;
}

PerformanceRole roleForTrackName(const juce::String& trackName, TrackType trackType)
{
    const auto name = trackName.toLowerCase();
    if (name.contains("vocal") || name.contains("voice")) return PerformanceRole::vocal;
    if (name.contains("guitar")) return PerformanceRole::guitar;
    if (name.contains("bass")) return PerformanceRole::bass;
    if (name.contains("key") || name.contains("piano")) return PerformanceRole::keyboard;
    if (name.contains("drum")) return PerformanceRole::drums;
    if (name.contains("beat") || name.contains("backing")) return PerformanceRole::beat;
    return trackType == TrackType::audio ? PerformanceRole::vocal : PerformanceRole::keyboard;
}

void drawPremiumWaveform(juce::Graphics& g, const AudioClipState& clip,
                         const WaveformThumbnail* thumbnail, juce::Rectangle<float> clipBounds,
                         WaveformRenderState state = {}, float amplitudeZoom = 1.0f)
{
    static const WaveformRenderer renderer;
    renderer.render(g, { clip, thumbnail, clipBounds, state, WaveformNormalization::clipAdaptive, amplitudeZoom });
}

std::shared_ptr<juce::AudioBuffer<float>> resampleForTimeline(
    std::shared_ptr<juce::AudioBuffer<float>> source, double sourceRate, double timelineRate)
{
    if (source == nullptr || sourceRate <= 0.0 || timelineRate <= 0.0
        || std::abs(sourceRate - timelineRate) < 0.01)
        return source;

    const auto outputSamples = juce::roundToInt(static_cast<double>(source->getNumSamples()) * timelineRate / sourceRate);
    if (outputSamples <= 0)
        return source;

    auto resampled = std::make_shared<juce::AudioBuffer<float>>(source->getNumChannels(), outputSamples);
    const auto ratio = sourceRate / timelineRate;
    for (int channel = 0; channel < source->getNumChannels(); ++channel)
    {
        // Lagrange needs a small look-ahead window. Pad with the final sample
        // so the final output frame cannot read beyond the decoded source.
        juce::AudioBuffer<float> padded(1, source->getNumSamples() + 4);
        padded.copyFrom(0, 0, *source, channel, 0, source->getNumSamples());
        padded.clear(0, source->getNumSamples(), 4);
        juce::LagrangeInterpolator interpolator;
        interpolator.process(ratio, padded.getReadPointer(0), resampled->getWritePointer(channel), outputSamples);
    }
    return resampled;
}

class AudioLoadJob final : public juce::ThreadPoolJob
{
public:
    AudioLoadJob(WaveformThumbnailCache* thumbnails, juce::File file,
                 double timelineSampleRate,
                 std::function<void(const juce::File&, std::shared_ptr<juce::AudioBuffer<float>>)> completion)
        : juce::ThreadPoolJob("Audio file loader"),
          waveformCache(thumbnails),
          sourceFile(std::move(file)),
          projectSampleRate(timelineSampleRate),
          onCompletion(std::move(completion))
    {
    }

    JobStatus runJob() override
    {
        std::shared_ptr<juce::AudioBuffer<float>> buffer;
        const auto decoded = MediaReloadService::decode(sourceFile, buffer).wasOk();
        if (decoded)
        {
            const auto metadata = AudioImportSupport::readMetadata(sourceFile);
            buffer = resampleForTimeline(std::move(buffer), metadata.sampleRate, projectSampleRate);
        }
        if (decoded && waveformCache != nullptr)
            waveformCache->prepare(sourceFile, buffer);

        auto file = sourceFile;
        juce::MessageManager::callAsync([completion = onCompletion, file, buffer]
        {
            if (completion != nullptr)
                completion(file, buffer);
        });
        return jobHasFinished;
    }

private:
    WaveformThumbnailCache* waveformCache = nullptr;
    juce::File sourceFile;
    double projectSampleRate = 44100.0;
    std::function<void(const juce::File&, std::shared_ptr<juce::AudioBuffer<float>>)> onCompletion;
};

class WaveformPreparationJob final : public juce::ThreadPoolJob
{
public:
    WaveformPreparationJob(juce::Component::SafePointer<ArrangeWindow> owner,
                           WaveformThumbnailCache* thumbnails, juce::File file,
                           std::shared_ptr<juce::AudioBuffer<float>> buffer,
                           std::function<void()> completion)
        : juce::ThreadPoolJob("Waveform thumbnail preparation"), safeOwner(owner),
          waveformCache(thumbnails), sourceFile(std::move(file)), decodedBuffer(std::move(buffer)),
          onCompletion(std::move(completion))
    {
    }

    JobStatus runJob() override
    {
        if (waveformCache != nullptr)
            waveformCache->prepare(sourceFile, decodedBuffer);
        juce::MessageManager::callAsync([completion = onCompletion]
        {
            if (completion != nullptr)
                completion();
        });
        return jobHasFinished;
    }

private:
    juce::Component::SafePointer<ArrangeWindow> safeOwner;
    WaveformThumbnailCache* waveformCache = nullptr;
    juce::File sourceFile;
    std::shared_ptr<juce::AudioBuffer<float>> decodedBuffer;
    std::function<void()> onCompletion;
};
}

TrackHeaderPanel::TrackHeaderPanel(TrackDataModel* model, AudioEngine* engine)
    : trackModel(model), audioEngine(engine)
{
    for (size_t index = 0; index < volumeControls.size(); ++index)
    {
        auto& volume = volumeControls[index];
        volume.setRange(0.0, AudioGainRange::maximumChannelGain, 0.01);
        volume.setSliderStyle(juce::Slider::LinearBar);
        volume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        volume.setTooltip("Track volume");
        volume.onValueChange = [this, index]
        {
            if (trackModel != nullptr && index < trackModel->getTrackCount())
                trackModel->setTrackVolume(index, static_cast<float>(volumeControls[index].getValue()));
        };
        addAndMakeVisible(volume);

        auto& pan = panControls[index];
        pan.setRange(-1.0, 1.0, 0.01);
        pan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        pan.setTooltip("Track pan");
        pan.onValueChange = [this, index]
        {
            if (trackModel != nullptr && index < trackModel->getTrackCount())
                trackModel->setTrackPan(index, static_cast<float>(panControls[index].getValue()));
        };
        addAndMakeVisible(pan);
    }
    startTimerHz(30);
}

void TrackHeaderPanel::timerCallback()
{
    if (trackModel != nullptr)
    {
        if (laidOutTrackCount != trackModel->getTrackCount() || laidOutTrackHeight != trackModel->getTrackHeight())
            resized();
        for (size_t index = 0; index < trackModel->getTrackCount() && index < volumeControls.size(); ++index)
        {
            const auto& track = trackModel->getTrack(index);
            volumeControls[index].setValue(track.volume.load(std::memory_order_relaxed), juce::dontSendNotification);
            panControls[index].setValue(track.pan.load(std::memory_order_relaxed), juce::dontSendNotification);
        }
    }
    repaint();
}

void TrackHeaderPanel::resized()
{
    const auto rowHeight = trackModel != nullptr ? trackModel->getTrackHeight() : StudioForgeTheme::UIMetrics::trackHeaderHeight;
    laidOutTrackCount = trackModel != nullptr ? trackModel->getTrackCount() : 0;
    laidOutTrackHeight = rowHeight;
    for (size_t index = 0; index < volumeControls.size(); ++index)
    {
        const auto visible = trackModel != nullptr && index < trackModel->getTrackCount();
        volumeControls[index].setVisible(visible);
        panControls[index].setVisible(visible);
        if (! visible)
            continue;
        const auto rowY = static_cast<int>(index) * rowHeight - verticalScrollOffset;
        const auto compact = rowHeight < 52;
        panControls[index].setVisible(visible && ! compact);
        volumeControls[index].setVisible(visible && ! compact);
        if (! compact)
        {
            // Reserve a dedicated right-hand meter column. The pan knob and
            // fader never enter that column, even at narrow inspector widths.
            constexpr int meterColumnWidth = 24;
            constexpr int panWidth = 28;
            constexpr int controlGap = 8;
            const auto panX = getWidth() - meterColumnWidth - controlGap - panWidth;
            panControls[index].setBounds(panX, rowY + 7, panWidth, 24);
            volumeControls[index].setBounds(136, rowY + 37,
                                             juce::jmax(0, panX - controlGap - 136), 10);
        }
    }
}

void TrackHeaderPanel::paint(juce::Graphics& g)
{
    g.fillAll(panelBackground);

    if (trackModel == nullptr)
        return;

    const auto rowHeight = static_cast<float>(trackModel->getTrackHeight());
    const auto count = static_cast<int>(trackModel->getTrackCount());
    constexpr float trackNumberWidth = 28.0f;
    for (int index = 0; index < count; ++index)
    {
        const auto row = juce::Rectangle<float>(0.0f, static_cast<float>(index) * rowHeight - verticalScrollOffset, static_cast<float>(getWidth()), rowHeight);
        const auto isSelected = index == selectedTrack;
        g.setColour(isSelected ? StudioForgeTheme::raisedSurface.brighter(0.05f)
                                           : (index % 2 == 0 ? StudioForgeTheme::panelBackground : StudioForgeTheme::panelBackground.darker(0.045f)));
        g.fillRect(row);
        if (index == audioFileDropTarget)
        {
            g.setColour(StudioForgeTheme::accentCyan.withAlpha(0.16f));
            g.fillRect(row.reduced(1.0f));
            g.setColour(StudioForgeTheme::accentCyan.withAlpha(0.92f));
            g.drawRoundedRectangle(row.reduced(1.5f), 3.0f, 1.5f);
        }
        g.setColour(StudioForgeTheme::separator.withAlpha(0.72f));
        g.drawVerticalLine(juce::roundToInt(trackNumberWidth), row.getY(), row.getBottom());
        g.setColour(StudioForgeTheme::secondaryText);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(juce::String(index + 1), row.withWidth(trackNumberWidth), juce::Justification::centred, false);

        const auto& track = trackModel->getTrack(static_cast<size_t>(index));
        const auto colour = track.type == TrackType::instrument ? juce::Colour(0xff35a866)
            : track.type == TrackType::externalMidi ? juce::Colour(0xff9167d4)
            : juce::Colour(0xff3d8ed7);
        g.setColour(StudioForgeTheme::separator.withAlpha(0.75f));
        g.drawHorizontalLine(row.getBottom() - 1.0f, row.getX(), row.getRight());
        g.setColour(colour);
        g.fillRect(trackNumberWidth + 3.0f, row.getY() + 2.0f, 3.0f, row.getHeight() - 4.0f);
        if (isSelected)
        {
            g.setColour(StudioForgeTheme::accentCyan.withAlpha(0.82f));
            g.fillRect(trackNumberWidth + 3.0f, row.getY() + 2.0f, 2.0f, row.getHeight() - 4.0f);
        }
        g.fillRoundedRectangle(trackNumberWidth + 10.0f, row.getY() + 14.0f, 22.0f, 22.0f, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(track.type == TrackType::instrument ? "♪" : track.type == TrackType::externalMidi ? "M" : "~",
                   trackNumberWidth + 10.0f, row.getY() + 14.0f, 22.0f, 22.0f, juce::Justification::centred, false);
        g.drawText(track.name.isNotEmpty() ? track.name : "Track " + juce::String(index + 1),
                   trackNumberWidth + 37.0f, row.getY() + 9.0f, row.getWidth() - 100.0f, 17.0f, juce::Justification::centredLeft, true);
        g.setColour(colour);
        g.fillRoundedRectangle(trackNumberWidth + 10.0f, row.getY() + 14.0f, 22.0f, 22.0f, 3.0f);
        PerformanceRoomIconLibrary::draw(g,
                                         { trackNumberWidth + 13.0f, row.getY() + 17.0f, 16.0f, 16.0f },
                                         roleForTrackName(track.name, track.type));
        const auto buttonY = row.getHeight() < 52.0f ? row.getY() + row.getHeight() - 19.0f : row.getY() + 34.0f;
        const auto arm = juce::Rectangle<float>(trackNumberWidth + 37.0f, buttonY, 16.0f, 16.0f);
        const auto monitor = arm.translated(18.0f, 0.0f);
        const auto mute = track.type == TrackType::audio ? monitor.translated(18.0f, 0.0f) : arm.translated(18.0f, 0.0f);
        const auto solo = mute.translated(18.0f, 0.0f);
        const auto armed = track.armed.load(std::memory_order_relaxed);
        const auto recording = audioEngine != nullptr
            && (audioEngine->isRecording() || audioEngine->isMidiRecording());
        const auto blinkOn = (juce::Time::getMillisecondCounter() / 420u) % 2u == 0u;
        // Armed means record-ready, not recording. Blinking only during a
        // real take prevents a Live-ready session from looking as if it is
        // already recording.
        g.setColour(armed ? StudioForgeTheme::recordRed.withAlpha(recording && ! blinkOn ? 0.48f : 1.0f)
                          : juce::Colour(0xff333333));
        g.fillRoundedRectangle(arm, 2.0f);
        if (armed)
        {
            g.setColour(StudioForgeTheme::recordRed.brighter(0.28f));
            g.drawRoundedRectangle(arm, 2.0f, 1.0f);
        }
        if (track.type == TrackType::audio)
        {
            const auto isAlwaysOn = track.inputMonitoring.load(std::memory_order_relaxed);
            const auto isAuto = track.autoInputMonitoring.load(std::memory_order_relaxed);
            const auto live = audioEngine != nullptr && audioEngine->isLivePerformanceEnabled();
            const auto routingToOutput = isAlwaysOn || (isAuto && armed);
            const auto permitted = track.monitorPermitted.load(std::memory_order_relaxed);
            const auto active = routingToOutput && permitted && live
                && track.monitorDestination != MonitorDestination::off;
            g.setColour(active ? juce::Colour(0xff23c6a0)
                         : isAlwaysOn ? juce::Colour(0xffff9f32)
                         : isAuto ? juce::Colour(0xffb66b27).withAlpha(0.72f)
                                  : juce::Colour(0xff333333));
            g.fillRoundedRectangle(monitor, 2.0f);
            if (isAlwaysOn || isAuto)
            {
                g.setColour((active ? juce::Colour(0xffa6ffe8)
                            : isAlwaysOn ? juce::Colour(0xffffc06a) : juce::Colour(0xffff9f32)).withAlpha(0.95f));
                g.drawRoundedRectangle(monitor, 2.0f, 1.0f);
            }
        }
        g.setColour(track.muted.load(std::memory_order_relaxed) ? StudioForgeTheme::muteAmber : juce::Colour(0xff333333));
        g.fillRoundedRectangle(mute, 2.0f);
        g.setColour(track.solo.load(std::memory_order_relaxed) ? StudioForgeTheme::soloYellow : juce::Colour(0xff333333));
        g.fillRoundedRectangle(solo, 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.drawText("R", arm, juce::Justification::centred, true);
        if (track.type == TrackType::audio) g.drawText("I", monitor, juce::Justification::centred, true);
        g.drawText("M", mute, juce::Justification::centred, true);
        g.drawText("S", solo, juce::Justification::centred, true);

        const auto meterHeight = juce::jmax(8.0f, row.getHeight() - 18.0f);
        const auto inputMeter = juce::Rectangle<float>(row.getRight() - 22.0f, row.getY() + 12.0f, 5.0f, meterHeight);
        const auto outputMeter = inputMeter.translated(8.0f, 0.0f);
        const auto inputPeak = audioEngine != nullptr && track.type == TrackType::audio
            ? audioEngine->getTrackInputPeak(static_cast<size_t>(index)) : 0.0f;
        const auto outputPeak = audioEngine != nullptr ? audioEngine->getTrackPeak(static_cast<size_t>(index)) : 0.0f;
        const auto drawMeter = [&g] (juce::Rectangle<float> meter, float peak, juce::Colour lowColour)
        {
            const auto lit = juce::jlimit(0.0f, 1.0f,
                (juce::Decibels::gainToDecibels(peak, -60.0f) + 60.0f) / 60.0f);
            constexpr int segments = 10;
            for (int segment = 0; segment < segments; ++segment)
            {
                const auto fraction = static_cast<float>(segment + 1) / segments;
                const auto segmentHeight = (meter.getHeight() - (segments - 1)) / segments;
                const auto rect = juce::Rectangle<float>(meter.getX(), meter.getBottom() - fraction * meter.getHeight(),
                                                         meter.getWidth(), segmentHeight);
                const auto colour = fraction > 0.95f ? juce::Colour(0xffff4d55)
                                  : fraction > 0.80f ? juce::Colour(0xffffb13b) : lowColour;
                g.setColour(fraction <= lit ? colour : juce::Colour(0xff17211d));
                g.fillRoundedRectangle(rect, 1.0f);
            }
        };
        drawMeter(inputMeter, inputPeak, juce::Colour(0xff4bbcf4));
        drawMeter(outputMeter, outputPeak, juce::Colour(0xff42dc82));
    }

}

TimelineGrid::TimelineGrid(TrackDataModel* model)
    : trackModel(model)
{
    viewportController.onChanged = [this] (const TimelineViewportState& state)
    {
        viewStartSample = state.viewStartSample;
        waveformZoom = state.waveformZoom;
        if (trackModel != nullptr)
        {
            trackModel->setHorizontalZoom(state.horizontalZoom);
            trackModel->setTrackHeight(state.trackHeight);
        }
        if (onViewChanged != nullptr)
            onViewChanged(viewStartSample, state.horizontalZoom);
        if (onWaveformZoomChanged != nullptr)
            onWaveformZoomChanged(waveformZoom);
        repaint();
    };
    if (trackModel != nullptr)
        viewportController.setState({ viewStartSample, trackModel->getHorizontalZoom(), trackModel->getTrackHeight(), 1.0f }, false);
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

void TrackHeaderPanel::mouseDown(const juce::MouseEvent& event)
{
    if (trackModel == nullptr)
        return;

    const auto rowHeight = juce::jmax(1, trackModel->getTrackHeight());
    const auto track = static_cast<int>((event.position.y + verticalScrollOffset) / rowHeight);
    if (track < 0 || track >= static_cast<int>(trackModel->getTrackCount()))
        return;

    pressedTrack = track;
    reorderPreviewTrack = track;
    draggingTrack = false;
    resizingTrackHeight = ! event.mods.isPopupMenu()
        && event.position.y + verticalScrollOffset
            >= static_cast<float>((track + 1) * rowHeight) - trackResizeHandleHeight;
    dragStartTrackHeight = rowHeight;
    dragStartY = event.position.y;
    if (resizingTrackHeight)
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    const auto rowY = static_cast<float>(track * rowHeight - verticalScrollOffset);
    const auto& state = trackModel->getTrack(static_cast<size_t>(track));
    constexpr float trackNumberWidth = 28.0f;
    const auto buttonY = rowHeight < 52 ? rowY + rowHeight - 19.0f : rowY + 34.0f;
    const auto arm = juce::Rectangle<float>(trackNumberWidth + 37.0f, buttonY, 16.0f, 16.0f);
    const auto monitor = arm.translated(18.0f, 0.0f);
    const auto mute = state.type == TrackType::audio ? monitor.translated(18.0f, 0.0f) : arm.translated(18.0f, 0.0f);
    const auto solo = mute.translated(18.0f, 0.0f);

    if (event.mods.isPopupMenu())
    {
        selectedTrack = track;
        repaint();
        if (onTrackSelected != nullptr)
            onTrackSelected(track);

        juce::PopupMenu menu;
        menu.addItem(1, "New Audio Track Below");
        menu.addItem(2, "New Software Instrument Below");
        menu.addItem(3, "New External MIDI Track Below");
        menu.addItem(4, "New Aux Bus");
        menu.addSeparator();
        menu.addItem(5, "Duplicate Track");
        menu.addItem(6, "Delete Track", trackModel->getTrackCount() > 1);
        const auto safeOwner = juce::Component::SafePointer<TrackHeaderPanel>(this);
        const auto clickPosition = event.getScreenPosition().roundToInt();
        menu.showMenuAsync(juce::PopupMenu::Options()
                               .withTargetComponent(this)
                               .withTargetScreenArea({ clickPosition.x, clickPosition.y, 1, 1 }),
                           [safeOwner, track] (int choice)
        {
            if (safeOwner == nullptr || safeOwner->trackModel == nullptr || choice == 0)
                return;

            auto& model = *safeOwner->trackModel;
            const auto createBelow = [&model, track] (TrackType type)
            {
                const auto created = model.addTrack(type);
                if (created.isValid() && track + 1 < static_cast<int>(model.getTrackCount()))
                    model.reorderTrack(created, static_cast<size_t>(track + 1));
                return created;
            };

            TrackId selected;
            if (choice >= 1 && choice <= 3)
                selected = createBelow(choice == 1 ? TrackType::audio
                                       : choice == 2 ? TrackType::instrument : TrackType::externalMidi);
            else if (choice == 4)
                model.addBus();
            else if (choice == 5 && track < static_cast<int>(model.getTrackCount()))
            {
                const auto& source = model.getTrack(static_cast<size_t>(track));
                selected = createBelow(source.type);
                if (selected.isValid())
                {
                    const auto index = model.getTrackIndex(selected);
                    if (index >= 0)
                    {
                        model.setTrackVolume(static_cast<size_t>(index), source.volume.load(std::memory_order_relaxed));
                        model.setTrackPan(static_cast<size_t>(index), source.pan.load(std::memory_order_relaxed));
                    }
                }
            }
            else if (choice == 6 && track < static_cast<int>(model.getTrackCount()))
                model.removeTrack(model.getTrackId(static_cast<size_t>(track)));

            if (selected.isValid())
            {
                safeOwner->selectedTrack = model.getTrackIndex(selected);
                if (safeOwner->onTrackSelected != nullptr)
                    safeOwner->onTrackSelected(safeOwner->selectedTrack);
            }
            safeOwner->repaint();
        });
        return;
    }

    if (arm.contains(event.position))
        trackModel->setTrackArmed(static_cast<size_t>(track), ! state.armed.load(std::memory_order_relaxed));
    else if (monitor.contains(event.position) && state.type == TrackType::audio)
    {
        const auto index = static_cast<size_t>(track);
        const auto enable = ! state.inputMonitoring.load(std::memory_order_relaxed);
        trackModel->setTrackInputMonitoring(index, enable);
        if (! enable)
        {
            if (audioEngine != nullptr)
                audioEngine->disableTrackMonitor(index);
        }
        else if (audioEngine != nullptr)
        {
            // The header I button is the fast, user-facing microphone switch.
            // Output topology is selected by the engine, never by the user.
            if (const auto result = audioEngine->enableTrackMonitor(index, MonitorDestination::master); result.failed())
            {
                trackModel->setTrackMonitorPermission(index, false);
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Microphone monitor blocked", result.getErrorMessage());
            }
        }
    }
    else if (mute.contains(event.position))
        trackModel->setTrackMuted(static_cast<size_t>(track), ! state.muted.load(std::memory_order_relaxed));
    else if (solo.contains(event.position))
        trackModel->setTrackSolo(static_cast<size_t>(track), ! state.solo.load(std::memory_order_relaxed));

    selectedTrack = track;
    repaint();
    if (onTrackSelected != nullptr)
        onTrackSelected(track);
}

void TrackHeaderPanel::mouseMove(const juce::MouseEvent& event)
{
    if (trackModel == nullptr)
        return;

    const auto rowHeight = juce::jmax(1, trackModel->getTrackHeight());
    const auto row = static_cast<int>((event.position.y + verticalScrollOffset) / rowHeight);
    const auto nearBoundary = row >= 0 && row < static_cast<int>(trackModel->getTrackCount())
        && event.position.y + verticalScrollOffset >= static_cast<float>((row + 1) * rowHeight) - trackResizeHandleHeight;
    setMouseCursor(nearBoundary ? juce::MouseCursor::UpDownResizeCursor
                                : juce::MouseCursor::NormalCursor);
}

void TrackHeaderPanel::mouseDrag(const juce::MouseEvent& event)
{
    if (trackModel == nullptr || pressedTrack < 0)
        return;

    if (resizingTrackHeight)
    {
        const auto height = juce::roundToInt(static_cast<float>(dragStartTrackHeight)
                                             + event.position.y - dragStartY);
        trackModel->setTrackHeight(height);
        resized();
        repaint();
        return;
    }

    // Track rows are deliberately not draggable.  Reordering shared the same
    // drag gesture as region edits and made edge-trimming unreliable.
}

void TrackHeaderPanel::mouseUp(const juce::MouseEvent&)
{
    pressedTrack = -1;
    reorderPreviewTrack = -1;
    draggingTrack = false;
    resizingTrackHeight = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void TrackHeaderPanel::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (trackModel == nullptr || trackModel->getTrackCount() >= TrackDataModel::maxTracks)
        return;

    const auto rowHeight = juce::jmax(1, trackModel->getTrackHeight());
    const auto contentY = event.position.y + static_cast<float>(verticalScrollOffset);
    const auto trackCount = static_cast<int>(trackModel->getTrackCount());
    if (contentY < static_cast<float>(trackCount * rowHeight))
        return;
    const auto anchor = trackCount - 1;
    const auto type = trackModel->getTrack(static_cast<size_t>(anchor)).type;
    const auto created = trackModel->addTrack(type);
    if (! created.isValid())
        return;
    if (anchor + 1 < static_cast<int>(trackModel->getTrackCount()))
        trackModel->reorderTrack(created, static_cast<size_t>(anchor + 1));
    selectedTrack = trackModel->getTrackIndex(created);
    if (onTrackSelected != nullptr)
        onTrackSelected(selectedTrack);
    repaint();
}

void TimelineGrid::timerCallback()
{
    if (trackModel == nullptr)
        return;

    if (edgeAutoScrollActive)
        updateEdgeAutoScroll();

    // Sample the realtime input meter at UI rate for a responsive in-progress
    // waveform; after stop the decoded clip supplies the full waveform.
    if (recordingPreviewTrack >= 0 && audioEngine != nullptr)
    {
        recordingEnvelope[recordingEnvelopeWrite] = audioEngine->getTrackInputPeak(
            static_cast<size_t>(recordingPreviewTrack));
        recordingEnvelopeWrite = (recordingEnvelopeWrite + 1) % recordingEnvelope.size();
        recordingEnvelopeCount = juce::jmin(recordingEnvelopeCount + 1, recordingEnvelope.size());
        repaint();
    }

    if (! trackModel->isPlaying())
        return;

    followPlaybackPosition(trackModel->getPlayheadPosition());
}

void TimelineGrid::followPlaybackPosition(double samplePosition)
{
    if (trackModel == nullptr || getWidth() <= 0)
        return;

    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto playheadX = trackModel->sampleToXPosition(samplePosition - viewStartSample, pixelsPerSecond);
    const auto followEdge = static_cast<double>(getWidth()) * 0.72;
    if (playheadX < 0.0 || playheadX > followEdge)
    {
        const auto lookBehindSeconds = static_cast<double>(getWidth()) * 0.28 / pixelsPerSecond;
        updateView(samplePosition - lookBehindSeconds * trackModel->getSampleRate(),
                   trackModel->getHorizontalZoom());
    }
}

void TimelineGrid::setViewStartSample(double sample) noexcept
{
    viewStartSample = juce::jmax(0.0, sample);
    repaint();
}

void TimelineGrid::setTrackHeight(int height)
{
    auto next = viewportController.state();
    next.trackHeight = height;
    viewportController.setState(next);
}

void TimelineGrid::setWaveformZoom(float zoom)
{
    auto next = viewportController.state();
    next.waveformZoom = zoom;
    viewportController.setState(next);
}

void TimelineGrid::zoomHorizontalBy(float multiplier, float pivotX)
{
    if (trackModel == nullptr || multiplier <= 0.0f)
        return;
    const auto pivot = viewStartSample + static_cast<double>(pivotX) / (80.0 * trackModel->getHorizontalZoom()) * trackModel->getSampleRate();
    viewportController.setState(TimelineZoomPolicy::zoomAroundSample(viewportController.state(), pivot,
        trackModel->getHorizontalZoom() * multiplier, 80.0, trackModel->getSampleRate(), pivotX));
}

void TimelineGrid::zoomVerticalBy(int delta)
{
    auto next = viewportController.state();
    next.trackHeight = juce::jlimit(28, 160, next.trackHeight + delta);
    viewportController.setState(next);
}

void TimelineGrid::zoomVerticalByFactor(float multiplier)
{
    if (multiplier <= 0.0f)
        return;
    auto next = viewportController.state();
    next.trackHeight = juce::jlimit(28, 160,
                                    juce::roundToInt(static_cast<float>(next.trackHeight) * multiplier));
    viewportController.setState(next);
}

void TimelineGrid::saveZoomPreset(int slot) noexcept
{
    zoomPresetStore.save(slot, viewportController.state());
}

bool TimelineGrid::recallZoomPreset(int slot)
{
    if (const auto preset = zoomPresetStore.recall(slot))
    {
        selectedTrackZoomed = false;
        viewportController.setState(*preset);
        return true;
    }
    return false;
}

bool TimelineGrid::navigateZoomHistory(bool forward)
{
    return forward ? viewportController.forward() : viewportController.back();
}

void TimelineGrid::setRecordingPreview(int trackIndex, double startSample) noexcept
{
    recordingPreviewTrack = trackIndex;
    recordingPreviewStartSample = juce::jmax(0.0, startSample);
    recordingEnvelopeWrite = 0;
    recordingEnvelopeCount = 0;
    recordingEnvelope.fill(0.0f);
    repaint();
}

double TimelineGrid::sampleAt(const juce::Point<float>& position) const noexcept
{
    if (trackModel == nullptr)
        return 0.0;
    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    return viewStartSample + static_cast<double>(position.x) / pixelsPerSecond * trackModel->getSampleRate();
}

int TimelineGrid::trackAt(const juce::Point<float>& position) const noexcept
{
    if (trackModel == nullptr || trackModel->getTrackCount() == 0)
        return -1;
    const auto rowHeight = juce::jmax(1, trackModel->getTrackHeight());
    const auto track = static_cast<int>((position.y + verticalScrollOffset) / rowHeight);
    return track >= 0 && track < static_cast<int>(trackModel->getTrackCount()) ? track : -1;
}

double TimelineGrid::editSample(double rawSample, const juce::ModifierKeys& modifiers) const noexcept
{
    if (trackModel == nullptr)
        return 0.0;
    // Live layout defaults to free timing. Cmd/Ctrl is also an explicit snap
    // bypass, keeping the gesture portable between macOS and Windows.
    const auto bypass = modifiers.isCommandDown() || modifiers.isCtrlDown() || modifiers.isShiftDown();
    return ! snapEnabled || bypass ? juce::jmax(0.0, rawSample)
                                  : trackModel->getSnappedSamplePosition(rawSample, fineSnapInBeats);
}

void TimelineGrid::updateView(double startSample, float zoom)
{
    if (trackModel == nullptr)
        return;
    auto next = viewportController.state();
    next.viewStartSample = juce::jmax(0.0, startSample);
    next.horizontalZoom = zoom;
    viewportController.setState(next);
}

void TimelineGrid::updateVerticalScroll(int offset)
{
    if (trackModel == nullptr)
        return;
    const auto contentHeight = static_cast<int>(trackModel->getTrackCount()) * trackModel->getTrackHeight();
    const auto maximumOffset = juce::jmax(0, contentHeight - getHeight());
    verticalScrollOffset = juce::jlimit(0, maximumOffset, offset);
    if (onVerticalScrollChanged != nullptr)
        onVerticalScrollChanged(verticalScrollOffset);
    repaint();
}

TrackId TimelineGrid::createTrackAfter(TrackType type, int anchorTrack)
{
    if (trackModel == nullptr || trackModel->getTrackCount() >= TrackDataModel::maxTracks)
        return {};
    const auto created = trackModel->addTrack(type);
    if (created.isValid() && anchorTrack >= 0 && anchorTrack + 1 < static_cast<int>(trackModel->getTrackCount()))
        trackModel->reorderTrack(created, static_cast<size_t>(anchorTrack + 1));
    return created;
}

void TimelineGrid::showContextMenu(const juce::MouseEvent& event, int track, ClipId clip, double sampleAtMouse)
{
    if (trackModel == nullptr)
        return;

    juce::PopupMenu menu;
    if (clip.isValid())
    {
        menu.addItem(1, "Split at Mouse Position");
        menu.addItem(2, "Split at Playhead");
        menu.addItem(3, "Duplicate");
        menu.addItem(4, "Delete");
        menu.addSeparator();
        menu.addItem(5, "Reset Fades");
        menu.addItem(6, "Create Crossfades for Overlaps");
        auto loopEnabled = false;
        if (track >= 0 && track < static_cast<int>(trackModel->getTrackCount()))
            for (const auto& candidate : trackModel->getTrack(static_cast<size_t>(track)).clips)
                if (candidate.id == clip) { loopEnabled = candidate.loopEnabled; break; }
        menu.addItem(7, loopEnabled ? "Disable Loop" : "Enable Loop");
    }
    else
    {
        menu.addItem(10, "New Audio Track Below");
        menu.addItem(11, "New Software Instrument Below");
        menu.addItem(12, "New External MIDI Track Below");
        menu.addItem(13, "New Aux Bus");
        menu.addSeparator();
        menu.addItem(14, "Delete Selected Track", track >= 0 && trackModel->getTrackCount() > 1);
    }

    const auto safeOwner = juce::Component::SafePointer<TimelineGrid>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                       [safeOwner, track, clip, sampleAtMouse] (int choice)
    {
        if (safeOwner == nullptr || safeOwner->trackModel == nullptr || choice == 0)
            return;

        auto& model = *safeOwner->trackModel;
        if (choice == 1 || choice == 2)
        {
            const auto trackIndex = model.getTrackIndex(model.getTrackId(static_cast<size_t>(track)));
            if (trackIndex >= 0)
            {
                const auto& clips = model.getTrack(static_cast<size_t>(trackIndex)).clips;
                const auto found = std::find_if(clips.begin(), clips.end(), [clip] (const auto& candidate) { return candidate.id == clip; });
                if (found != clips.end())
                    model.splitAudioClip(static_cast<size_t>(trackIndex), static_cast<size_t>(std::distance(clips.begin(), found)),
                                         choice == 1 ? sampleAtMouse : model.getPlayheadPosition());
            }
        }
        else if (choice == 3)
        {
            const auto trackIndex = model.getTrackIndex(model.getTrackId(static_cast<size_t>(track)));
            if (trackIndex >= 0)
            {
                const auto& clips = model.getTrack(static_cast<size_t>(trackIndex)).clips;
                const auto found = std::find_if(clips.begin(), clips.end(), [clip] (const auto& candidate) { return candidate.id == clip; });
                if (found != clips.end())
                    safeOwner->selectedClipId = model.duplicateAudioClip(clip, model.getTrackId(static_cast<size_t>(trackIndex)),
                                                                           found->startSample + found->durationSamples);
            }
        }
        else if (choice == 4)
            model.deleteAudioClip(clip);
        else if (choice == 5)
            model.setClipFades(clip, 0.0, 0.0);
        else if (choice == 6 && track >= 0 && track < static_cast<int>(model.getTrackCount()))
            model.createCrossfadesForTrack(model.getTrackId(static_cast<size_t>(track)));
        else if (choice == 7)
        {
            for (size_t index = 0; index < model.getTrackCount(); ++index)
                for (const auto& candidate : model.getTrack(index).clips)
                    if (candidate.id == clip) { model.setClipLoopEnabled(clip, ! candidate.loopEnabled); return; }
        }
        else if (choice >= 10 && choice <= 12)
        {
            const auto type = choice == 10 ? TrackType::audio : choice == 11 ? TrackType::instrument : TrackType::externalMidi;
            const auto created = safeOwner->createTrackAfter(type, track);
            if (created.isValid())
            {
                safeOwner->selectedTrack = model.getTrackIndex(created);
                if (safeOwner->onTrackSelected != nullptr)
                    safeOwner->onTrackSelected(safeOwner->selectedTrack);
            }
        }
        else if (choice == 13)
            model.addBus();
        else if (choice == 14 && track >= 0 && track < static_cast<int>(model.getTrackCount()))
            model.removeTrack(model.getTrackId(static_cast<size_t>(track)));

        safeOwner->selectedClip = -1;
        safeOwner->repaint();
    });
}

TimelineRuler::TimelineRuler(TrackDataModel* model)
    : trackModel(model)
{
    if (trackModel != nullptr)
        trackModel->addChangeListener(this);
}

TimelineRuler::~TimelineRuler()
{
    if (trackModel != nullptr)
        trackModel->removeChangeListener(this);
}

double TimelineRuler::sampleAt(const juce::Point<float>& position) const noexcept
{
    if (trackModel == nullptr)
        return 0.0;

    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    return viewStartSample + static_cast<double>(position.x) / pixelsPerSecond * trackModel->getSampleRate();
}

double TimelineRuler::snappedSampleAt(const juce::Point<float>& position) const noexcept
{
    return trackModel != nullptr ? trackModel->getSnappedSamplePosition(sampleAt(position), fineSnapInBeats) : 0.0;
}

void TimelineRuler::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.fillAll(juce::Colour(0xff202020));
    if (trackModel == nullptr)
        return;

    constexpr int locatorLaneHeight = static_cast<int>(loopLocatorLaneHeight);
    constexpr int primaryRulerBottom = 29;
    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto timelineWidth = juce::jmax(0, getWidth() - reservedTrailingWidth);
    auto sample = trackModel->getSnappedSamplePosition(viewStartSample, 1.0);
    if (sample < viewStartSample) sample += trackModel->getSampleRate() * 60.0 / trackModel->getTempoAtSample(sample);

    g.setColour(juce::Colour(0xff161b1f));
    g.fillRect(bounds.removeFromTop(locatorLaneHeight));
    const auto loopIsEngaged = trackModel->isCycleActive();
    const auto loopIsInteractive = loopIsEngaged && (loopLocatorHovered || loopFocused || dragMode != DragMode::none);
    const auto idleLoopStrip = juce::Rectangle<float>(1.0f, 2.5f, static_cast<float>(timelineWidth - 2),
                                                       static_cast<float>(locatorLaneHeight) - 5.0f);
    // The locator is always discoverable. It intentionally stays quiet until
    // the pointer enters it or the user selects it, so it never competes with
    // the musical ruler underneath.
    g.setColour(juce::Colour(0xff35434e).withAlpha(0.26f));
    g.fillRoundedRectangle(idleLoopStrip, 2.5f);
    g.setColour(juce::Colour(0xff637686).withAlpha(0.20f));
    g.drawRoundedRectangle(idleLoopStrip, 2.5f, 0.7f);
    g.setColour(juce::Colours::white.withAlpha(0.13f));
    g.drawHorizontalLine(locatorLaneHeight - 1, 0.0f, static_cast<float>(getWidth()));

    while (sample <= viewStartSample + trackModel->getSampleRate() * timelineWidth / pixelsPerSecond)
    {
        const auto signature = trackModel->getTimeSignatureAtSample(sample);
        const auto samplesPerBeat = trackModel->getSampleRate() * 60.0 / trackModel->getTempoAtSample(sample)
            * 4.0 / signature.denominator;
        const auto beatWidth = trackModel->sampleToXPosition(samplesPerBeat, pixelsPerSecond);
        const auto x = static_cast<float>(trackModel->sampleToXPosition(sample - viewStartSample, pixelsPerSecond));
        const auto barBeat = trackModel->getBarBeatInfo(sample);
        const auto isBar = barBeat.beats == 0 && barBeat.pulses == 0;
        const auto pixel = juce::roundToInt(x);

        // Beat ticks stay deliberately shorter and softer than bar lines: a
        // performer can read 1.2 / 1.3 / 1.4 at a glance without turning the
        // ruler into a row of competing labels.
        g.setColour(isBar ? juce::Colours::white.withAlpha(0.70f)
                          : juce::Colours::white.withAlpha(0.42f));
        g.drawVerticalLine(pixel, static_cast<float>(locatorLaneHeight),
                           isBar ? static_cast<float>(primaryRulerBottom) : static_cast<float>(locatorLaneHeight + 8));
        if (isBar)
            g.drawText(juce::String(barBeat.bars + 1),
                       pixel + 4, locatorLaneHeight + 1, 44, 17, juce::Justification::left, false);
        else if (beatWidth >= 42.0)
            g.drawText(juce::String(barBeat.beats + 1), pixel + 3, locatorLaneHeight + 2, 18, 14,
                       juce::Justification::left, false);

        const auto divisions = beatWidth >= 120.0 ? 4 : beatWidth >= 56.0 ? 2 : 1;
        for (int division = 1; division < divisions; ++division)
        {
            const auto subdivisionX = x + static_cast<float>(division) * static_cast<float>(beatWidth / divisions);
            g.setColour(juce::Colours::white.withAlpha(0.18f));
            g.drawVerticalLine(juce::roundToInt(subdivisionX), static_cast<float>(primaryRulerBottom - 5),
                               static_cast<float>(primaryRulerBottom));
        }
        sample += samplesPerBeat;
    }

    if (loopIsEngaged)
    {
        const auto start = static_cast<float>(trackModel->sampleToXPosition(trackModel->getCycleStartSample() - viewStartSample, pixelsPerSecond));
        const auto end = static_cast<float>(trackModel->sampleToXPosition(trackModel->getCycleEndSample() - viewStartSample, pixelsPerSecond));
        const auto cycleBounds = juce::Rectangle<float>(juce::jmin(start, end), 1.5f, std::abs(end - start),
                                                        static_cast<float>(locatorLaneHeight) - 3.0f);
        g.setColour(juce::Colour(0xff526d86).withAlpha(loopIsInteractive ? 0.62f : 0.22f));
        g.fillRoundedRectangle(cycleBounds, 2.8f);
        g.setColour(juce::Colour(0xffa7c5df).withAlpha(loopIsInteractive ? 0.90f : 0.42f));
        g.drawRoundedRectangle(cycleBounds, 2.8f, loopIsInteractive ? 1.25f : 0.8f);
    }

    for (const auto& marker : trackModel->getMarkers())
    {
        const auto x = static_cast<float>(trackModel->sampleToXPosition(
            marker.samplePosition - viewStartSample, pixelsPerSecond));
        if (x < 0.0f || x > static_cast<float>(timelineWidth))
            continue;
        juce::Path flag;
        flag.startNewSubPath(x, 0.0f);
        flag.lineTo(x + 8.0f, 3.0f);
        flag.lineTo(x, 7.0f);
        flag.closeSubPath();
        g.setColour(juce::Colour(0xffffb347));
        g.fillPath(flag);
        g.drawVerticalLine(juce::roundToInt(x), 0.0f, static_cast<float>(getHeight()));
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText(marker.name, juce::roundToInt(x + 10.0f), 0, 88, locatorLaneHeight,
                   juce::Justification::centredLeft, true);
    }

    // Secondary time ruler stays available while the primary ruler remains
    // musical. It is deliberately read-only: no modal unit switching just to
    // answer the most common "where am I in the recording?" question.
    g.setColour(juce::Colour(0xff15191d));
    g.fillRect(0, primaryRulerBottom, getWidth(), getHeight() - primaryRulerBottom);
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawHorizontalLine(primaryRulerBottom, 0.0f, static_cast<float>(getWidth()));
    // Timecode chooses its own major interval. Tying it to bars produced a
    // redundant decimal label for every bar (0:02.0, 0:04.0, ...), which is
    // neither a useful musical grid nor a legible time ruler.
    constexpr std::array<double, 10> timeIntervals { 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0 };
    constexpr double desiredTimeLabelSpacing = 260.0;
    const auto minimumInterval = desiredTimeLabelSpacing / std::max(1.0, pixelsPerSecond);
    auto timeInterval = timeIntervals.back();
    for (const auto candidate : timeIntervals)
    {
        if (candidate >= minimumInterval) { timeInterval = candidate; break; }
    }
    const auto visibleStartSeconds = viewStartSample / trackModel->getSampleRate();
    const auto visibleEndSeconds = visibleStartSeconds + timelineWidth / pixelsPerSecond;
    for (auto seconds = std::ceil(visibleStartSeconds / timeInterval) * timeInterval;
         seconds <= visibleEndSeconds + timeInterval * 0.001; seconds += timeInterval)
    {
        const auto minutes = static_cast<int>(seconds) / 60;
        const auto remaining = std::fmod(seconds, 60.0);
        const auto x = static_cast<float>((seconds - visibleStartSeconds) * pixelsPerSecond);
        g.setColour(juce::Colours::white.withAlpha(0.24f));
        g.drawVerticalLine(juce::roundToInt(x), static_cast<float>(primaryRulerBottom), static_cast<float>(getHeight()));
        g.setColour(juce::Colours::white.withAlpha(0.58f));
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::plain)));
        const auto timeLabel = timeInterval < 1.0
            ? juce::String(minutes) + ":" + juce::String(remaining, 1).paddedLeft('0', 4)
            : juce::String(minutes) + ":" + juce::String(static_cast<int>(std::round(remaining))).paddedLeft('0', 2);
        g.drawText(timeLabel,
                   juce::roundToInt(x + 4.0f), primaryRulerBottom + 1, 52, 11, juce::Justification::left, false);
    }

    if (trackModel->isPunchActive())
    {
        const auto in = static_cast<float>(trackModel->sampleToXPosition(trackModel->getPunchInSample() - viewStartSample, pixelsPerSecond));
        const auto out = static_cast<float>(trackModel->sampleToXPosition(trackModel->getPunchOutSample() - viewStartSample, pixelsPerSecond));
        g.setColour(juce::Colour(0xffe35d62).withAlpha(0.26f));
        g.fillRect(juce::Rectangle<float>(in, 1.0f, out - in, locatorLaneHeight - 2.0f));
        g.setColour(juce::Colour(0xffff8184));
        g.drawVerticalLine(juce::roundToInt(in), 0.0f, locatorLaneHeight);
        g.drawVerticalLine(juce::roundToInt(out), 0.0f, locatorLaneHeight);
    }

    for (const auto& event : trackModel->getTempoMap())
    {
        if (event.samplePosition <= 0.0)
            continue;
        const auto x = static_cast<float>(trackModel->sampleToXPosition(event.samplePosition - viewStartSample, pixelsPerSecond));
        if (x < 0.0f || x > static_cast<float>(timelineWidth))
            continue;
        g.setColour(juce::Colour(0xff63d6df));
        g.fillEllipse(x - 3.0f, 2.0f, 6.0f, 6.0f);
        if (trackModel->sampleToXPosition(trackModel->getSampleRate() * 60.0 / trackModel->getTempoAtSample(event.samplePosition),
                                          pixelsPerSecond) >= 32.0)
        {
            g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::plain)));
            g.drawText(juce::String(event.bpm, 1), juce::roundToInt(x + 5.0f), 0, 40, locatorLaneHeight,
                       juce::Justification::centredLeft, false);
        }
    }

    // A signature change is a musical event, not decoration: show it at the
    // exact bar where the grid starts using its new beat unit.
    for (const auto& event : trackModel->getTimeSignatureMap())
    {
        if (event.samplePosition <= 0.0)
            continue;
        const auto x = static_cast<float>(trackModel->sampleToXPosition(event.samplePosition - viewStartSample, pixelsPerSecond));
        if (x < 0.0f || x > static_cast<float>(timelineWidth))
            continue;
        g.setColour(juce::Colour(0xffc29cff));
        juce::Path flag;
        flag.addTriangle(x, 1.0f, x + 7.0f, 4.5f, x, 8.0f);
        g.fillPath(flag);
        g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
        g.drawText(juce::String(event.numerator) + "/" + juce::String(event.denominator),
                   juce::roundToInt(x + 8.0f), 0, 32, locatorLaneHeight, juce::Justification::centredLeft, false);
    }

    const auto playheadX = static_cast<float>(trackModel->sampleToXPosition(displayPlayheadSample - viewStartSample,
                                                                              pixelsPerSecond));
    if (playheadX >= -6.0f && playheadX <= static_cast<float>(timelineWidth) + 6.0f)
    {
        const auto playheadPixel = juce::roundToInt(playheadX);
        juce::Path head;
        head.addTriangle(static_cast<float>(playheadPixel - 5), static_cast<float>(locatorLaneHeight),
                         static_cast<float>(playheadPixel + 5), static_cast<float>(locatorLaneHeight),
                         static_cast<float>(playheadPixel), static_cast<float>(locatorLaneHeight + 5));
        g.setColour(juce::Colour(0xffff5c5c));
        g.fillPath(head);
        g.drawVerticalLine(playheadPixel, static_cast<float>(locatorLaneHeight + 4), static_cast<float>(getHeight()));
    }
}

void TimelineRuler::mouseDown(const juce::MouseEvent& event)
{
    if (trackModel == nullptr)
        return;

    // Keep the locator compact; its upper band creates and edits the cycle,
    // while the lower ruler remains available for playhead scrubbing.
    const auto clickedSample = snappedSampleAt(event.position);
    if (event.mods.isRightButtonDown())
    {
        trackModel->clearCycle();
        loopFocused = false;
        repaint();
        return;
    }
    if (event.position.y >= loopLocatorLaneHeight)
    {
        dragMode = DragMode::scrubPlayhead;
        trackModel->setPlayheadPosition(clickedSample);
        repaint();
        return;
    }

    const auto wasFocused = loopFocused && trackModel->isCycleActive();
    loopFocused = true;
    beginCycleEdit(clickedSample, event.position.x);
    // The first click only selects the loop. A second intentional click is a
    // quick toggle-off; any drag clears this flag in updateCycleEdit().
    cycleClickPendingToggle = wasFocused && dragMode != DragMode::createCycle;
}

void TimelineRuler::mouseDrag(const juce::MouseEvent& event)
{
    if (trackModel == nullptr || dragMode == DragMode::none)
        return;

    const auto current = snappedSampleAt(event.position);
    if (dragMode == DragMode::scrubPlayhead)
        trackModel->setPlayheadPosition(current);
    else
        updateCycleEdit(current, event.position.x);
    repaint();
}

void TimelineRuler::mouseUp(const juce::MouseEvent&)
{
    finishCycleEdit();
}

void TimelineRuler::mouseMove(const juce::MouseEvent& event)
{
    const auto nowHovered = event.position.y < loopLocatorLaneHeight;
    if (loopLocatorHovered != nowHovered)
    {
        loopLocatorHovered = nowHovered;
        repaint();
    }
    if (! nowHovered || trackModel == nullptr)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (! trackModel->isCycleActive())
    {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        return;
    }
    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto start = trackModel->sampleToXPosition(trackModel->getCycleStartSample() - viewStartSample, pixelsPerSecond);
    const auto end = trackModel->sampleToXPosition(trackModel->getCycleEndSample() - viewStartSample, pixelsPerSecond);
    setMouseCursor((std::abs(event.position.x - start) <= loopHandleWidth || std::abs(event.position.x - end) <= loopHandleWidth)
                       ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void TimelineRuler::mouseExit(const juce::MouseEvent&)
{
    if (loopLocatorHovered)
    {
        loopLocatorHovered = false;
        repaint();
    }
}

void TimelineRuler::beginCycleEdit(double clickedSample, float pointerX)
{
    cycleClickPendingToggle = false;
    cycleMouseDownX = pointerX;

    if (! trackModel->isCycleActive())
    {
        createDefaultCycle(clickedSample);
        return;
    }

    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto cycleStartX = trackModel->sampleToXPosition(trackModel->getCycleStartSample() - viewStartSample, pixelsPerSecond);
    const auto cycleEndX = trackModel->sampleToXPosition(trackModel->getCycleEndSample() - viewStartSample, pixelsPerSecond);
    originalCycleStartSample = trackModel->getCycleStartSample();
    originalCycleEndSample = trackModel->getCycleEndSample();

    // An ordinary click focuses the existing cycle; it must not silently turn
    // loop playback off while the performer is trying to grab its handle.
    if (std::abs(pointerX - cycleStartX) <= loopHandleWidth)
        dragMode = DragMode::resizeCycleStart;
    else if (std::abs(pointerX - cycleEndX) <= loopHandleWidth)
        dragMode = DragMode::resizeCycleEnd;
    else if (pointerX >= cycleStartX && pointerX <= cycleEndX)
    {
        dragAnchorSample = clickedSample;
        dragMode = DragMode::moveCycle;
    }
    else
    {
        createDefaultCycle(clickedSample);
        return;
    }

    cycleClickPendingToggle = true;
}

void TimelineRuler::updateCycleEdit(double currentSample, float pointerX)
{
    // macOS can deliver a zero-distance drag after a click. Keep the toggle
    // pending until the gesture passes a small, intentional threshold.
    if (cycleClickPendingToggle && std::abs(pointerX - cycleMouseDownX) < loopDragThreshold)
        return;

    cycleClickPendingToggle = false;
    switch (dragMode)
    {
        case DragMode::createCycle:
            trackModel->setCycle(dragAnchorSample, currentSample);
            break;
        case DragMode::resizeCycleStart:
            trackModel->setCycle(currentSample, originalCycleEndSample);
            break;
        case DragMode::resizeCycleEnd:
            trackModel->setCycle(originalCycleStartSample, currentSample);
            break;
        case DragMode::moveCycle:
        {
            const auto offset = currentSample - dragAnchorSample;
            trackModel->setCycle(juce::jmax(0.0, originalCycleStartSample + offset),
                                 juce::jmax(0.0, originalCycleEndSample + offset));
            break;
        }
        case DragMode::none:
        case DragMode::scrubPlayhead:
            break;
    }
}

void TimelineRuler::finishCycleEdit()
{
    if (cycleClickPendingToggle && trackModel != nullptr)
    {
        trackModel->clearCycle();
        loopFocused = false;
        repaint();
    }
    cycleClickPendingToggle = false;
    dragMode = DragMode::none;
}

void TimelineRuler::createDefaultCycle(double clickedSample)
{
    const auto barBeat = trackModel->getBarBeatInfo(clickedSample);
    dragAnchorSample = trackModel->barBeatToSamples(barBeat.bars, 0, 0);
    dragMode = DragMode::createCycle;
    trackModel->setCycle(dragAnchorSample, trackModel->barBeatToSamples(barBeat.bars + 1, 0, 0));
    repaint();
}

void TimelineGrid::mouseDown(const juce::MouseEvent& event)
{
    if (trackModel == nullptr)
        return;

    if (event.mods.isMiddleButtonDown())
    {
        panningTimeline = true;
        panDragStartX = event.position.x;
        panDragStartSample = viewStartSample;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto sampleAtMouse = sampleAt(event.position);
    const auto track = trackAt(event.position);
    if (track < 0)
        return;
    grabKeyboardFocus();
    const auto primaryModifier = event.mods.isCommandDown() || event.mods.isCtrlDown();
    if ((primaryModifier || trackModel->getActiveTool() == TrackDataModel::EditTool::Range)
        && ! event.mods.isPopupMenu())
    {
        marqueeSelecting = true;
        marqueeSelectionValid = false;
        marqueeAnchorSample = marqueeCurrentSample = sampleAtMouse;
        marqueeAnchorTrack = marqueeCurrentTrack = track;
        selectedClipId = {};
        selectedMidiClipId = {};
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        repaint();
        return;
    }
    const auto& audioClips = trackModel->getTrack(static_cast<size_t>(track)).clips;
    for (size_t index = 0; index < audioClips.size(); ++index)
    {
        const auto& clip = audioClips[index];
        const auto startX = trackModel->sampleToXPosition(clip.startSample - viewStartSample, pixelsPerSecond);
        const auto endX = trackModel->sampleToXPosition(clip.startSample + clip.durationSamples - viewStartSample, pixelsPerSecond);
        const auto clipBounds = AudioRegionBounds { static_cast<float>(startX),
            static_cast<float>(track * trackModel->getTrackHeight() - verticalScrollOffset) + 3.0f,
            juce::jmax(0.0f, static_cast<float>(endX - startX)), static_cast<float>(trackModel->getTrackHeight() - 6) };
        const auto zone = isAudioRegionLoopHandle(event.position.x, event.position.y, clipBounds)
            ? AudioRegionMouseZone::loop
            : detectAudioRegionMouseZone(event.position.x, event.position.y, clipBounds, clipTrimHandleWidth);
        if (zone != AudioRegionMouseZone::none)
        {
            if (event.mods.isPopupMenu())
            {
                showContextMenu(event, track, clip.id, sampleAtMouse);
                return;
            }
            selectedTrack = track;
            selectedClip = static_cast<int>(index);
            selectedClipId = clip.id;
            selectedMidiClipId = {};
            if (event.mods.isShiftDown()) selection.extend(clip.id);
            else if (event.mods.isCommandDown() || event.mods.isCtrlDown()) selection.toggle(clip.id);
            else selection.selectOnly(clip.id);
            if (onTrackSelected != nullptr) onTrackSelected(track);
            if (trackModel->getActiveTool() == TrackDataModel::EditTool::Scissors)
            {
                trackModel->splitAudioClip(static_cast<size_t>(track), index, sampleAtMouse);
                repaint();
                return;
            }
            if (trackModel->getActiveTool() == TrackDataModel::EditTool::Eraser)
            {
                trackModel->deleteAudioClip(static_cast<size_t>(track), index);
                selectedClip = -1;
                selectedClipId = {};
                repaint();
                return;
            }
            if (onAudioClipSelected != nullptr) onAudioClipSelected(clip.id);
            if (zone == AudioRegionMouseZone::loop)
            {
                // Upper-right drag creates a loop. The lower-right trim path
                // remains untouched, so an audio region cannot be extended
                // past its source unless this explicit loop gesture is used.
                trackModel->setClipLoopEnabled(clip.id, true);
                audioRegionZone = AudioRegionMouseZone::rightTrim;
            }
            // An edge trims by default; Option/Alt turns that same deliberate
            // edge target into a fade edit. The resolver is shared by hover
            // and press so feedback can never promise a different operation.
            audioRegionZone = zone == AudioRegionMouseZone::loop ? AudioRegionMouseZone::rightTrim
                : applyAudioRegionModifiers(zone, event.mods.isAltDown());
            audioRegionPhase = AudioRegionInteractionPhase::pressed;
            audioRegionPressPosition = event.position;
            adjustingFadeIn = audioRegionZone == AudioRegionMouseZone::fadeIn;
            adjustingFadeOut = audioRegionZone == AudioRegionMouseZone::fadeOut;
            trimmingLeft = audioRegionZone == AudioRegionMouseZone::leftTrim;
            trimmingRight = audioRegionZone == AudioRegionMouseZone::rightTrim;
            clipDragGrabOffsetSamples = juce::jmax(0.0, sampleAtMouse - clip.startSample);
            clipDragPreviewSample = clip.startSample;
            clipDragPreviewTrack = track;
            clipTrimOriginalStartSample = clip.startSample;
            clipTrimOriginalDurationSamples = clip.durationSamples;
            // Loop regions may extend beyond their source. Non-loop audio
            // remains constrained in TrackDataModel::trimAudioClip.
            clipTrimMaximumEndSample = std::numeric_limits<double>::max();
            clipTrimPreviewStartSample = clip.startSample;
            clipTrimPreviewDurationSamples = clip.durationSamples;
            clipFadeOriginalInSamples = clip.fadeInSamples;
            clipFadeOriginalOutSamples = clip.fadeOutSamples;
            clipFadePreviewInSamples = clip.fadeInSamples;
            clipFadePreviewOutSamples = clip.fadeOutSamples;
            setMouseCursor(cursorForAudioRegionZone(audioRegionZone));
            return;
        }
    }
    if (event.mods.isPopupMenu())
    {
        showContextMenu(event, track, {}, sampleAtMouse);
        return;
    }
    for (const auto& midiClip : trackModel->getMidiClips())
    {
        if (midiClip.trackId != trackModel->getTrackId(static_cast<size_t>(track))) continue;
        const auto duration = midiClipDurationSamples(midiClip, trackModel->getSampleRate());
        const auto startX = trackModel->sampleToXPosition(midiClip.startSample - viewStartSample, pixelsPerSecond);
        const auto endX = trackModel->sampleToXPosition(midiClip.startSample + duration - viewStartSample, pixelsPerSecond);
        if (event.position.x >= startX - clipTrimHandleWidth
            && event.position.x <= endX + clipTrimHandleWidth)
        {
            selectedTrack = track; selectedClip = -1; selectedClipId = {}; selectedMidiClipId = midiClip.id;
            if (event.mods.isShiftDown()) selection.extend(midiClip.id);
            else if (event.mods.isCommandDown() || event.mods.isCtrlDown()) selection.toggle(midiClip.id);
            else selection.selectOnly(midiClip.id);
            midiClipTrimOriginalDurationSamples = duration;
            midiClipTrimPreviewDurationSamples = duration;
            midiClipTrimOriginalStartSample = midiClip.startSample;
            midiClipTrimPreviewStartSample = midiClip.startSample;
            const auto leftEdgeHit = std::abs(event.position.x - startX) <= clipTrimHandleWidth;
            const auto rightEdgeHit = std::abs(event.position.x - endX) <= clipTrimHandleWidth;
            trimmingMidiClipLeft = leftEdgeHit
                && (! rightEdgeHit || std::abs(event.position.x - startX) <= std::abs(event.position.x - endX));
            trimmingMidiClipRight = rightEdgeHit && ! trimmingMidiClipLeft;
            midiClipDragGrabOffsetSamples = juce::jmax(0.0, sampleAtMouse - midiClip.startSample);
            midiClipDragPreviewSample = midiClip.startSample;
            midiClipDragPreviewTrack = track;
            draggingMidiClip = false;
            if (onTrackSelected != nullptr) onTrackSelected(track);
            if (onMidiClipSelected != nullptr) onMidiClipSelected(midiClip.id);
            setMouseCursor(trimmingMidiClipLeft || trimmingMidiClipRight ? juce::MouseCursor::LeftRightResizeCursor
                                                  : juce::MouseCursor::DraggingHandCursor);
            repaint();
            return;
        }
    }
    // Option/Alt-drag blank timeline is the temporary Zoom Tool. It is
    // intentionally checked after region hits so Alt-edge fade gestures keep
    // priority over a background navigation tool.
    if (event.mods.isAltDown() && ! event.mods.isPopupMenu())
    {
        zoomSelecting = true;
        zoomSelectionAnchor = zoomSelectionCurrent = event.position;
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        repaint();
        return;
    }
    if (trackModel->isPlaying())
    {
        scrubbingPlayhead = true;
        trackModel->setPlayheadPosition(sampleAtMouse);
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    // Clicking empty timeline space is an explicit focus-dismiss gesture. It
    // must not leave the previous track's highlight/monitor focus latched;
    // selecting a clip above still takes the normal track-selection path.
    selectedTrack = -1;
    selectedClip = -1;
    selectedClipId = {};
    selectedMidiClipId = {};
    selection.clear();
    scrubbingPlayhead = true;
    trackModel->setPlayheadPosition(sampleAtMouse);
    if (onTrackSelected != nullptr) onTrackSelected(-1);
    repaint();
}

void TimelineGrid::mouseMove(const juce::MouseEvent& event)
{
    if (trackModel == nullptr || panningTimeline || scrubbingPlayhead
        || trimmingLeft || trimmingRight || adjustingFadeIn || adjustingFadeOut)
        return;
    if (audioRegionPhase == AudioRegionInteractionPhase::pressed
        || audioRegionPhase == AudioRegionInteractionPhase::dragging)
        return;

    const auto track = trackAt(event.position);
    if (track < 0)
    {
        updateAudioRegionHover({}, AudioRegionMouseZone::none);
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto& clips = trackModel->getTrack(static_cast<size_t>(track)).clips;
    for (const auto& clip : clips)
    {
        const auto startX = trackModel->sampleToXPosition(clip.startSample - viewStartSample, pixelsPerSecond);
        const auto endX = trackModel->sampleToXPosition(clip.startSample + clip.durationSamples - viewStartSample, pixelsPerSecond);
        const auto bounds = AudioRegionBounds { static_cast<float>(startX),
            static_cast<float>(track * trackModel->getTrackHeight() - verticalScrollOffset) + 3.0f,
            juce::jmax(0.0f, static_cast<float>(endX - startX)), static_cast<float>(trackModel->getTrackHeight() - 6) };
        const auto baseZone = isAudioRegionLoopHandle(event.position.x, event.position.y, bounds)
            ? AudioRegionMouseZone::loop
            : detectAudioRegionMouseZone(event.position.x, event.position.y, bounds, clipTrimHandleWidth);
        const auto zone = applyAudioRegionModifiers(baseZone, event.mods.isAltDown());
        if (zone != AudioRegionMouseZone::none)
        {
            updateAudioRegionHover(clip.id, zone);
            setMouseCursor(cursorForAudioRegionZone(zone));
            return;
        }
    }

    updateAudioRegionHover({}, AudioRegionMouseZone::none);

    const auto trackId = trackModel->getTrackId(static_cast<size_t>(track));
    for (const auto& midiClip : trackModel->getMidiClips())
    {
        if (midiClip.trackId != trackId)
            continue;

        const auto duration = midiClipDurationSamples(midiClip, trackModel->getSampleRate());
        const auto startX = trackModel->sampleToXPosition(midiClip.startSample - viewStartSample,
                                                          pixelsPerSecond);
        const auto endX = trackModel->sampleToXPosition(midiClip.startSample + duration - viewStartSample,
                                                        pixelsPerSecond);
        if (event.position.x >= startX - clipTrimHandleWidth
            && event.position.x <= endX + clipTrimHandleWidth
            && (std::abs(event.position.x - startX) <= clipTrimHandleWidth
                || std::abs(event.position.x - endX) <= clipTrimHandleWidth))
        {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void TimelineGrid::mouseExit(const juce::MouseEvent&)
{
    if (audioRegionPhase == AudioRegionInteractionPhase::idle
        || audioRegionPhase == AudioRegionInteractionPhase::hover)
        updateAudioRegionHover({}, AudioRegionMouseZone::none);
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void TimelineGrid::updateAudioRegionHover(ClipId clipId, AudioRegionMouseZone zone)
{
    if (hoveredAudioClipId == clipId && hoveredAudioRegionZone == zone)
        return;

    hoveredAudioClipId = clipId;
    hoveredAudioRegionZone = zone;
    audioRegionPhase = zone == AudioRegionMouseZone::none
        ? AudioRegionInteractionPhase::idle
        : AudioRegionInteractionPhase::hover;
    repaint();
}

void TimelineGrid::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (trackModel == nullptr)
        return;

    const auto trackHeight = juce::jmax(1, trackModel->getTrackHeight());
    const auto clickedTrack = static_cast<int>((event.position.y + verticalScrollOffset) / trackHeight);
    if (clickedTrack >= 0 && clickedTrack < static_cast<int>(trackModel->getTrackCount()))
    {
        const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
        const auto clickedSample = editSample(sampleAt(event.position), event.mods);
        const auto& clips = trackModel->getTrack(static_cast<size_t>(clickedTrack)).clips;
        for (size_t index = 0; index < clips.size(); ++index)
        {
            const auto& clip = clips[index];
            const auto startX = trackModel->sampleToXPosition(clip.startSample - viewStartSample, pixelsPerSecond);
            const auto endX = trackModel->sampleToXPosition(clip.startSample + clip.durationSamples - viewStartSample, pixelsPerSecond);
            if (event.position.x > startX && event.position.x < endX
                && clickedSample > clip.startSample && clickedSample < clip.startSample + clip.durationSamples)
            {
                selectedTrack = clickedTrack;
                selectedClip = static_cast<int>(index);
                selectedClipId = clip.id;
                selection.selectOnly(clip.id);
                if (onTrackSelected != nullptr) onTrackSelected(clickedTrack);
                if (onAudioClipOpenRequested != nullptr) onAudioClipOpenRequested(clip.id);
                repaint();
                return;
            }
        }
        const auto trackId = trackModel->getTrackId(static_cast<size_t>(clickedTrack));
        for (const auto& midi : trackModel->getMidiClips())
            if (midi.trackId == trackId && clickedSample >= midi.startSample
                && clickedSample <= midi.startSample + midiClipDurationSamples(midi, trackModel->getSampleRate()))
            {
                selectedClipId = {}; selectedMidiClipId = midi.id; selection.selectOnly(midi.id);
                if (onMidiClipOpenRequested != nullptr) onMidiClipOpenRequested(midi.id);
                repaint();
                return;
            }
        const auto type = trackModel->getTrack(static_cast<size_t>(clickedTrack)).type;
        if (type == TrackType::instrument || type == TrackType::externalMidi)
        {
            const auto duration = trackModel->getSampleRate() * 60.0 / trackModel->getBpm()
                * trackModel->getTimeSignatureNumerator();
            const auto created = trackModel->addMidiClip(trackId, clickedSample);
            if (created.isValid())
            {
                trackModel->setMidiClipDuration(created, duration);
                selectedMidiClipId = created; selection.selectOnly(created);
                if (onMidiClipSelected != nullptr) onMidiClipSelected(created);
            }
            repaint();
        }
        return;
    }

    if (trackModel->getTrackCount() >= TrackDataModel::maxTracks)
        return;

    const auto contentY = event.position.y + static_cast<float>(verticalScrollOffset);
    const auto trackCount = static_cast<int>(trackModel->getTrackCount());
    if (contentY < static_cast<float>(trackCount * trackHeight))
        return;
    const auto anchor = trackCount - 1;
    const auto type = anchor >= 0 ? trackModel->getTrack(static_cast<size_t>(anchor)).type : TrackType::audio;
    const auto createdId = createTrackAfter(type, anchor);
    if (! createdId.isValid())
        return;

    selectedTrack = trackModel->getTrackIndex(createdId);
    selectedClip = -1;
    selectedClipId = {};
    if (onTrackSelected != nullptr)
        onTrackSelected(selectedTrack);
    repaint();
}

void TimelineGrid::mouseDrag(const juce::MouseEvent& event)
{
    if (trackModel == nullptr)
        return;

    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    lastDragPosition = event.position;
    edgeAutoScrollActive = selectedClipId.isValid() || selectedMidiClipId.isValid()
        || trimmingLeft || trimmingRight || adjustingFadeIn || adjustingFadeOut || marqueeSelecting || zoomSelecting;
    if (zoomSelecting)
    {
        zoomSelectionCurrent = event.position;
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        repaint();
        return;
    }
    if (marqueeSelecting)
    {
        marqueeCurrentSample = sampleAt(event.position);
        marqueeCurrentTrack = trackAt(event.position);
        marqueeSelectionValid = std::abs(marqueeCurrentSample - marqueeAnchorSample) > 1.0;
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        repaint();
        return;
    }
    if (panningTimeline)
    {
        const auto gain = event.mods.isShiftDown() ? timelineDragPanFastGain : timelineDragPanGain;
        const auto deltaSamples = static_cast<double>(panDragStartX - event.position.x) / pixelsPerSecond
            * trackModel->getSampleRate() * gain;
        updateView(panDragStartSample + deltaSamples, trackModel->getHorizontalZoom());
        return;
    }
    if (scrubbingPlayhead)
    {
        constexpr float scrollEdgeWidth = 32.0f;
        const auto x = event.position.x;
        const auto edgeStrength = x < scrollEdgeWidth
            ? (scrollEdgeWidth - x) / scrollEdgeWidth
            : x > static_cast<float>(getWidth()) - scrollEdgeWidth
                ? (x - (static_cast<float>(getWidth()) - scrollEdgeWidth)) / scrollEdgeWidth : 0.0f;
        if (edgeStrength > 0.0f)
        {
            const auto direction = x < scrollEdgeWidth ? -1.0 : 1.0;
            const auto scrollSeconds = 0.10 + edgeStrength * 0.22;
            updateView(viewStartSample + direction * scrollSeconds * trackModel->getSampleRate(),
                       trackModel->getHorizontalZoom());
        }
        trackModel->setPlayheadPosition(sampleAt(event.position));
        return;
    }
    if (selectedClipId.isValid() && audioRegionPhase == AudioRegionInteractionPhase::pressed)
    {
        if (! audioRegionDragThresholdExceeded(audioRegionPressPosition.x, audioRegionPressPosition.y,
                                                event.position.x, event.position.y))
            return;
        audioRegionPhase = AudioRegionInteractionPhase::dragging;
    }
    if (selectedClipId.isValid() && audioRegionPhase != AudioRegionInteractionPhase::dragging)
        return;
    if (selectedTrack >= 0 && selectedClip >= 0 && (adjustingFadeIn || adjustingFadeOut))
    {
        const auto sample = viewStartSample
            + static_cast<double>(event.position.x) / pixelsPerSecond * trackModel->getSampleRate();
        const auto clipEnd = clipTrimOriginalStartSample + clipTrimOriginalDurationSamples;
        const auto fadeLength = adjustingFadeIn ? sample - clipTrimOriginalStartSample : clipEnd - sample;
        if (adjustingFadeIn)
            clipFadePreviewInSamples = juce::jlimit(0.0, clipTrimOriginalDurationSamples, fadeLength);
        else
            clipFadePreviewOutSamples = juce::jlimit(0.0, clipTrimOriginalDurationSamples, fadeLength);
        repaint();
        return;
    }
    if (selectedTrack >= 0 && selectedClip >= 0 && (trimmingLeft || trimmingRight))
    {
        const auto sample = viewStartSample
            + static_cast<double>(event.position.x) / pixelsPerSecond * trackModel->getSampleRate();
        const auto snappedSample = editSample(sample, event.mods);
        const auto originalEnd = clipTrimOriginalStartSample + clipTrimOriginalDurationSamples;
        if (trimmingLeft)
        {
            clipTrimPreviewStartSample = std::clamp(snappedSample, 0.0, originalEnd - 1.0);
            clipTrimPreviewDurationSamples = originalEnd - clipTrimPreviewStartSample;
        }
        else
        {
            const auto previewEnd = std::clamp(snappedSample, clipTrimOriginalStartSample + 1.0,
                                               juce::jmax(originalEnd, clipTrimMaximumEndSample));
            clipTrimPreviewStartSample = clipTrimOriginalStartSample;
            clipTrimPreviewDurationSamples = previewEnd - clipTrimOriginalStartSample;
        }
        repaint();
        return;
    }

    if (selectedMidiClipId.isValid())
    {
        if (trimmingMidiClipLeft || trimmingMidiClipRight)
        {
            const auto draggedSample = editSample(sampleAt(event.position), event.mods);
            const auto originalEnd = midiClipTrimOriginalStartSample + midiClipTrimOriginalDurationSamples;
            if (trimmingMidiClipLeft)
            {
                // The model retains source-relative notes through its content
                // offset, so pulling this edge never shifts their timing.
                midiClipTrimPreviewStartSample = std::clamp(draggedSample, 0.0, originalEnd - 1.0);
                midiClipTrimPreviewDurationSamples = originalEnd - midiClipTrimPreviewStartSample;
            }
            else
            {
                midiClipTrimPreviewStartSample = midiClipTrimOriginalStartSample;
                midiClipTrimPreviewDurationSamples = juce::jmax(1.0,
                    draggedSample - midiClipTrimOriginalStartSample);
            }
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            repaint();
            return;
        }

        const auto candidate = trackAt(event.position);
        const auto validDestination = candidate >= 0 && candidate < static_cast<int>(trackModel->getTrackCount())
            && (trackModel->getTrack(static_cast<size_t>(candidate)).type == TrackType::instrument
                || trackModel->getTrack(static_cast<size_t>(candidate)).type == TrackType::externalMidi);
        if (validDestination)
        {
            midiClipDragPreviewTrack = candidate;
            midiClipDragPreviewSample = editSample(sampleAt(event.position) - midiClipDragGrabOffsetSamples, event.mods);
            draggingMidiClip = true;
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            repaint();
        }
        return;
    }

    if (selectedClipId.isValid())
    {
        updateClipDragPreview(event.position, event.mods);
        draggingClip = true;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        repaint();
    }
}

void TimelineGrid::mouseUp(const juce::MouseEvent&)
{
    if (zoomSelecting)
    {
        zoomSelecting = false;
        const auto selection = juce::Rectangle<float>::leftTopRightBottom(
            juce::jmin(zoomSelectionAnchor.x, zoomSelectionCurrent.x), juce::jmin(zoomSelectionAnchor.y, zoomSelectionCurrent.y),
            juce::jmax(zoomSelectionAnchor.x, zoomSelectionCurrent.x), juce::jmax(zoomSelectionAnchor.y, zoomSelectionCurrent.y));
        if (selection.getWidth() >= 8.0f && trackModel != nullptr)
        {
            const auto start = sampleAt(zoomSelectionAnchor);
            const auto end = sampleAt(zoomSelectionCurrent);
            const auto firstTrack = trackAt(zoomSelectionAnchor);
            const auto lastTrack = trackAt(zoomSelectionCurrent);
            viewportController.setState(TimelineZoomPolicy::fitSelection(viewportController.state(), start, end,
                getWidth(), std::abs(lastTrack - firstTrack) + 1, getHeight(), trackModel->getSampleRate()));
        }
        edgeAutoScrollActive = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
        return;
    }
    if (marqueeSelecting)
    {
        marqueeSelecting = false;
        marqueeSelectionValid = std::abs(marqueeCurrentSample - marqueeAnchorSample) > 1.0;
        edgeAutoScrollActive = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
        return;
    }
    const auto commitAudioGesture = audioRegionPhase == AudioRegionInteractionPhase::dragging;
    if (commitAudioGesture && (trimmingLeft || trimmingRight) && trackModel != nullptr && selectedClipId.isValid()
        && (clipTrimPreviewStartSample != clipTrimOriginalStartSample
            || clipTrimPreviewDurationSamples != clipTrimOriginalDurationSamples))
        trackModel->trimAudioClip(selectedClipId, clipTrimPreviewStartSample, clipTrimPreviewDurationSamples);
    if (commitAudioGesture && (adjustingFadeIn || adjustingFadeOut) && trackModel != nullptr && selectedClipId.isValid()
        && (clipFadePreviewInSamples != clipFadeOriginalInSamples
            || clipFadePreviewOutSamples != clipFadeOriginalOutSamples))
        trackModel->setClipFades(selectedClipId, clipFadePreviewInSamples, clipFadePreviewOutSamples);
    if (commitAudioGesture && draggingClip && trackModel != nullptr && selectedClipId.isValid() && clipDragPreviewTrack >= 0)
    {
        const auto destination = trackModel->getTrackId(static_cast<size_t>(clipDragPreviewTrack));
        trackModel->moveAudioClip(selectedClipId, destination, clipDragPreviewSample);
        selectedTrack = clipDragPreviewTrack;
        if (onTrackSelected != nullptr)
            onTrackSelected(selectedTrack);
    }
    if ((trimmingMidiClipLeft || trimmingMidiClipRight) && trackModel != nullptr && selectedMidiClipId.isValid()
        && (midiClipTrimPreviewDurationSamples != midiClipTrimOriginalDurationSamples
            || midiClipTrimPreviewStartSample != midiClipTrimOriginalStartSample))
        trackModel->trimMidiClip(selectedMidiClipId, midiClipTrimPreviewStartSample, midiClipTrimPreviewDurationSamples);
    if (draggingMidiClip && trackModel != nullptr && selectedMidiClipId.isValid() && midiClipDragPreviewTrack >= 0)
        trackModel->moveMidiClip(selectedMidiClipId,
            trackModel->getTrackId(static_cast<size_t>(midiClipDragPreviewTrack)), midiClipDragPreviewSample);
    cancelAudioRegionInteraction();
    trimmingMidiClipLeft = false;
    trimmingMidiClipRight = false;
    draggingMidiClip = false;
    panningTimeline = false;
    scrubbingPlayhead = false;
    trimmingLeft = false;
    trimmingRight = false;
    adjustingFadeIn = false;
    adjustingFadeOut = false;
    edgeAutoScrollActive = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void TimelineGrid::cancelAudioRegionInteraction() noexcept
{
    audioRegionZone = AudioRegionMouseZone::none;
    audioRegionPhase = AudioRegionInteractionPhase::idle;
    hoveredAudioClipId = {};
    hoveredAudioRegionZone = AudioRegionMouseZone::none;
    draggingClip = false;
    trimmingLeft = false;
    trimmingRight = false;
    adjustingFadeIn = false;
    adjustingFadeOut = false;
}

void TimelineGrid::updateClipDragPreview(const juce::Point<float>& position,
                                         const juce::ModifierKeys& modifiers)
{
    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto candidateTrack = trackAt(position);
    const auto sample = viewStartSample + static_cast<double>(position.x) / pixelsPerSecond * trackModel->getSampleRate();
    clipDragPreviewSample = editSample(sample - clipDragGrabOffsetSamples, modifiers);
    clipDragPreviewTrack = isAudioTrack(*trackModel, candidateTrack) ? candidateTrack : -1;
}

void TimelineGrid::updateEdgeAutoScroll()
{
    constexpr float edgeWidth = 42.0f;
    const auto x = lastDragPosition.x;
    const auto width = static_cast<float>(getWidth());
    const auto strength = x < edgeWidth ? (edgeWidth - x) / edgeWidth
        : x > width - edgeWidth ? (x - (width - edgeWidth)) / edgeWidth : 0.0f;
    if (strength > 0.0f)
    {
        const auto direction = x < edgeWidth ? -1.0 : 1.0;
        const auto secondsPerTick = 0.018 + strength * 0.085;
        updateView(viewStartSample + direction * secondsPerTick * trackModel->getSampleRate(),
                   trackModel->getHorizontalZoom());
    }

    const auto y = lastDragPosition.y;
    const auto height = static_cast<float>(getHeight());
    const auto verticalStrength = y < edgeWidth ? (edgeWidth - y) / edgeWidth
        : y > height - edgeWidth ? (y - (height - edgeWidth)) / edgeWidth : 0.0f;
    if (verticalStrength > 0.0f)
    {
        const auto direction = y < edgeWidth ? -1.0f : 1.0f;
        const auto pixelsPerTick = juce::roundToInt((2.0f + verticalStrength * 11.0f) * direction);
        updateVerticalScroll(verticalScrollOffset + pixelsPerTick);
    }
    if (draggingClip && selectedClipId.isValid())
        updateClipDragPreview(lastDragPosition, juce::ModifierKeys::getCurrentModifiers());
    repaint();
}

bool TimelineGrid::autoScrollForExternalFileDrag(juce::Point<float> position)
{
    if (trackModel == nullptr)
        return false;

    constexpr float edgeWidth = 42.0f;
    const auto x = position.x;
    const auto width = static_cast<float>(getWidth());
    const auto horizontalStrength = x < edgeWidth ? (edgeWidth - x) / edgeWidth
        : x > width - edgeWidth ? (x - (width - edgeWidth)) / edgeWidth : 0.0f;
    auto changed = false;
    if (horizontalStrength > 0.0f)
    {
        const auto direction = x < edgeWidth ? -1.0 : 1.0;
        const auto secondsPerTick = 0.018 + horizontalStrength * 0.085;
        updateView(viewStartSample + direction * secondsPerTick * trackModel->getSampleRate(),
                   trackModel->getHorizontalZoom());
        changed = true;
    }

    const auto y = position.y;
    const auto height = static_cast<float>(getHeight());
    const auto verticalStrength = y < edgeWidth ? (edgeWidth - y) / edgeWidth
        : y > height - edgeWidth ? (y - (height - edgeWidth)) / edgeWidth : 0.0f;
    if (verticalStrength > 0.0f)
    {
        const auto direction = y < edgeWidth ? -1.0f : 1.0f;
        updateVerticalScroll(verticalScrollOffset + juce::roundToInt((2.0f + verticalStrength * 11.0f) * direction));
        changed = true;
    }
    return changed;
}

void TimelineGrid::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (trackModel == nullptr)
        return;

    const auto currentZoom = trackModel->getHorizontalZoom();
    if (event.mods.isCtrlDown())
    {
        zoomVerticalBy(wheel.deltaY > 0.0f ? 8 : -8);
        return;
    }
    if (event.mods.isCommandDown())
    {
        const auto pivotSample = sampleAt(event.position);
        const auto zoomMultiplier = wheel.deltaY > 0.0f ? 1.12f : 1.0f / 1.12f;
        const auto next = TimelineZoomPolicy::zoomAroundSample(viewportController.state(), pivotSample,
            currentZoom * zoomMultiplier, 80.0, trackModel->getSampleRate(), event.position.x);
        viewportController.setState(next);
        return;
    }

    // A wheel and a two-finger trackpad gesture are navigation, not an
    // accidental zoom gesture.  Pinch and Command-wheel own zooming.
    const auto horizontalGesture = event.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY);
    if (! horizontalGesture && wheel.deltaY != 0.0f)
    {
        // JUCE normalises wheel and precision-touchpad deltas on Windows and
        // macOS, so vertical navigation stays platform-neutral.
        const auto pixels = juce::roundToInt(-wheel.deltaY * 84.0f);
        updateVerticalScroll(verticalScrollOffset + pixels);
        return;
    }
    const auto gestureDelta = horizontalGesture && wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
    if (gestureDelta != 0.0f)
    {
        const auto pixelsPerSecond = 80.0 * currentZoom;
        // Precision trackpads report a tiny delta for each sample.  A DAW
        // should cross a useful part of the arrange view per gesture, not
        // make the user repeat the same two-finger swipe several times.
        constexpr double horizontalWheelPanGain = 60.0;
        const auto delta = static_cast<double>(gestureDelta)
            * trackModel->getSampleRate() * horizontalWheelPanGain / pixelsPerSecond;
        updateView(viewStartSample - delta, currentZoom);
    }
}

void TimelineGrid::mouseMagnify(const juce::MouseEvent& event, float scaleFactor)
{
    if (trackModel == nullptr || scaleFactor <= 0.0f)
        return;

   #if JUCE_MAC
    // Pinch owns time zoom; Control-pinch owns lane zoom. Keeping a gesture
    // on one axis avoids the old diagonal "fight" when high-rate touchpad
    // events arrived during a vertical zoom.
    if (event.mods.isCtrlDown())
    {
        zoomVerticalByFactor(std::pow(scaleFactor, 0.72f));
        return;
    }
    const auto next = TimelineZoomPolicy::zoomAroundSample(viewportController.state(), sampleAt(event.position),
        trackModel->getHorizontalZoom() * scaleFactor, 80.0, trackModel->getSampleRate(), event.position.x);
    viewportController.setState(next);
   #else
    const auto next = TimelineZoomPolicy::zoomAroundSample(viewportController.state(), sampleAt(event.position),
        trackModel->getHorizontalZoom() * scaleFactor, 80.0, trackModel->getSampleRate(), event.position.x);
    viewportController.setState(next);
   #endif
}

void TimelineGrid::fitAllToWindow()
{
    if (trackModel == nullptr || trackModel->getTrackCount() == 0)
        return;

    auto endSample = trackModel->getSampleRate() * 4.0;
    for (size_t track = 0; track < trackModel->getTrackCount(); ++track)
        for (const auto& clip : trackModel->getTrack(track).clips)
            endSample = juce::jmax(endSample, clip.startSample + clip.durationSamples);
    for (const auto& clip : trackModel->getMidiClips())
        endSample = juce::jmax(endSample, clip.startSample
            + midiClipDurationSamples(clip, trackModel->getSampleRate()));

    selectedTrackZoomed = false;
    updateVerticalScroll(0);
    viewportController.setState(TimelineZoomPolicy::fitProject(viewportController.state(), endSample,
        getWidth(), getHeight(), static_cast<int>(trackModel->getTrackCount()), trackModel->getSampleRate()));
}

void TimelineGrid::toggleSelectedTrackZoom()
{
    if (trackModel == nullptr || selectedTrack < 0)
        return;

    if (! selectedTrackZoomed)
    {
        savedTrackHeight = trackModel->getTrackHeight();
        savedVerticalScrollOffset = verticalScrollOffset;
        savedViewStartSample = viewStartSample;
        savedHorizontalZoom = trackModel->getHorizontalZoom();
        auto next = viewportController.state();
        next.trackHeight = juce::jlimit(96, 160, getHeight() - 12);
        viewportController.setState(next);
        updateVerticalScroll(selectedTrack * trackModel->getTrackHeight());
        selectedTrackZoomed = true;
    }
    else
    {
        auto next = viewportController.state();
        next.trackHeight = savedTrackHeight;
        next.viewStartSample = savedViewStartSample;
        next.horizontalZoom = savedHorizontalZoom;
        viewportController.setState(next);
        updateVerticalScroll(savedVerticalScrollOffset);
        selectedTrackZoomed = false;
    }
}

bool TimelineGrid::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && (audioRegionPhase != AudioRegionInteractionPhase::idle
        || marqueeSelecting || marqueeSelectionValid || zoomSelecting || panningTimeline || scrubbingPlayhead
        || trimmingMidiClipLeft || trimmingMidiClipRight))
    {
        cancelAudioRegionInteraction();
        marqueeSelecting = marqueeSelectionValid = zoomSelecting = panningTimeline = scrubbingPlayhead = false;
        trimmingMidiClipLeft = trimmingMidiClipRight = false;
        selectedMidiClipId = {};
        edgeAutoScrollActive = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
        return true;
    }
    const auto hasPrimaryShortcutModifier = key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown();
    // These commands deliberately use the platform's primary modifier. JUCE
    // exposes Command on macOS and Ctrl on Windows through the same semantic
    // check, keeping the interaction portable without platform branches.
    if (hasPrimaryShortcutModifier)
    {
        const auto centre = static_cast<float>(getWidth()) * 0.5f;
        if (key.getKeyCode() == juce::KeyPress::rightKey) { zoomHorizontalBy(1.25f, centre); return true; }
        if (key.getKeyCode() == juce::KeyPress::leftKey) { zoomHorizontalBy(1.0f / 1.25f, centre); return true; }
        if (key.getKeyCode() == juce::KeyPress::downKey) { zoomVerticalBy(10); return true; }
        if (key.getKeyCode() == juce::KeyPress::upKey) { zoomVerticalBy(-10); return true; }
        if (key.getKeyCode() == ',' || key.getKeyCode() == '<') return navigateZoomHistory(false);
        if (key.getKeyCode() == '.' || key.getKeyCode() == '>') return navigateZoomHistory(true);
    }
    if (key.getModifiers().isAltDown() && (key.getKeyCode() >= '1' && key.getKeyCode() <= '3'))
    {
        const auto slot = key.getKeyCode() - '0';
        if (hasPrimaryShortcutModifier) saveZoomPreset(slot);
        else recallZoomPreset(slot);
        return true;
    }
    if ((key.getKeyCode() == 'z' || key.getKeyCode() == 'Z') && ! hasPrimaryShortcutModifier)
    {
        if (key.getModifiers().isShiftDown())
            fitAllToWindow();
        else
            toggleSelectedTrackZoom();
        return true;
    }
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        && marqueeSelectionValid && trackModel != nullptr)
    {
        const auto start = juce::jmin(marqueeAnchorSample, marqueeCurrentSample);
        const auto end = juce::jmax(marqueeAnchorSample, marqueeCurrentSample);
        const auto first = juce::jmin(marqueeAnchorTrack, marqueeCurrentTrack);
        const auto last = juce::jmax(marqueeAnchorTrack, marqueeCurrentTrack);
        const auto changed = trackModel->deleteAudioRange(start, end, first, last);
        marqueeSelectionValid = false;
        repaint();
        return changed;
    }
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        && selectedMidiClipId.isValid() && trackModel != nullptr)
    {
        const auto deleted = trackModel->deleteMidiClip(selectedMidiClipId);
        if (deleted) selectedMidiClipId = {};
        repaint();
        return deleted;
    }
    if (selectedMidiClipId.isValid() && trackModel != nullptr)
    {
        const auto found = std::find_if(trackModel->getMidiClips().begin(), trackModel->getMidiClips().end(), [this] (const auto& clip)
        {
            return clip.id == selectedMidiClipId;
        });
        if (found != trackModel->getMidiClips().end())
        {
            const auto samplesPerBeat = trackModel->getSampleRate() * 60.0 / trackModel->getBpm();
            const auto step = samplesPerBeat * (key.getModifiers().isShiftDown() ? tickSnapInBeats : fineSnapInBeats);
            if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
            {
                const auto direction = key.getKeyCode() == juce::KeyPress::leftKey ? -1.0 : 1.0;
                trackModel->moveMidiClip(selectedMidiClipId, found->trackId,
                                         juce::jmax(0.0, found->startSample + direction * step));
                repaint();
                return true;
            }
            if (hasPrimaryShortcutModifier && (key.getKeyCode() == 'd' || key.getKeyCode() == 'D'))
            {
                const auto duplicate = trackModel->duplicateMidiClip(selectedMidiClipId, found->trackId,
                                                                       found->startSample + midiClipDurationSamples(*found, trackModel->getSampleRate()));
                if (! duplicate.isValid())
                    return false;
                selectedMidiClipId = duplicate;
                repaint();
                return true;
            }
        }
    }
    if (hasPrimaryShortcutModifier && (key.getKeyCode() == 't' || key.getKeyCode() == 'T')
        && selectedClipId.isValid() && selectedTrack >= 0 && trackModel != nullptr)
    {
        const auto& clips = trackModel->getTrack(static_cast<size_t>(selectedTrack)).clips;
        const auto found = std::find_if(clips.begin(), clips.end(), [this] (const AudioClipState& clip)
        {
            return clip.id == selectedClipId;
        });
        if (found == clips.end())
            return false;

        trackModel->splitAudioClip(static_cast<size_t>(selectedTrack),
                                   static_cast<size_t>(std::distance(clips.begin(), found)),
                                   trackModel->getPlayheadPosition());
        repaint();
        return true;
    }

    if ((key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
        && selectedClipId.isValid() && selectedTrack >= 0 && trackModel != nullptr)
    {
        const auto& clips = trackModel->getTrack(static_cast<size_t>(selectedTrack)).clips;
        const auto found = std::find_if(clips.begin(), clips.end(), [this] (const AudioClipState& clip) { return clip.id == selectedClipId; });
        if (found == clips.end()) return false;

        const auto samplesPerBeat = trackModel->getSampleRate() * 60.0 / trackModel->getBpm();
        const auto step = samplesPerBeat * (key.getModifiers().isShiftDown() ? tickSnapInBeats : fineSnapInBeats);
        const auto direction = key.getKeyCode() == juce::KeyPress::leftKey ? -1.0 : 1.0;
        trackModel->moveAudioClip(selectedClipId, trackModel->getTrackId(static_cast<size_t>(selectedTrack)),
                                  juce::jmax(0.0, found->startSample + direction * step));
        repaint();
        return true;
    }

    if (hasPrimaryShortcutModifier && (key.getKeyCode() == 'd' || key.getKeyCode() == 'D')
        && selectedClipId.isValid() && selectedTrack >= 0 && trackModel != nullptr)
    {
        const auto& clips = trackModel->getTrack(static_cast<size_t>(selectedTrack)).clips;
        const auto found = std::find_if(clips.begin(), clips.end(), [this](const AudioClipState& clip) { return clip.id == selectedClipId; });
        if (found == clips.end()) return false;
        const auto duplicate = trackModel->duplicateAudioClip(selectedClipId,
            trackModel->getTrackId(static_cast<size_t>(selectedTrack)), found->startSample + found->durationSamples);
        if (! duplicate.isValid()) return false;
        selectedClipId = duplicate;
        selectedClip = -1;
        repaint();
        return true;
    }
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        && selectedClipId.isValid() && trackModel != nullptr)
    {
        trackModel->deleteAudioClip(selectedClipId);
        selectedTrack = selectedClip = -1;
        selectedClipId = {};
        repaint();
        return true;
    }
    return false;
}

void TimelineGrid::paint(juce::Graphics& g)
{
    g.fillAll(StudioForgeTheme::workspaceBackground);

    if (trackModel == nullptr)
        return;

    const auto localBounds = getLocalBounds();
    const auto width = static_cast<float>(localBounds.getWidth());
    const auto height = static_cast<float>(localBounds.getHeight());
    const auto trackHeight = static_cast<float>(trackModel->getTrackHeight());

    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    auto gridSample = trackModel->getSnappedSamplePosition(viewStartSample, 1.0);
    if (gridSample < viewStartSample) gridSample += trackModel->getSampleRate() * 60.0 / trackModel->getTempoAtSample(gridSample);
    while (gridSample <= viewStartSample + trackModel->getSampleRate() * width / pixelsPerSecond)
    {
        const auto signature = trackModel->getTimeSignatureAtSample(gridSample);
        const auto samplesPerBeat = trackModel->getSampleRate() * 60.0 / trackModel->getTempoAtSample(gridSample)
            * 4.0 / signature.denominator;
        const auto x = trackModel->sampleToXPosition(gridSample - viewStartSample, pixelsPerSecond);
        const auto barBeat = trackModel->getBarBeatInfo(gridSample);
        const auto isBar = barBeat.beats == 0 && barBeat.pulses == 0;
        g.setColour(isBar ? accentBlue.withAlpha(0.45f) : juce::Colours::white.withAlpha(0.18f));
        g.drawVerticalLine(juce::roundToInt(x), 0.0f, height);
        gridSample += samplesPerBeat;
    }

    const auto trackCount = static_cast<int>(trackModel->getTrackCount());
    for (int track = 0; track < trackCount; ++track)
    {
        const auto rowY = static_cast<float>(track) * trackHeight - verticalScrollOffset;
        g.setColour(track == selectedTrack ? juce::Colour(0xff52697d).withAlpha(0.20f) : juce::Colours::white.withAlpha(0.04f));
        g.fillRect(0.0f, rowY, width, trackHeight);
        if (track == audioFileDropTarget)
        {
            g.setColour(accentCyan.withAlpha(0.19f));
            g.fillRect(0.0f, rowY, width, trackHeight);
            g.setColour(accentCyan.withAlpha(0.82f));
            g.drawRoundedRectangle({ 0.5f, rowY + 1.0f, width - 1.0f, trackHeight - 2.0f }, 3.0f, 1.2f);
        }

        const auto& state = trackModel->getTrack(static_cast<size_t>(track));
        for (const auto& clip : state.clips)
        {
            const auto previewingTrim = (trimmingLeft || trimmingRight) && clip.id == selectedClipId;
            const auto previewingDrag = draggingClip && clip.id == selectedClipId && clipDragPreviewTrack >= 0;
            const auto displayTrack = previewingDrag ? clipDragPreviewTrack : track;
            const auto displayStart = previewingDrag ? clipDragPreviewSample
                : previewingTrim ? clipTrimPreviewStartSample : clip.startSample;
            const auto displayDuration = previewingTrim ? clipTrimPreviewDurationSamples : clip.durationSamples;
            const auto displayX = trackModel->sampleToXPosition(displayStart - viewStartSample, pixelsPerSecond);
            const auto displayY = static_cast<float>(displayTrack) * trackHeight - verticalScrollOffset;
            const auto rectWidth = trackModel->sampleToXPosition(displayDuration, pixelsPerSecond);
            const auto rect = juce::Rectangle<float>(displayX, displayY + 3.0f, rectWidth, trackHeight - 6.0f);
            if (rect.getWidth() <= 0.0f)
                continue;

            const auto thumbnail = waveformCache != nullptr ? waveformCache->find(clip.sourceFile) : nullptr;
            drawPremiumWaveform(g, clip, thumbnail.get(), rect,
                                { selection.contains(clip.id),
                                  state.muted.load(std::memory_order_relaxed) }, waveformZoom);
            if (rect.getWidth() >= 30.0f)
            {
                const auto loopHandle = juce::Rectangle<float>(rect.getRight() - 15.0f, rect.getY() + 2.0f, 13.0f, 11.0f);
                const auto loopHovered = clip.id == hoveredAudioClipId && hoveredAudioRegionZone == AudioRegionMouseZone::loop;
                g.setColour((clip.loopEnabled ? accentCyan : juce::Colours::white).withAlpha(loopHovered ? 0.95f : clip.loopEnabled ? 0.70f : 0.28f));
                g.drawRoundedRectangle(loopHandle.reduced(2.0f, 3.0f), 2.0f, 1.2f);
                g.drawLine(loopHandle.getX() + 4.0f, loopHandle.getCentreY(), loopHandle.getRight() - 4.0f, loopHandle.getCentreY(), 1.2f);
            }
            const auto isSelected = selection.contains(clip.id);
            const auto isHovered = clip.id == hoveredAudioClipId;
            if (isSelected || isHovered)
            {
                const auto previewingFade = isSelected && (adjustingFadeIn || adjustingFadeOut) && clip.id == selectedClipId;
                const auto fadeInSamples = previewingFade ? clipFadePreviewInSamples : clip.fadeInSamples;
                const auto fadeOutSamples = previewingFade ? clipFadePreviewOutSamples : clip.fadeOutSamples;
                const auto fadeInX = rect.getX() + static_cast<float>(trackModel->sampleToXPosition(fadeInSamples, pixelsPerSecond));
                const auto fadeOutX = rect.getRight() - static_cast<float>(trackModel->sampleToXPosition(fadeOutSamples, pixelsPerSecond));
                const auto interactionAlpha = isSelected ? 0.62f : 0.36f;
                g.setColour(juce::Colours::white.withAlpha(interactionAlpha));
                g.drawLine(rect.getX(), rect.getBottom() - 3.0f, fadeInX, rect.getY() + 3.0f, 1.5f);
                g.drawLine(fadeOutX, rect.getY() + 3.0f, rect.getRight(), rect.getBottom() - 3.0f, 1.5f);
                const auto leftHovered = isHovered && (hoveredAudioRegionZone == AudioRegionMouseZone::leftTrim
                    || hoveredAudioRegionZone == AudioRegionMouseZone::fadeIn);
                const auto rightHovered = isHovered && (hoveredAudioRegionZone == AudioRegionMouseZone::rightTrim
                    || hoveredAudioRegionZone == AudioRegionMouseZone::fadeOut);
                // Moving a clip stays visually quiet. Trim/fade affordances
                // are cursor-driven rather than permanent vertical grips.
                juce::ignoreUnused(leftHovered, rightHovered);
                g.setColour(juce::Colour(0xffc9e5ff).withAlpha(isSelected ? 1.0f : 0.55f));
                g.drawRoundedRectangle(rect.reduced(1.0f), 5.0f, isSelected ? 2.0f : 1.0f);
            }
        }

        if (track == recordingPreviewTrack && displayPlayheadSample > recordingPreviewStartSample)
        {
            const auto startX = trackModel->sampleToXPosition(recordingPreviewStartSample - viewStartSample, pixelsPerSecond);
            const auto endX = trackModel->sampleToXPosition(displayPlayheadSample - viewStartSample, pixelsPerSecond);
            const auto recordingBounds = juce::Rectangle<float>(startX, rowY + 3.0f, endX - startX, trackHeight - 6.0f);
            g.setColour(juce::Colour(0xff1d7d8d).withAlpha(0.56f));
            g.fillRect(recordingBounds);
            g.setColour(juce::Colour(0xffed6870));
            g.fillRect(recordingBounds.withHeight(2.0f));
            g.setColour(juce::Colour(0xffb9f5fb).withAlpha(0.82f));
            g.drawRect(recordingBounds, 1.0f);
            g.setFont(10.0f);
            g.drawText("REC", recordingBounds.reduced(7.0f, 3.0f), juce::Justification::topLeft, false);
            if (recordingEnvelopeCount > 1 && recordingBounds.getWidth() > 3.0f)
            {
                const auto centreY = recordingBounds.getCentreY();
                const auto first = (recordingEnvelopeWrite + recordingEnvelope.size() - recordingEnvelopeCount)
                    % recordingEnvelope.size();
                g.setColour(juce::Colour(0xffd9fbff).withAlpha(0.9f));
                for (size_t point = 0; point < recordingEnvelopeCount; ++point)
                {
                    const auto peak = juce::jlimit(0.0f, 1.0f,
                        recordingEnvelope[(first + point) % recordingEnvelope.size()]);
                    const auto x = recordingBounds.getX() + recordingBounds.getWidth()
                        * static_cast<float>(point) / static_cast<float>(recordingEnvelopeCount - 1);
                    const auto amplitude = peak * (recordingBounds.getHeight() * 0.42f);
                    g.drawVerticalLine(juce::roundToInt(x), centreY - amplitude, centreY + amplitude);
                }
            }
        }

        for (const auto& midiClip : trackModel->getMidiClips())
        {
            if (midiClip.trackId != state.id) continue;

            const auto duration = midiClipDurationSamples(midiClip, trackModel->getSampleRate());
            const auto previewingTrim = (trimmingMidiClipLeft || trimmingMidiClipRight) && midiClip.id == selectedMidiClipId;
            const auto displayStart = previewingTrim ? midiClipTrimPreviewStartSample : midiClip.startSample;
            const auto displayDuration = previewingTrim
                ? midiClipTrimPreviewDurationSamples : duration;
            const auto startX = trackModel->sampleToXPosition(displayStart - viewStartSample, pixelsPerSecond);
            const auto endX = trackModel->sampleToXPosition(displayStart + displayDuration - viewStartSample, pixelsPerSecond);
            const auto midiRect = juce::Rectangle<float>(startX, rowY + 7.0f, endX - startX, trackHeight - 14.0f);
            if (midiRect.getWidth() <= 0.0f) continue;
            g.setColour(juce::Colour(0xff6a67bf));
            g.fillRoundedRectangle(midiRect, 5.0f);
            g.setColour(juce::Colour(0xffbdb9ff));
            g.drawRoundedRectangle(midiRect, 5.0f, 1.0f);
            if (midiClip.id == selectedMidiClipId)
            {
                g.setColour(juce::Colours::white.withAlpha(0.85f));
                g.fillRect(midiRect.getRight() - 1.5f, midiRect.getY() + 3.0f, 2.0f, midiRect.getHeight() - 6.0f);
            }
            for (const auto& note : midiClip.notes)
            {
                const auto noteX = startX + trackModel->sampleToXPosition(note.startSample - midiClip.contentOffsetSamples, pixelsPerSecond);
                const auto noteWidth = juce::jmax(2.0f, static_cast<float>(trackModel->sampleToXPosition(note.durationSamples, pixelsPerSecond)));
                const auto noteY = midiRect.getBottom() - 5.0f - (static_cast<float>(note.pitch) / 127.0f) * (midiRect.getHeight() - 10.0f);
                g.setColour(juce::Colour(0xffe2e0ff).withAlpha(0.75f));
                g.fillRoundedRectangle(noteX, noteY, noteWidth, 3.0f, 1.0f);
            }
            g.setColour(juce::Colours::white.withAlpha(0.88f));
            g.setFont(10.0f);
            g.drawText("MIDI", midiRect.reduced(6.0f, 2.0f), juce::Justification::topLeft, false);
        }
    }

    if (marqueeSelecting || marqueeSelectionValid)
    {
        const auto firstSample = juce::jmin(marqueeAnchorSample, marqueeCurrentSample);
        const auto lastSample = juce::jmax(marqueeAnchorSample, marqueeCurrentSample);
        const auto firstTrack = juce::jmin(marqueeAnchorTrack, marqueeCurrentTrack);
        const auto lastTrack = juce::jmax(marqueeAnchorTrack, marqueeCurrentTrack);
        const auto x = static_cast<float>(trackModel->sampleToXPosition(firstSample - viewStartSample, pixelsPerSecond));
        const auto right = static_cast<float>(trackModel->sampleToXPosition(lastSample - viewStartSample, pixelsPerSecond));
        const auto y = firstTrack * trackHeight - verticalScrollOffset;
        const auto bottom = (lastTrack + 1) * trackHeight - verticalScrollOffset;
        const auto selection = juce::Rectangle<float>::leftTopRightBottom(x, y, right, bottom);
        g.setColour(StudioForgeTheme::accentCyan.withAlpha(0.16f));
        g.fillRect(selection);
        g.setColour(StudioForgeTheme::accentCyan.withAlpha(0.95f));
        g.drawRect(selection, 1.5f);
    }
    if (zoomSelecting)
    {
        const auto selection = juce::Rectangle<float>::leftTopRightBottom(
            juce::jmin(zoomSelectionAnchor.x, zoomSelectionCurrent.x), juce::jmin(zoomSelectionAnchor.y, zoomSelectionCurrent.y),
            juce::jmax(zoomSelectionAnchor.x, zoomSelectionCurrent.x), juce::jmax(zoomSelectionAnchor.y, zoomSelectionCurrent.y));
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.fillRect(selection);
        g.setColour(StudioForgeTheme::accentCyan.withAlpha(0.95f));
        g.drawRect(selection, 1.5f);
    }

    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillRect(0.0f, static_cast<float>(trackCount) * trackHeight, width, 2.0f);

    const auto playheadX = trackModel->sampleToXPosition(
        displayPlayheadSample - viewStartSample, pixelsPerSecond);
    if (playheadX >= 0.0 && playheadX <= width)
    {
        g.setColour(juce::Colour(0xffff5c5c));
        g.drawVerticalLine(juce::roundToInt(playheadX), 0.0f, height);
    }
}

ArrangeWindow::ArrangeWindow(TrackDataModel* model, AudioEngine* engine)
    : trackHeaderPanel(model, engine),
      timelineRuler(model),
      timelineGrid(model)
{
    trackModel = model;
    timelineGrid.setAudioEngine(engine);
    timelineGrid.setWaveformCache(&waveformCache);
    const auto initialPlayhead = model != nullptr ? model->getPlayheadPosition() : 0.0;
    timelineGrid.setDisplayPlayheadSample(initialPlayhead);
    timelineRuler.setDisplayPlayheadSample(initialPlayhead);
    addAndMakeVisible(trackHeaderPanel);
    addAndMakeVisible(timelineRuler);
    addAndMakeVisible(timelineGrid);
    horizontalZoomSlider.setRange(0.5, 4096.0, 0.01);
    horizontalZoomSlider.setSkewFactorFromMidPoint(32.0);
    horizontalZoomSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    horizontalZoomSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    horizontalZoomSlider.setValue(model != nullptr ? model->getHorizontalZoom() : 1.0);
    horizontalZoomSlider.onValueChange = [this]
    {
        if (trackModel != nullptr)
        {
            const auto zoom = static_cast<float>(horizontalZoomSlider.getValue());
            zoomReadout.setText(zoomReadoutText(zoom), juce::dontSendNotification);
            setTimelineView(timelineGrid.getViewStartSample(), zoom);
        }
    };
    trackHeightSlider.setRange(28.0, 160.0, 1.0);
    trackHeightSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    trackHeightSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    trackHeightSlider.setValue(model != nullptr ? model->getTrackHeight() : 72);
    trackHeightSlider.onValueChange = [this]
    {
        if (trackModel != nullptr)
        {
            const auto height = juce::roundToInt(trackHeightSlider.getValue());
            trackHeightReadout.setText("H " + juce::String(height), juce::dontSendNotification);
            timelineGrid.setTrackHeight(height);
            trackHeaderPanel.repaint();
            timelineGrid.repaint();
        }
    };
    waveformZoomSlider.setRange(0.25, 8.0, 0.01);
    waveformZoomSlider.setSkewFactorFromMidPoint(1.0);
    waveformZoomSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    waveformZoomSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    waveformZoomSlider.setValue(1.0);
    waveformZoomSlider.onValueChange = [this]
    {
        const auto zoom = static_cast<float>(waveformZoomSlider.getValue());
        waveformZoomReadout.setText("W " + juce::String(zoom, 1) + "x", juce::dontSendNotification);
        timelineGrid.setWaveformZoom(zoom);
    };

    const auto configureReadout = [] (juce::Label& readout)
    {
        readout.setJustificationType(juce::Justification::centred);
        readout.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        readout.setColour(juce::Label::textColourId, juce::Colour(0xffedf3f7));
        readout.setColour(juce::Label::backgroundColourId, juce::Colour(0xff1a2025));
        readout.setColour(juce::Label::outlineColourId, juce::Colour(0xff63717b));
    };
    configureReadout(zoomReadout);
    configureReadout(trackHeightReadout);
    configureReadout(waveformZoomReadout);
    zoomReadout.setText(zoomReadoutText(horizontalZoomSlider.getValue()), juce::dontSendNotification);
    trackHeightReadout.setText("H " + juce::String(juce::roundToInt(trackHeightSlider.getValue())), juce::dontSendNotification);
    waveformZoomReadout.setText("W 1.0x", juce::dontSendNotification);
    addAndMakeVisible(horizontalZoomSlider);
    addAndMakeVisible(trackHeightSlider);
    addAndMakeVisible(waveformZoomSlider);
    addAndMakeVisible(zoomReadout);
    addAndMakeVisible(trackHeightReadout);
    addAndMakeVisible(waveformZoomReadout);
    addPerformanceButton.setTooltip("Configure Performance");
    addPerformanceButton.onClick = [this]
    {
        if (onPerformanceSetupRequested != nullptr)
            onPerformanceSetupRequested();
    };
    addAndMakeVisible(addPerformanceButton);
    snapButton.setTooltip("Snap is Off for live timing. Click to enable beat snapping; Cmd/Ctrl temporarily bypasses it.");
    snapButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff242a2f));
    snapButton.onClick = [this]
    {
        const auto enabled = ! timelineGrid.isSnapEnabled();
        timelineGrid.setSnapEnabled(enabled);
        snapButton.setButtonText(enabled ? "SNAP ON" : "SNAP OFF");
        snapButton.setColour(juce::TextButton::buttonColourId,
                             enabled ? StudioForgeTheme::accentBlue : juce::Colour(0xff242a2f));
    };
    addAndMakeVisible(snapButton);
    markerButton.setTooltip("Add song marker at the playhead (Option/Alt + ')");
    markerButton.onClick = [this] { addMarkerAtPlayhead(); };
    addAndMakeVisible(markerButton);
    startTimerHz(60);

    trackHeaderPanel.onTrackSelected = [this](int track)
    {
        timelineGrid.setSelectedTrack(track);
        if (onTrackSelected != nullptr) onTrackSelected(track);
    };
    timelineGrid.onTrackSelected = [this](int track)
    {
        trackHeaderPanel.setSelectedTrack(track);
        if (onTrackSelected != nullptr) onTrackSelected(track);
    };
    timelineGrid.onViewChanged = [this] (double startSample, float zoom)
    {
        timelineRuler.setViewStartSample(startSample);
        horizontalZoomSlider.setValue(zoom, juce::dontSendNotification);
        zoomReadout.setText(zoomReadoutText(zoom), juce::dontSendNotification);
        timelineRuler.repaint();
        ++waveformViewportGeneration;
        requestWaveformPreparation();
    };
    timelineGrid.onWaveformZoomChanged = [this] (float zoom)
    {
        waveformZoomSlider.setValue(zoom, juce::dontSendNotification);
        waveformZoomReadout.setText("W " + juce::String(zoom, 1) + "x", juce::dontSendNotification);
    };
    timelineGrid.onVerticalScrollChanged = [this] (int offset)
    {
        trackHeaderPanel.setVerticalScrollOffset(offset);
        ++waveformViewportGeneration;
        requestWaveformPreparation();
    };
    timelineGrid.onMidiClipSelected = [this](MidiClipId clip)
    {
        if (onMidiClipSelected != nullptr) onMidiClipSelected(clip);
    };
    timelineGrid.onAudioClipSelected = [this](ClipId clip)
    {
        if (onAudioClipSelected != nullptr) onAudioClipSelected(clip);
    };
    timelineGrid.onAudioClipOpenRequested = [this](ClipId clip)
    {
        if (onAudioClipOpenRequested != nullptr) onAudioClipOpenRequested(clip);
    };
    timelineGrid.onMidiClipOpenRequested = [this](MidiClipId clip)
    {
        if (onMidiClipOpenRequested != nullptr) onMidiClipOpenRequested(clip);
    };
}

void ArrangeWindow::timerCallback()
{
    if (trackModel == nullptr)
        return;

    if (isDraggingOver)
    {
        const auto timelinePosition = fileDragPosition - timelineGrid.getPosition().toFloat();
        if (timelineGrid.autoScrollForExternalFileDrag(timelinePosition))
            updateFileDropTarget(juce::roundToInt(fileDragPosition.x), juce::roundToInt(fileDragPosition.y));
    }

    const auto currentSample = trackModel->getPlayheadPosition();
    if (std::abs(currentSample - lastDisplayPlayheadSample) < 0.01)
        return;

    lastDisplayPlayheadSample = currentSample;
    // Both child components receive one UI-thread snapshot.  Their playhead
    // x-coordinate therefore remains identical even at extreme zoom levels.
    timelineRuler.setDisplayPlayheadSample(currentSample);
    timelineGrid.setDisplayPlayheadSample(currentSample);
    timelineGrid.followPlaybackPosition(currentSample);
}

void ArrangeWindow::paint(juce::Graphics& g)
{
    g.fillAll(panelBackground);
    const auto controlStrip = juce::Rectangle<float>(static_cast<float>(getWidth() - 236), 0.0f, 236.0f, 29.0f);
    g.setColour(StudioForgeTheme::raisedSurface.darker(0.10f));
    g.fillRect(controlStrip);
    g.setColour(juce::Colours::black.withAlpha(0.62f));
    g.drawVerticalLine(controlStrip.getX(), controlStrip.getY(), controlStrip.getBottom());
    if (isDraggingOver)
    {
        const auto trackHeight = static_cast<float>(trackModel != nullptr ? trackModel->getTrackHeight() : 60);
        const auto timelineBounds = timelineGrid.getBounds().toFloat();
        const auto sampleX = timelineBounds.getX() + static_cast<float>(trackModel->sampleToXPosition(
            draggedSample - timelineGrid.getViewStartSample(), 80.0 * trackModel->getHorizontalZoom()));
        if (dropCreatesAudioTrack)
        {
            const auto insertionY = timelineBounds.getY()
                + static_cast<float>(juce::jmax(0, dropInsertionIndex)) * trackHeight - timelineGrid.getVerticalScrollOffset();
            g.setColour(accentCyan.withAlpha(0.95f));
            g.fillRect(timelineBounds.getX(), insertionY - 2.0f, timelineBounds.getWidth(), 4.0f);
            g.setColour(StudioForgeTheme::primaryText);
            g.setFont(11.0f);
            g.drawText("Drop to create Audio Track", timelineBounds.getX() + 8.0f, insertionY + 5.0f, 180.0f, 16.0f,
                       juce::Justification::centredLeft, false);
        }
        else if (draggedTrack >= 0)
        {
            const auto rowY = timelineBounds.getY() + static_cast<float>(draggedTrack) * trackHeight
                - timelineGrid.getVerticalScrollOffset();
            const auto row = juce::Rectangle<float>(timelineBounds.getX(), rowY, timelineBounds.getWidth(), trackHeight);
            g.setColour(accentCyan.withAlpha(0.12f));
            g.fillRect(row);
            g.setColour(accentCyan.withAlpha(0.90f));
            g.drawRoundedRectangle(row.reduced(1.0f), 3.0f, 1.5f);

            const auto durationSamples = draggedAudioMetadata.isValid()
                ? draggedAudioMetadata.durationSeconds() * trackModel->getSampleRate()
                : trackModel->getSampleRate() * 2.0;
            const auto fullGhostWidth = static_cast<float>(trackModel->sampleToXPosition(durationSamples,
                                                                                           80.0 * trackModel->getHorizontalZoom()));
            const auto ghost = juce::Rectangle<float>(sampleX, rowY + 5.0f, fullGhostWidth, trackHeight - 10.0f)
                .getIntersection(row.reduced(3.0f));
            g.setColour(accentCyan.withAlpha(0.90f));
            g.drawVerticalLine(sampleX, timelineBounds.getY(), timelineBounds.getBottom());
            if (! ghost.isEmpty())
            {
                g.setColour(accentCyan.withAlpha(0.26f));
                g.fillRoundedRectangle(ghost, 4.0f);
                g.setColour(accentCyan.withAlpha(0.85f));
                juce::Path waveform;
                const auto middle = ghost.getCentreY();
                waveform.startNewSubPath(ghost.getX() + 5.0f, middle);
                for (int point = 1; point < 28; ++point)
                {
                    const auto x = ghost.getX() + 5.0f + (ghost.getWidth() - 10.0f) * point / 28.0f;
                    const auto seed = static_cast<float>((point * 17 + draggedAudioFiles.size() * 11) % 13) / 12.0f;
                    waveform.lineTo(x, middle + (seed - 0.5f) * ghost.getHeight() * 0.58f);
                }
                g.strokePath(waveform, juce::PathStrokeType(1.15f));
            }

            const auto metadata = draggedAudioMetadata.description();
            const auto detail = AudioImportSupport::summaryLabel(draggedAudioFiles) + "  •  " + metadata
                + "  •  Snap " + (timelineGrid.isSnapEnabled() ? "On" : "Off");
            g.setColour(StudioForgeTheme::primaryText.withAlpha(0.90f));
            g.setFont(10.0f);
            const auto labelBounds = juce::Rectangle<float>(sampleX + 6.0f, rowY - 20.0f,
                                                             juce::jmax(160.0f, ghost.getWidth()), 16.0f)
                .getIntersection(timelineBounds.reduced(2.0f));
            g.drawText(detail, labelBounds, juce::Justification::centredLeft, true);
        }
    }
}

void ArrangeWindow::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(timelineRulerHeight);
    // At a 1600px desktop this produces the ~260px header lane from the
    // reference console, while preserving a useful timeline at smaller sizes.
    const auto leftWidth = juce::jlimit(228, 282, juce::roundToInt(static_cast<float>(getWidth()) * 0.24f));
    timelineRuler.setBounds(leftWidth, 0, juce::jmax(0, getWidth() - leftWidth), timelineRulerHeight);
    timelineRuler.setReservedTrailingWidth(0);
    addPerformanceButton.setBounds(juce::jmax(2, leftWidth - 28), 3, 22, 22);
    snapButton.setVisible(false);
    markerButton.setVisible(false);
    trackHeaderPanel.setBounds(area.removeFromLeft(leftWidth));
    timelineGrid.setBounds(area);
    // Zoom is intentionally gesture-first: pinch/Cmd-wheel changes horizontal
    // zoom and wheel/trackpad handles navigation. Keep the ruler unobstructed.
    zoomReadout.setVisible(false);
    horizontalZoomSlider.setVisible(false);
    trackHeightReadout.setVisible(false);
    trackHeightSlider.setVisible(false);
    waveformZoomSlider.setVisible(false);
    waveformZoomReadout.setVisible(false);
}

bool ArrangeWindow::isInterestedInFileDrag(const juce::StringArray& files)
{
    return ! AudioImportSupport::supportedFiles(files).isEmpty();
}

void ArrangeWindow::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    draggedAudioFiles = AudioImportSupport::supportedFiles(files);
    draggedAudioMetadata = draggedAudioFiles.isEmpty() ? AudioImportSupport::AudioFileMetadata {}
                                                        : AudioImportSupport::readMetadata(draggedAudioFiles.getFirst());
    fileDragPosition = { static_cast<float>(x), static_cast<float>(y) };
    updateFileDropTarget(x, y);
    isDraggingOver = true;
    repaint();
}

void ArrangeWindow::fileDragMove(const juce::StringArray&, int x, int y)
{
    fileDragPosition = { static_cast<float>(x), static_cast<float>(y) };
    updateFileDropTarget(x, y);
    isDraggingOver = true;
    repaint();
}

void ArrangeWindow::fileDragExit(const juce::StringArray&)
{
    clearFileDropPreview();
}

void ArrangeWindow::filesDropped(const juce::StringArray& files, int x, int y)
{
    draggedAudioFiles = AudioImportSupport::supportedFiles(files);
    draggedAudioMetadata = draggedAudioFiles.isEmpty() ? AudioImportSupport::AudioFileMetadata {}
                                                        : AudioImportSupport::readMetadata(draggedAudioFiles.getFirst());
    fileDragPosition = { static_cast<float>(x), static_cast<float>(y) };
    updateFileDropTarget(x, y);
    if (trackModel == nullptr || draggedAudioFiles.isEmpty())
    {
        clearFileDropPreview();
        return;
    }

    const auto audioFiles = draggedAudioFiles;
    const auto targetTrack = draggedTrack;
    const auto targetSample = draggedSample;
    const auto insertionIndex = dropInsertionIndex;
    clearFileDropPreview();
    importDroppedAudioFiles(audioFiles, targetTrack, targetSample, insertionIndex);
}

void ArrangeWindow::updateFileDropTarget(int x, int y)
{
    draggedTrack = -1;
    dropCreatesAudioTrack = false;
    dropInsertionIndex = -1;
    if (trackModel == nullptr)
        return;

    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto timelineX = juce::jmax(0, x - timelineGrid.getX());
    const auto rawSample = timelineGrid.getViewStartSample()
        + static_cast<double>(timelineX) / pixelsPerSecond * trackModel->getSampleRate();
    draggedSample = timelineGrid.isSnapEnabled() ? trackModel->getSnappedSamplePosition(rawSample, 0.25)
                                                  : juce::jmax(0.0, rawSample);

    const auto localY = y - timelineGrid.getY() + timelineGrid.getVerticalScrollOffset();
    const auto rowHeight = juce::jmax(1, trackModel->getTrackHeight());
    const auto candidateTrack = localY >= 0 ? localY / rowHeight : -1;
    if (isAudioTrack(*trackModel, candidateTrack))
        draggedTrack = candidateTrack;
    else
    {
        dropCreatesAudioTrack = true;
        dropInsertionIndex = juce::jlimit(0, static_cast<int>(trackModel->getTrackCount()),
                                          candidateTrack < 0 ? 0 : candidateTrack);
    }

    // File drops are accepted by the arrangement lane, not by its track
    // header controls. Keeping the header neutral avoids implying that an
    // input channel or mixer strip is the drop destination.
    trackHeaderPanel.setAudioFileDropTarget(-1);
    timelineGrid.setAudioFileDropTarget(draggedTrack);
}

void ArrangeWindow::clearFileDropPreview() noexcept
{
    isDraggingOver = false;
    draggedTrack = -1;
    dropCreatesAudioTrack = false;
    dropInsertionIndex = -1;
    draggedAudioFiles.clear();
    draggedAudioMetadata = {};
    timelineGrid.setAudioFileDropTarget(-1);
    repaint();
}

void ArrangeWindow::setTimelineView(double startSample, float zoom)
{
    timelineGrid.setViewStartSample(startSample);
    timelineRuler.setViewStartSample(startSample);
    if (trackModel != nullptr)
        trackModel->setHorizontalZoom(zoom);
    horizontalZoomSlider.setValue(zoom, juce::dontSendNotification);
    zoomReadout.setText(zoomReadoutText(zoom), juce::dontSendNotification);
    timelineRuler.repaint();
    timelineGrid.repaint();
}

void ArrangeWindow::setSelectedTrack(int trackIndex) noexcept
{
    trackHeaderPanel.setSelectedTrack(trackIndex);
    timelineGrid.setSelectedTrack(trackIndex);
}

void ArrangeWindow::addMarkerAtPlayhead()
{
    if (trackModel == nullptr)
        return;
    trackModel->addMarker(trackModel->getPlayheadPosition());
    timelineRuler.repaint();
}

void ArrangeWindow::showRecordingPreview(int trackIndex, double startSample) noexcept
{
    timelineGrid.setRecordingPreview(trackIndex, startSample);
}

void ArrangeWindow::hideRecordingPreview() noexcept
{
    timelineGrid.setRecordingPreview(-1, 0.0);
}

void ArrangeWindow::importAudioFile(const juce::File& file, int trackIndex, double startSample)
{
    importAudioFileAt(file, trackIndex, startSample, -1);
}

void ArrangeWindow::importAudioFileAt(const juce::File& file, int trackIndex, double startSample,
                                      int insertionIndex)
{
    if (trackModel == nullptr || ! file.existsAsFile())
        return;

    const auto metadata = AudioImportSupport::readMetadata(file);
    if (! metadata.isValid())
        return;

    const auto resolvedTrack = resolveAudioImportTrack(trackIndex, insertionIndex);
    if (resolvedTrack < 0)
        return;
    if (onTrackSelected != nullptr)
        onTrackSelected(resolvedTrack);
    if (onAudioFileImported != nullptr)
        onAudioFileImported(resolvedTrack, file);

    const auto snappedStart = timelineGrid.isSnapEnabled()
        ? trackModel->getSnappedSamplePosition(startSample, 0.25) : juce::jmax(0.0, startSample);
    if (const auto cachedSource = trackModel->findDecodedAudioSource(file); cachedSource != nullptr)
    {
        trackModel->addClipToTrack(resolvedTrack, file, snappedStart, cachedSource);
        repaint();
        return;
    }

    const auto placeholder = trackModel->addUnloadedClipToTrack(resolvedTrack, file, snappedStart,
        metadata.durationSeconds() * trackModel->getSampleRate());
    if (! placeholder.isValid())
        return;

    const auto sourceKey = AudioMediaPool::canonicalPath(file);
    auto& pending = pendingImportsBySource[sourceKey];
    const auto shouldStartDecode = pending.empty();
    pending.push_back({ placeholder });
    repaint();
    if (! shouldStartDecode)
        return;

    const auto safeOwner = juce::Component::SafePointer<ArrangeWindow>(this);
    loader.addJob(new AudioLoadJob(&waveformCache, file, trackModel->getSampleRate(),
                                   [safeOwner](const juce::File& sourceFile,
                                               std::shared_ptr<juce::AudioBuffer<float>> decodedBuffer)
    {
        if (safeOwner != nullptr)
            safeOwner->completeAudioImport(sourceFile, std::move(decodedBuffer));
    }), true);
}

void ArrangeWindow::importDroppedAudioFiles(const juce::Array<juce::File>& files, int trackIndex,
                                            double startSample, int insertionIndex)
{
    if (files.size() == 1)
    {
        importAudioFileAt(files.getFirst(), trackIndex, startSample, insertionIndex);
        return;
    }

    showMultipleAudioImportChoices(files, trackIndex, startSample, insertionIndex);
}

void ArrangeWindow::showMultipleAudioImportChoices(juce::Array<juce::File> files, int trackIndex,
                                                    double startSample, int insertionIndex)
{
    auto* dialog = new juce::AlertWindow("Import " + juce::String(files.size()) + " audio files",
                                         "Choose how StudioForge should place these files.",
                                         juce::MessageBoxIconType::QuestionIcon);
    dialog->addButton("One Audio Track per File", 1);
    dialog->addButton("Stack on Target Track", 2, juce::KeyPress());
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const auto safeOwner = juce::Component::SafePointer<ArrangeWindow>(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeOwner, files = std::move(files), trackIndex, startSample, insertionIndex] (int choice)
        {
            if (safeOwner == nullptr || choice == 0)
                return;
            if (choice == 1)
            {
                safeOwner->importEachFileOnNewTrack(files, startSample, insertionIndex);
                return;
            }
            auto destinationTrack = trackIndex;
            if (destinationTrack < 0)
                destinationTrack = safeOwner->resolveAudioImportTrack(-1, insertionIndex);
            if (destinationTrack < 0)
                return;
            for (const auto& file : files)
                safeOwner->importAudioFileAt(file, destinationTrack, startSample, -1);
        }), true);
}

void ArrangeWindow::importEachFileOnNewTrack(const juce::Array<juce::File>& files,
                                             double startSample, int insertionIndex)
{
    for (int index = 0; index < files.size(); ++index)
        importAudioFileAt(files[index], -1, startSample,
                          insertionIndex < 0 ? -1 : insertionIndex + index);
}

void ArrangeWindow::completeAudioImport(const juce::File& sourceFile,
                                        std::shared_ptr<juce::AudioBuffer<float>> decodedBuffer)
{
    const auto sourceKey = AudioMediaPool::canonicalPath(sourceFile);
    const auto pending = std::move(pendingImportsBySource[sourceKey]);
    pendingImportsBySource.erase(sourceKey);
    if (trackModel == nullptr)
    {
        repaint();
        return;
    }

    for (const auto& request : pending)
        if (decodedBuffer != nullptr)
            trackModel->setClipMediaResource(request.clipId, sourceFile, decodedBuffer);
        else
            trackModel->setClipMediaStatus(request.clipId, AudioMediaStatus::DecodeFailed);
    repaint();
}

void ArrangeWindow::requestWaveformPreparation()
{
    if (trackModel == nullptr)
        return;

    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto visibleStart = timelineGrid.getViewStartSample();
    const auto visibleDuration = static_cast<double>(timelineGrid.getWidth()) / pixelsPerSecond
        * trackModel->getSampleRate();
    const auto warmStart = juce::jmax(0.0, visibleStart - visibleDuration);
    const auto warmEnd = visibleStart + visibleDuration * 2.0;

    // Restrict background work to the visible timeline plus one viewport on
    // either side. Full-project thumbnail generation caused import work to
    // compete with scrolling in large sessions.
    for (size_t trackIndex = 0; trackIndex < trackModel->getTrackCount(); ++trackIndex)
        for (const auto& clip : trackModel->getTrack(trackIndex).clips)
        {
            const auto clipEnd = clip.startSample + clip.durationSamples;
            if (clipEnd < warmStart || clip.startSample > warmEnd)
                continue;
            queueWaveformPreparation(clip);
        }
}

void ArrangeWindow::queueWaveformPreparation(const AudioClipState& clip)
{
    if (clip.cachedBuffer == nullptr || waveformCache.find(clip.sourceFile) != nullptr)
        return;
    const auto key = AudioMediaPool::canonicalPath(clip.sourceFile);
    if (key.isEmpty() || ! pendingWaveformPreparations.insert(key).second)
        return;
    const auto generation = waveformViewportGeneration;
    const auto safeOwner = juce::Component::SafePointer<ArrangeWindow>(this);
    loader.addJob(new WaveformPreparationJob(safeOwner, &waveformCache, clip.sourceFile, clip.cachedBuffer,
        [safeOwner, key, generation]
        {
            if (safeOwner == nullptr)
                return;
            safeOwner->pendingWaveformPreparations.erase(key);
            if (generation == safeOwner->waveformViewportGeneration)
                safeOwner->repaint();
        }), true);
}

int ArrangeWindow::resolveAudioImportTrack(int requestedTrackIndex, int insertionIndex)
{
    if (trackModel == nullptr)
        return -1;

    if (requestedTrackIndex >= 0 && requestedTrackIndex < static_cast<int>(trackModel->getTrackCount())
        && trackModel->getTrack(static_cast<size_t>(requestedTrackIndex)).type == TrackType::audio)
        return requestedTrackIndex;

    const auto id = trackModel->addTrack(TrackType::audio);
    if (! id.isValid())
        return -1;

    const auto createdIndex = trackModel->getTrackIndex(id);
    if (insertionIndex >= 0 && insertionIndex < createdIndex)
        trackModel->reorderTrack(id, static_cast<size_t>(insertionIndex));
    return trackModel->getTrackIndex(id);
}
