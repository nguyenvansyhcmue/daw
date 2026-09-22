#include "ArrangeWindow.h"

#include "../Media/MediaReloadService.h"
#include "../AudioEngine/AudioEngine.h"
#include "PerformanceRoomIconLibrary.h"
#include "Theme/StudioForgeLookAndFeel.h"

#include <cmath>

namespace
{
const auto panelBackground = juce::Colour(0xff222222);
const auto panelAlt = juce::Colour(0xff303030);
const auto accentBlue = juce::Colour(0xff8098b5);
const auto accentCyan = juce::Colour(0xffb7cbe0);
constexpr double fineSnapInBeats = 1.0 / 16.0;
constexpr double tickSnapInBeats = 1.0 / 960.0;

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
                         const WaveformThumbnail* thumbnail, juce::Rectangle<float> clipBounds)
{
    juce::Graphics::ScopedSaveState savedState(g);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);

    const auto body = clipBounds.reduced(1.0f);
    g.setColour(juce::Colour(0xff174d5b));
    g.fillRect(body);
    g.setColour(juce::Colour(0xff42c9df).withAlpha(0.72f));
    g.drawRect(body, 1.0f);

    // The title is painted over the waveform; it never reserves a separate
    // title bar.  This leaves compact tracks with their full usable height.
    const auto waveformBounds = body.reduced(3.0f, 2.0f);
    const auto centreY = waveformBounds.getCentreY();
    const auto halfHeight = waveformBounds.getHeight() * 0.48f;
    if (thumbnail != nullptr && ! thumbnail->isEmpty() && waveformBounds.getWidth() > 2.0f)
    {
        const auto pixelCount = juce::jmax(1, juce::roundToInt(waveformBounds.getWidth()));
        const auto sourceStart = clip.sourceOffsetSamples;
        const auto sourceEnd = sourceStart + clip.durationSamples;
        // The reference is prepared with the thumbnail.  Painting therefore
        // performs one peak lookup per display column, even while zooming.
        const auto displayReference = juce::jmax(0.16f, thumbnail->getDisplayPeak());
        const auto columnWidth = waveformBounds.getWidth() / static_cast<float>(pixelCount) + 0.25f;
        g.setColour(juce::Colour(0xff69e8fa).withAlpha(0.88f));
        for (int pixel = 0; pixel < pixelCount; ++pixel)
        {
            const auto firstSample = sourceStart + (sourceEnd - sourceStart) * pixel / pixelCount;
            const auto lastSample = sourceStart + (sourceEnd - sourceStart) * (pixel + 1) / pixelCount;
            const auto peakRange = thumbnail->peakForSourceRange(firstSample, lastSample);
            const auto peak = juce::jmax(std::abs(peakRange.minimum), std::abs(peakRange.maximum));
            // Preserve the source envelope at high zoom.  A square-root curve
            // made modern, heavily compressed music look like a solid block.
            const auto visiblePeak = juce::jlimit(0.0f, 1.0f, peak / displayReference);
            const auto height = juce::jmax(0.75f, halfHeight * visiblePeak);
            const auto x = waveformBounds.getX() + waveformBounds.getWidth() * pixel / pixelCount;
            g.fillRect(x, centreY - height, columnWidth, height * 2.0f);
        }
    }

    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.drawHorizontalLine(juce::roundToInt(centreY), waveformBounds.getX(), waveformBounds.getRight());

    if (thumbnail == nullptr && clip.mediaStatus != AudioMediaStatus::Ready)
    {
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.setFont(10.0f);
        g.drawText(clip.mediaStatus == AudioMediaStatus::DecodeFailed ? "Media unavailable" : "Preparing waveform…",
                   waveformBounds, juce::Justification::centred, false);
    }

}

class AudioLoadJob final : public juce::ThreadPoolJob
{
public:
    AudioLoadJob(WaveformThumbnailCache* thumbnails, juce::File file,
                 std::function<void(const juce::File&, std::shared_ptr<juce::AudioBuffer<float>>)> completion)
        : juce::ThreadPoolJob("Audio file loader"),
          waveformCache(thumbnails),
          sourceFile(std::move(file)),
          onCompletion(std::move(completion))
    {
    }

    JobStatus runJob() override
    {
        std::shared_ptr<juce::AudioBuffer<float>> buffer;
        const auto decoded = MediaReloadService::decode(sourceFile, buffer).wasOk();
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
    std::function<void(const juce::File&, std::shared_ptr<juce::AudioBuffer<float>>)> onCompletion;
};

class WaveformPreparationJob final : public juce::ThreadPoolJob
{
public:
    WaveformPreparationJob(juce::Component::SafePointer<ArrangeWindow> owner,
                           WaveformThumbnailCache* thumbnails, juce::File file,
                           std::shared_ptr<juce::AudioBuffer<float>> buffer)
        : juce::ThreadPoolJob("Waveform thumbnail preparation"), safeOwner(owner),
          waveformCache(thumbnails), sourceFile(std::move(file)), decodedBuffer(std::move(buffer))
    {
    }

    JobStatus runJob() override
    {
        if (waveformCache != nullptr)
            waveformCache->prepare(sourceFile, decodedBuffer);
        juce::MessageManager::callAsync([owner = safeOwner]
        {
            if (owner != nullptr)
                owner->repaint();
        });
        return jobHasFinished;
    }

private:
    juce::Component::SafePointer<ArrangeWindow> safeOwner;
    WaveformThumbnailCache* waveformCache = nullptr;
    juce::File sourceFile;
    std::shared_ptr<juce::AudioBuffer<float>> decodedBuffer;
};
}

TrackHeaderPanel::TrackHeaderPanel(TrackDataModel* model, AudioEngine* engine)
    : trackModel(model), audioEngine(engine)
{
    for (size_t index = 0; index < volumeControls.size(); ++index)
    {
        auto& volume = volumeControls[index];
        volume.setRange(0.0, 2.0, 0.01);
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
        const auto rowY = static_cast<int>(index) * rowHeight;
        panControls[index].setBounds(getWidth() - 30, rowY + 7, 24, 24);
        volumeControls[index].setBounds(106, rowY + 31, juce::jmax(0, getWidth() - 142), 10);
    }
}

void TrackHeaderPanel::paint(juce::Graphics& g)
{
    g.fillAll(panelBackground);

    if (trackModel == nullptr)
        return;

    const auto rowHeight = static_cast<float>(trackModel->getTrackHeight());
    const auto count = static_cast<int>(trackModel->getTrackCount());
    for (int index = 0; index < count; ++index)
    {
        const auto row = juce::Rectangle<float>(0.0f, static_cast<float>(index) * rowHeight, static_cast<float>(getWidth()), rowHeight);
        g.setColour(index == selectedTrack ? StudioForgeTheme::raisedSurface.brighter(0.05f)
                                           : (index % 2 == 0 ? StudioForgeTheme::panelBackground : StudioForgeTheme::panelBackground.darker(0.045f)));
        g.fillRect(row);

        const auto& track = trackModel->getTrack(static_cast<size_t>(index));
        const auto colour = track.type == TrackType::instrument ? juce::Colour(0xff35a866)
            : track.type == TrackType::externalMidi ? juce::Colour(0xff9167d4)
            : juce::Colour(0xff3d8ed7);
        g.setColour(StudioForgeTheme::separator.withAlpha(0.75f));
        g.drawHorizontalLine(row.getBottom() - 1.0f, row.getX(), row.getRight());
        g.setColour(colour);
        g.fillRect(row.getX() + 3.0f, row.getY() + 2.0f, 3.0f, row.getHeight() - 4.0f);
        g.fillRoundedRectangle(row.getX() + 10.0f, row.getY() + 11.0f, 18.0f, 18.0f, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(track.type == TrackType::instrument ? "♪" : track.type == TrackType::externalMidi ? "M" : "~",
                   row.getX() + 10.0f, row.getY() + 11.0f, 18.0f, 18.0f, juce::Justification::centred, false);
        g.drawText(track.name.isNotEmpty() ? track.name : "Track " + juce::String(index + 1),
                   row.getX() + 35.0f, row.getY() + 4.0f, row.getWidth() - 70.0f, 16.0f, juce::Justification::centredLeft, true);
        g.setColour(colour);
        g.fillRoundedRectangle(row.getX() + 10.0f, row.getY() + 11.0f, 18.0f, 18.0f, 3.0f);
        PerformanceRoomIconLibrary::draw(g,
                                         { row.getX() + 12.0f, row.getY() + 13.0f, 14.0f, 14.0f },
                                         roleForTrackName(track.name, track.type));
        const auto arm = juce::Rectangle<float>(row.getX() + 35.0f, row.getY() + 23.0f, 14.0f, 14.0f);
        const auto monitor = arm.translated(16.0f, 0.0f);
        const auto mute = track.type == TrackType::audio ? monitor.translated(16.0f, 0.0f) : arm.translated(16.0f, 0.0f);
        const auto solo = mute.translated(16.0f, 0.0f);
        g.setColour(track.armed.load(std::memory_order_relaxed) ? StudioForgeTheme::recordRed : juce::Colour(0xff333333));
        g.fillRoundedRectangle(arm, 2.0f);
        if (track.type == TrackType::audio)
        {
            g.setColour(track.inputMonitoring.load(std::memory_order_relaxed) ? StudioForgeTheme::accentBlue : juce::Colour(0xff333333));
            g.fillRoundedRectangle(monitor, 2.0f);
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

        const auto meter = juce::Rectangle<float>(row.getX() + 107.0f, row.getY() + 21.0f,
                                                  juce::jmax(0.0f, row.getWidth() - 144.0f), 4.0f);
        g.setColour(StudioForgeTheme::recessedSurface);
        g.fillRoundedRectangle(meter, 1.5f);
        if (audioEngine != nullptr)
        {
            const auto peak = juce::jlimit(0.0f, 1.0f, audioEngine->getTrackPeak(static_cast<size_t>(index)));
            g.setColour(StudioForgeTheme::meterGreen);
            g.fillRoundedRectangle(meter.withWidth(meter.getWidth() * peak), 1.5f);
        }
    }
}

TimelineGrid::TimelineGrid(TrackDataModel* model)
    : trackModel(model)
{
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

void TrackHeaderPanel::mouseDown(const juce::MouseEvent& event)
{
    if (trackModel == nullptr)
        return;

    const auto rowHeight = juce::jmax(1, trackModel->getTrackHeight());
    const auto track = static_cast<int>(event.position.y) / rowHeight;
    if (track < 0 || track >= static_cast<int>(trackModel->getTrackCount()))
        return;

    const auto rowY = static_cast<float>(track * rowHeight);
    const auto& state = trackModel->getTrack(static_cast<size_t>(track));
    const auto arm = juce::Rectangle<float>(35.0f, rowY + 23.0f, 14.0f, 14.0f);
    const auto monitor = arm.translated(16.0f, 0.0f);
    const auto mute = state.type == TrackType::audio ? monitor.translated(16.0f, 0.0f) : arm.translated(16.0f, 0.0f);
    const auto solo = mute.translated(16.0f, 0.0f);

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
        trackModel->setTrackInputMonitoring(static_cast<size_t>(track), ! state.inputMonitoring.load(std::memory_order_relaxed));
    else if (mute.contains(event.position))
        trackModel->setTrackMuted(static_cast<size_t>(track), ! state.muted.load(std::memory_order_relaxed));
    else if (solo.contains(event.position))
        trackModel->setTrackSolo(static_cast<size_t>(track), ! state.solo.load(std::memory_order_relaxed));

    selectedTrack = track;
    repaint();
    if (onTrackSelected != nullptr)
        onTrackSelected(track);
}

void TrackHeaderPanel::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (trackModel == nullptr || trackModel->getTrackCount() >= TrackDataModel::maxTracks)
        return;

    const auto anchor = juce::jlimit(0, static_cast<int>(trackModel->getTrackCount()) - 1,
                                     static_cast<int>(event.position.y) / juce::jmax(1, trackModel->getTrackHeight()));
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
    if (trackModel == nullptr || ! trackModel->isPlaying())
        return;

    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto playheadX = trackModel->sampleToXPosition(displayPlayheadSample - viewStartSample, pixelsPerSecond);
    const auto followEdge = static_cast<double>(getWidth()) * 0.82;
    if (playheadX < 0.0 || playheadX > followEdge)
    {
        const auto lookAheadSeconds = static_cast<double>(getWidth()) * 0.20 / pixelsPerSecond;
        updateView(trackModel->getPlayheadPosition() - lookAheadSeconds * trackModel->getSampleRate(),
                   trackModel->getHorizontalZoom());
        return;
    }

    repaint();
}

void TimelineGrid::setViewStartSample(double sample) noexcept
{
    viewStartSample = juce::jmax(0.0, sample);
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
    return juce::jlimit(0, static_cast<int>(trackModel->getTrackCount()) - 1,
                        static_cast<int>(position.y / trackModel->getTrackHeight()));
}

void TimelineGrid::updateView(double startSample, float zoom)
{
    if (trackModel == nullptr)
        return;

    const auto clampedZoom = juce::jlimit(0.5f, 4096.0f, zoom);
    viewStartSample = juce::jmax(0.0, startSample);
    trackModel->setHorizontalZoom(clampedZoom);
    if (onViewChanged != nullptr)
        onViewChanged(viewStartSample, clampedZoom);
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

    constexpr int locatorLaneHeight = 12;
    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto timelineWidth = juce::jmax(0, getWidth() - reservedTrailingWidth);
    const auto beatWidth = trackModel->sampleToXPosition(trackModel->getSampleRate() * 60.0 / trackModel->getBpm(),
                                                          pixelsPerSecond);

    g.setColour(juce::Colour(0xff161b1f));
    g.fillRect(bounds.removeFromTop(locatorLaneHeight));
    g.setColour(juce::Colours::white.withAlpha(0.13f));
    g.drawHorizontalLine(locatorLaneHeight - 1, 0.0f, static_cast<float>(getWidth()));

    for (int beat = 0; beatWidth > 1.0 && beat * beatWidth < timelineWidth + beatWidth; ++beat)
    {
        const auto x = static_cast<float>(beat * beatWidth
                                          - trackModel->sampleToXPosition(viewStartSample, pixelsPerSecond));
        g.setColour(beat % trackModel->getTimeSignatureNumerator() == 0
                        ? juce::Colours::white.withAlpha(0.7f)
                        : juce::Colours::white.withAlpha(0.25f));
        g.drawVerticalLine(juce::roundToInt(x), static_cast<float>(locatorLaneHeight), static_cast<float>(getHeight()));
        if (beat % trackModel->getTimeSignatureNumerator() == 0)
            g.drawText(juce::String(beat / trackModel->getTimeSignatureNumerator() + 1),
                       juce::roundToInt(x) + 4, locatorLaneHeight + 1, 44, 17, juce::Justification::left, false);
    }

    if (trackModel->isCycleActive())
    {
        const auto start = static_cast<float>(trackModel->sampleToXPosition(trackModel->getCycleStartSample() - viewStartSample, pixelsPerSecond));
        const auto end = static_cast<float>(trackModel->sampleToXPosition(trackModel->getCycleEndSample() - viewStartSample, pixelsPerSecond));
        const auto cycleBounds = juce::Rectangle<float>(start, 1.0f, end - start, static_cast<float>(locatorLaneHeight - 2));
        g.setColour(juce::Colour(0xffd2a843).withAlpha(0.78f));
        g.fillRect(cycleBounds);
        g.setColour(juce::Colour(0xffffd166));
        g.drawHorizontalLine(locatorLaneHeight - 1, start, end);
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

    constexpr float locatorLaneHeight = 12.0f;
    const auto clickedSample = snappedSampleAt(event.position);
    if (event.position.y >= locatorLaneHeight)
    {
        dragMode = DragMode::scrubPlayhead;
        trackModel->setPlayheadPosition(clickedSample);
        repaint();
        return;
    }

    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto cycleStartX = trackModel->sampleToXPosition(trackModel->getCycleStartSample() - viewStartSample, pixelsPerSecond);
    const auto cycleEndX = trackModel->sampleToXPosition(trackModel->getCycleEndSample() - viewStartSample, pixelsPerSecond);
    const auto clearsExistingRange = trackModel->isCycleActive()
        && event.position.x >= cycleStartX && event.position.x <= cycleEndX;
    if (clearsExistingRange)
    {
        // A second click on the highlighted range is the deliberate, simple
        // cancel gesture.  Dragging immediately afterwards starts a new range.
        trackModel->clearCycle();
    }

    const auto samplesPerBar = trackModel->getSampleRate() * 60.0 / trackModel->getBpm()
        * trackModel->getTimeSignatureNumerator();
    dragAnchorSample = std::floor(clickedSample / samplesPerBar) * samplesPerBar;
    dragMode = DragMode::selectCycle;
    if (! clearsExistingRange)
        trackModel->setCycle(dragAnchorSample, dragAnchorSample + samplesPerBar);
    repaint();
}

void TimelineRuler::mouseDrag(const juce::MouseEvent& event)
{
    if (trackModel == nullptr || dragMode == DragMode::none)
        return;

    const auto current = snappedSampleAt(event.position);
    if (dragMode == DragMode::scrubPlayhead)
        trackModel->setPlayheadPosition(current);
    else if (dragMode == DragMode::selectCycle)
        trackModel->setCycle(dragAnchorSample, current);
    repaint();
}

void TimelineRuler::mouseUp(const juce::MouseEvent&)
{
    dragMode = DragMode::none;
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
    const auto& audioClips = trackModel->getTrack(static_cast<size_t>(track)).clips;
    for (size_t index = 0; index < audioClips.size(); ++index)
    {
        const auto& clip = audioClips[index];
        const auto startX = trackModel->sampleToXPosition(clip.startSample - viewStartSample, pixelsPerSecond);
        const auto endX = trackModel->sampleToXPosition(clip.startSample + clip.durationSamples - viewStartSample, pixelsPerSecond);
        if (event.position.x >= startX && event.position.x <= endX)
        {
            if (event.mods.isPopupMenu())
            {
                showContextMenu(event, track, clip.id, sampleAtMouse);
                return;
            }
            selectedTrack = track;
            selectedClip = static_cast<int>(index);
            selectedClipId = clip.id;
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
            const auto isHandleRow = event.position.y <= static_cast<float>(track) * trackModel->getTrackHeight() + 20.0f;
            adjustingFadeIn = isHandleRow && event.position.x <= startX + 12.0f;
            adjustingFadeOut = isHandleRow && event.position.x >= endX - 12.0f;
            trimmingLeft = ! adjustingFadeIn && std::abs(event.position.x - startX) <= 5.0;
            trimmingRight = ! adjustingFadeOut && std::abs(event.position.x - endX) <= 5.0;
            clipDragStartSample = clip.startSample;
            clipDragGrabOffsetSamples = juce::jmax(0.0, sampleAtMouse - clip.startSample);
            clipDragPreviewSample = clip.startSample;
            clipDragPreviewTrack = track;
            setMouseCursor(trimmingLeft || trimmingRight || adjustingFadeIn || adjustingFadeOut
                               ? juce::MouseCursor::LeftRightResizeCursor
                               : juce::MouseCursor::NormalCursor);
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
        double duration = trackModel->getSampleRate() * 0.5;
        for (const auto& note : midiClip.notes) duration = juce::jmax(duration, note.startSample + note.durationSamples);
        const auto startX = trackModel->sampleToXPosition(midiClip.startSample - viewStartSample, pixelsPerSecond);
        const auto endX = trackModel->sampleToXPosition(midiClip.startSample + duration - viewStartSample, pixelsPerSecond);
        if (event.position.x >= startX && event.position.x <= endX)
        {
            selectedTrack = track; selectedClip = -1; selectedClipId = {};
            if (onTrackSelected != nullptr) onTrackSelected(track);
            if (onMidiClipSelected != nullptr) onMidiClipSelected(midiClip.id);
            repaint();
            return;
        }
    }
    selectedTrack = track;
    selectedClip = -1;
    selectedClipId = {};
    if (onTrackSelected != nullptr) onTrackSelected(track);
    repaint();
}

void TimelineGrid::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (trackModel == nullptr || trackModel->getTrackCount() >= TrackDataModel::maxTracks)
        return;

    const auto trackHeight = trackModel->getTrackHeight();
    const auto clickedTrack = static_cast<int>(event.position.y / trackHeight);
    if (clickedTrack >= 0 && clickedTrack < static_cast<int>(trackModel->getTrackCount()))
        return; // Existing rows are reserved for region editing, not track creation.

    const auto anchor = selectedTrack >= 0 ? selectedTrack : static_cast<int>(trackModel->getTrackCount()) - 1;
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
    if (panningTimeline)
    {
        const auto deltaSamples = static_cast<double>(panDragStartX - event.position.x) / pixelsPerSecond * trackModel->getSampleRate();
        updateView(panDragStartSample + deltaSamples, trackModel->getHorizontalZoom());
        return;
    }
    if (selectedTrack >= 0 && selectedClip >= 0 && (adjustingFadeIn || adjustingFadeOut))
    {
        const auto& clip = trackModel->getTrack(static_cast<size_t>(selectedTrack)).clips[static_cast<size_t>(selectedClip)];
        const auto sample = viewStartSample
            + static_cast<double>(event.position.x) / pixelsPerSecond * trackModel->getSampleRate();
        const auto fadeLength = adjustingFadeIn ? sample - clip.startSample
                                                : clip.startSample + clip.durationSamples - sample;
        trackModel->setClipFades(selectedClipId,
                                 adjustingFadeIn ? fadeLength : clip.fadeInSamples,
                                 adjustingFadeOut ? fadeLength : clip.fadeOutSamples);
        repaint();
        return;
    }
    if (selectedTrack >= 0 && selectedClip >= 0 && (trimmingLeft || trimmingRight))
    {
        const auto& clip = trackModel->getTrack(static_cast<size_t>(selectedTrack)).clips[static_cast<size_t>(selectedClip)];
        const auto sample = viewStartSample
            + static_cast<double>(event.position.x) / pixelsPerSecond * trackModel->getSampleRate();
        const auto snappedSample = snappedEditSample(*trackModel, sample, event.mods);
        if (trimmingLeft)
            trackModel->trimAudioClip(static_cast<size_t>(selectedTrack), static_cast<size_t>(selectedClip),
                                      snappedSample,
                                      clip.startSample + clip.durationSamples - snappedSample);
        else
            trackModel->trimAudioClip(static_cast<size_t>(selectedTrack), static_cast<size_t>(selectedClip),
                                      clip.startSample, snappedSample - clip.startSample);
        repaint();
        return;
    }

    if (selectedClipId.isValid())
    {
        const auto destinationTrack = juce::jlimit(0, static_cast<int>(trackModel->getTrackCount()) - 1,
                                                    static_cast<int>(event.position.y / trackModel->getTrackHeight()));
        const auto sampleAtMouse = viewStartSample + static_cast<double>(event.position.x) / pixelsPerSecond * trackModel->getSampleRate();
        clipDragPreviewSample = snappedEditSample(*trackModel, sampleAtMouse - clipDragGrabOffsetSamples, event.mods);
        clipDragPreviewTrack = destinationTrack;
        draggingClip = true;
        repaint();
    }
}

void TimelineGrid::mouseUp(const juce::MouseEvent&)
{
    if (draggingClip && trackModel != nullptr && selectedClipId.isValid() && clipDragPreviewTrack >= 0)
    {
        trackModel->moveAudioClip(selectedClipId, trackModel->getTrackId(static_cast<size_t>(clipDragPreviewTrack)), clipDragPreviewSample);
        selectedTrack = clipDragPreviewTrack;
        selectedClip = -1;
        if (onTrackSelected != nullptr) onTrackSelected(selectedTrack);
    }
    draggingClip = false;
    panningTimeline = false;
    trimmingLeft = false;
    trimmingRight = false;
    adjustingFadeIn = false;
    adjustingFadeOut = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void TimelineGrid::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (trackModel == nullptr)
        return;

    const auto currentZoom = trackModel->getHorizontalZoom();
    if (event.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
    {
        const auto pixelsPerSecond = 80.0 * currentZoom;
        const auto delta = static_cast<double>(wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY)
            * trackModel->getSampleRate() * 2.5 / pixelsPerSecond;
        updateView(viewStartSample - delta, currentZoom);
        return;
    }

    const auto pivotSample = sampleAt(event.position);
    const auto zoomMultiplier = wheel.deltaY > 0.0f ? 1.12f : 1.0f / 1.12f;
    const auto newZoom = juce::jlimit(0.5f, 4096.0f, currentZoom * zoomMultiplier);
    const auto newPixelsPerSecond = 80.0 * newZoom;
    const auto newStart = pivotSample - static_cast<double>(event.position.x) / newPixelsPerSecond * trackModel->getSampleRate();
    updateView(newStart, newZoom);
}

bool TimelineGrid::keyPressed(const juce::KeyPress& key)
{
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

    if (key == juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)
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
        && selectedTrack >= 0 && selectedClip >= 0 && trackModel != nullptr)
    {
        trackModel->deleteAudioClip(static_cast<size_t>(selectedTrack), static_cast<size_t>(selectedClip));
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
    const auto samplesPerBeat = trackModel->getSampleRate() * 60.0 / trackModel->getBpm();
    const auto pixelsPerBeat = trackModel->sampleToXPosition(samplesPerBeat, pixelsPerSecond);
    for (auto x = 0.0; x < width; x += pixelsPerBeat)
    {
        const auto absoluteSample = viewStartSample
            + (x / pixelsPerSecond) * trackModel->getSampleRate();
        const auto beatIndex = std::floor(absoluteSample / samplesPerBeat);
        const auto isBar = std::fmod(beatIndex, static_cast<double>(trackModel->getTimeSignatureNumerator())) < 0.001;
        g.setColour(isBar ? accentBlue.withAlpha(0.45f) : juce::Colours::white.withAlpha(0.10f));
        g.drawVerticalLine(juce::roundToInt(x), 0.0f, height);
    }

    const auto trackCount = static_cast<int>(trackModel->getTrackCount());
    for (int track = 0; track < trackCount; ++track)
    {
        const auto rowY = static_cast<float>(track) * trackHeight;
        g.setColour(track == selectedTrack ? juce::Colour(0xff52697d).withAlpha(0.20f) : juce::Colours::white.withAlpha(0.04f));
        g.fillRect(0.0f, rowY, width, trackHeight);

        const auto& state = trackModel->getTrack(static_cast<size_t>(track));
        for (const auto& clip : state.clips)
        {
            const auto startX = trackModel->sampleToXPosition(clip.startSample - viewStartSample, pixelsPerSecond);
            const auto endX = trackModel->sampleToXPosition(clip.startSample + clip.durationSamples - viewStartSample, pixelsPerSecond);
            const auto displayTrack = draggingClip && clip.id == selectedClipId ? clipDragPreviewTrack : track;
            const auto displayStart = draggingClip && clip.id == selectedClipId ? clipDragPreviewSample : clip.startSample;
            const auto displayX = trackModel->sampleToXPosition(displayStart - viewStartSample, pixelsPerSecond);
            const auto displayY = static_cast<float>(displayTrack) * trackHeight;
            const auto rect = juce::Rectangle<float>(displayX, displayY + 3.0f, endX - startX, trackHeight - 6.0f);
            if (rect.getWidth() <= 0.0f)
                continue;

            const auto thumbnail = waveformCache != nullptr ? waveformCache->find(clip.sourceFile) : nullptr;
            drawPremiumWaveform(g, clip, thumbnail.get(), rect);
            const auto fadeInX = rect.getX() + static_cast<float>(trackModel->sampleToXPosition(clip.fadeInSamples, pixelsPerSecond));
            const auto fadeOutX = rect.getRight() - static_cast<float>(trackModel->sampleToXPosition(clip.fadeOutSamples, pixelsPerSecond));
            g.setColour(juce::Colours::white.withAlpha(0.62f));
            g.drawLine(rect.getX(), rect.getBottom() - 3.0f, fadeInX, rect.getY() + 3.0f, 1.5f);
            g.drawLine(fadeOutX, rect.getY() + 3.0f, rect.getRight(), rect.getBottom() - 3.0f, 1.5f);
            g.fillEllipse(rect.getX() - 2.0f, rect.getY() + 2.0f, 5.0f, 5.0f);
            g.fillEllipse(rect.getRight() - 3.0f, rect.getY() + 2.0f, 5.0f, 5.0f);
            if (track == selectedTrack && static_cast<int>(&clip - state.clips.data()) == selectedClip)
            {
                g.setColour(juce::Colour(0xffc9e5ff));
                g.drawRoundedRectangle(rect.reduced(1.0f), 5.0f, 2.0f);
            }
        }

        for (const auto& midiClip : trackModel->getMidiClips())
        {
            if (midiClip.trackId != state.id) continue;
            double duration = trackModel->getSampleRate() * 0.5;
            for (const auto& note : midiClip.notes)
                duration = juce::jmax(duration, note.startSample + note.durationSamples);
            const auto startX = trackModel->sampleToXPosition(midiClip.startSample - viewStartSample, pixelsPerSecond);
            const auto endX = trackModel->sampleToXPosition(midiClip.startSample + duration - viewStartSample, pixelsPerSecond);
            const auto midiRect = juce::Rectangle<float>(startX, rowY + 7.0f, endX - startX, trackHeight - 14.0f);
            if (midiRect.getWidth() <= 0.0f) continue;
            g.setColour(juce::Colour(0xff6a67bf));
            g.fillRoundedRectangle(midiRect, 5.0f);
            g.setColour(juce::Colour(0xffbdb9ff));
            g.drawRoundedRectangle(midiRect, 5.0f, 1.0f);
            for (const auto& note : midiClip.notes)
            {
                const auto noteX = startX + trackModel->sampleToXPosition(note.startSample, pixelsPerSecond);
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
    trackHeightSlider.setRange(36.0, 96.0, 1.0);
    trackHeightSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    trackHeightSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    trackHeightSlider.setValue(model != nullptr ? model->getTrackHeight() : 64);
    trackHeightSlider.onValueChange = [this]
    {
        if (trackModel != nullptr)
        {
            const auto height = juce::roundToInt(trackHeightSlider.getValue());
            trackHeightReadout.setText("H " + juce::String(height), juce::dontSendNotification);
            trackModel->setTrackHeight(height);
            trackHeaderPanel.repaint();
            timelineGrid.repaint();
        }
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
    zoomReadout.setText(zoomReadoutText(horizontalZoomSlider.getValue()), juce::dontSendNotification);
    trackHeightReadout.setText("H " + juce::String(juce::roundToInt(trackHeightSlider.getValue())), juce::dontSendNotification);
    addAndMakeVisible(horizontalZoomSlider);
    addAndMakeVisible(trackHeightSlider);
    addAndMakeVisible(zoomReadout);
    addAndMakeVisible(trackHeightReadout);
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
    };
    timelineGrid.onMidiClipSelected = [this](MidiClipId clip)
    {
        if (onMidiClipSelected != nullptr) onMidiClipSelected(clip);
    };
    timelineGrid.onAudioClipSelected = [this](ClipId clip)
    {
        if (onAudioClipSelected != nullptr) onAudioClipSelected(clip);
    };
}

void ArrangeWindow::timerCallback()
{
    if (trackModel == nullptr)
        return;

    const auto currentSample = trackModel->getPlayheadPosition();
    if (std::abs(currentSample - lastDisplayPlayheadSample) < 0.01)
        return;

    lastDisplayPlayheadSample = currentSample;
    // Both child components receive one UI-thread snapshot.  Their playhead
    // x-coordinate therefore remains identical even at extreme zoom levels.
    timelineRuler.setDisplayPlayheadSample(currentSample);
    timelineGrid.setDisplayPlayheadSample(currentSample);
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
        const auto row = juce::Rectangle<float>(0.0f, 29.0f + draggedTrack * trackHeight,
                                                static_cast<float>(getWidth()), trackHeight);
        g.setColour(accentCyan.withAlpha(0.16f));
        g.fillRect(row);
        g.setColour(accentCyan);
        g.drawRect(row, 2.0f);
    }
}

void ArrangeWindow::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(29);
    constexpr int rulerControlStripWidth = 236;
    const auto leftWidth = juce::jlimit(190, 270, juce::roundToInt(static_cast<float>(getWidth()) * 0.19f));
    timelineRuler.setBounds(leftWidth, 0, juce::jmax(0, getWidth() - leftWidth), 29);
    timelineRuler.setReservedTrailingWidth(rulerControlStripWidth);
    trackHeaderPanel.setBounds(area.removeFromLeft(leftWidth));
    timelineGrid.setBounds(area);
    const auto controlLeft = getWidth() - rulerControlStripWidth;
    zoomReadout.setBounds(controlLeft + 8, 5, 48, 16);
    horizontalZoomSlider.setBounds(controlLeft + 60, 5, 50, 16);
    trackHeightReadout.setBounds(controlLeft + 120, 5, 36, 16);
    trackHeightSlider.setBounds(controlLeft + 160, 5, 66, 16);
}

bool ArrangeWindow::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const auto extension = juce::File(path).getFileExtension().toLowerCase();
        if (extension == ".wav" || extension == ".mp3" || extension == ".aiff"
            || extension == ".aif")
            return true;
    }
    return false;
}

void ArrangeWindow::fileDragEnter(const juce::StringArray&, int, int)
{
    isDraggingOver = true;
    repaint();
}

void ArrangeWindow::fileDragMove(const juce::StringArray&, int, int y)
{
    if (trackModel == nullptr)
        return;

    const auto count = static_cast<int>(trackModel->getTrackCount());
    draggedTrack = juce::jlimit(0, count,
                                 (y - 29) / juce::jmax(1, trackModel->getTrackHeight()));
    isDraggingOver = true;
    repaint();
}

void ArrangeWindow::fileDragExit(const juce::StringArray&)
{
    isDraggingOver = false;
    repaint();
}

void ArrangeWindow::filesDropped(const juce::StringArray& files, int x, int y)
{
    isDraggingOver = false;
    repaint();
    if (trackModel == nullptr || files.isEmpty() || ! isInterestedInFileDrag(files))
        return;

    const auto trackCount = static_cast<int>(trackModel->getTrackCount());
    const auto candidateTrack = (y - 29) / juce::jmax(1, trackModel->getTrackHeight());
    const auto track = candidateTrack >= 0 && candidateTrack < trackCount
        && trackModel->getTrack(static_cast<size_t>(candidateTrack)).type == TrackType::audio
            ? candidateTrack : -1;
    const auto timelineX = juce::jmax(0, x - 210);
    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto rawSample = (static_cast<double>(timelineX) / pixelsPerSecond)
        * trackModel->getSampleRate();
    const auto snappedSample = trackModel->getSnappedSamplePosition(rawSample, 0.25);
    importAudioFile(juce::File(files[0]), track, snappedSample);
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

void ArrangeWindow::importAudioFile(const juce::File& file, int trackIndex, double startSample)
{
    if (trackModel == nullptr || ! file.existsAsFile())
        return;

    const auto resolvedTrack = resolveAudioImportTrack(trackIndex);
    if (resolvedTrack < 0)
        return;
    const auto trackId = trackModel->getTrackId(static_cast<size_t>(resolvedTrack));
    if (! trackId.isValid())
        return;
    if (onTrackSelected != nullptr)
        onTrackSelected(resolvedTrack);

    const auto snappedStart = trackModel->getSnappedSamplePosition(startSample, 0.25);
    if (const auto cachedSource = trackModel->findDecodedAudioSource(file); cachedSource != nullptr)
    {
        trackModel->addClipToTrack(resolvedTrack, file, snappedStart, cachedSource);
        repaint();
        return;
    }

    const auto sourceKey = AudioMediaPool::canonicalPath(file);
    auto& pending = pendingImportsBySource[sourceKey];
    const auto shouldStartDecode = pending.empty();
    pending.push_back({ trackId, snappedStart });
    if (! shouldStartDecode)
        return;

    const auto safeOwner = juce::Component::SafePointer<ArrangeWindow>(this);
    loader.addJob(new AudioLoadJob(&waveformCache, file,
                                   [safeOwner](const juce::File& sourceFile,
                                               std::shared_ptr<juce::AudioBuffer<float>> decodedBuffer)
    {
        if (safeOwner != nullptr)
            safeOwner->completeAudioImport(sourceFile, std::move(decodedBuffer));
    }), true);
}

void ArrangeWindow::completeAudioImport(const juce::File& sourceFile,
                                        std::shared_ptr<juce::AudioBuffer<float>> decodedBuffer)
{
    const auto sourceKey = AudioMediaPool::canonicalPath(sourceFile);
    const auto pending = std::move(pendingImportsBySource[sourceKey]);
    pendingImportsBySource.erase(sourceKey);
    if (trackModel == nullptr || decodedBuffer == nullptr)
    {
        repaint();
        return;
    }

    for (const auto& request : pending)
        if (const auto trackIndex = trackModel->getTrackIndex(request.trackId); trackIndex >= 0)
            trackModel->addClipToTrack(trackIndex, sourceFile, request.startSample, decodedBuffer);
    repaint();
}

void ArrangeWindow::requestWaveformPreparation()
{
    if (trackModel == nullptr)
        return;

    for (size_t trackIndex = 0; trackIndex < trackModel->getTrackCount(); ++trackIndex)
        for (const auto& clip : trackModel->getTrack(trackIndex).clips)
            if (clip.cachedBuffer != nullptr && waveformCache.find(clip.sourceFile) == nullptr)
                loader.addJob(new WaveformPreparationJob(juce::Component::SafePointer<ArrangeWindow>(this),
                                                         &waveformCache, clip.sourceFile, clip.cachedBuffer), true);
}

int ArrangeWindow::resolveAudioImportTrack(int requestedTrackIndex)
{
    if (trackModel == nullptr)
        return -1;

    if (requestedTrackIndex >= 0 && requestedTrackIndex < static_cast<int>(trackModel->getTrackCount())
        && trackModel->getTrack(static_cast<size_t>(requestedTrackIndex)).type == TrackType::audio)
        return requestedTrackIndex;

    const auto id = trackModel->addTrack(TrackType::audio);
    return id.isValid() ? trackModel->getTrackIndex(id) : -1;
}
