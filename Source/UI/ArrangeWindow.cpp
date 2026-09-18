#include "ArrangeWindow.h"

#include "../Media/MediaReloadService.h"
#include "Theme/StudioForgeLookAndFeel.h"

#include <cmath>

namespace
{
const auto panelBackground = juce::Colour(0xff222222);
const auto panelAlt = juce::Colour(0xff303030);
const auto accentBlue = juce::Colour(0xff8098b5);
const auto accentCyan = juce::Colour(0xffb7cbe0);

void drawPremiumWaveform(juce::Graphics& g, const AudioClipState& clip,
                         const WaveformThumbnail* thumbnail, juce::Rectangle<float> clipBounds)
{
    juce::Graphics::ScopedSaveState savedState(g);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);

    const auto body = clipBounds.reduced(2.0f);
    g.setColour(juce::Colour(0xff3a3a3a));
    g.fillRoundedRectangle(body, 6.0f);
    g.setColour(juce::Colour(0xff4e4e4e));
    g.drawRoundedRectangle(body, 6.0f, 1.0f);

    const auto waveformBounds = body.reduced(4.0f, 2.0f);
    const auto centreY = waveformBounds.getCentreY();
    const auto halfHeight = waveformBounds.getHeight() * 0.46f;
    if (thumbnail != nullptr && ! thumbnail->isEmpty() && waveformBounds.getWidth() > 2.0f)
    {
        juce::Path waveform;
        const auto pixelCount = juce::jmax(1, juce::roundToInt(waveformBounds.getWidth()));
        const auto sourceStart = clip.sourceOffsetSamples;
        const auto sourceEnd = sourceStart + clip.durationSamples;
        waveform.startNewSubPath(waveformBounds.getX(), centreY);

        for (int pixel = 0; pixel <= pixelCount; ++pixel)
        {
            const auto firstSample = sourceStart + (sourceEnd - sourceStart) * pixel / pixelCount;
            const auto lastSample = sourceStart + (sourceEnd - sourceStart) * (pixel + 1) / pixelCount;
            const auto peakRange = thumbnail->peakForSourceRange(firstSample, lastSample);
            const auto peak = juce::jmax(std::abs(peakRange.minimum), std::abs(peakRange.maximum));

            const auto x = waveformBounds.getX()
                + waveformBounds.getWidth() * static_cast<float>(pixel) / pixelCount;
            waveform.lineTo(x, centreY - halfHeight * juce::jlimit(0.0f, 1.0f, peak));
        }

        for (int pixel = pixelCount; pixel >= 0; --pixel)
        {
            const auto firstSample = sourceStart + (sourceEnd - sourceStart) * pixel / pixelCount;
            const auto lastSample = sourceStart + (sourceEnd - sourceStart) * (pixel + 1) / pixelCount;
            const auto peakRange = thumbnail->peakForSourceRange(firstSample, lastSample);
            const auto peak = juce::jmax(std::abs(peakRange.minimum), std::abs(peakRange.maximum));

            const auto x = waveformBounds.getX()
                + waveformBounds.getWidth() * static_cast<float>(pixel) / pixelCount;
            waveform.lineTo(x, centreY + halfHeight * juce::jlimit(0.0f, 1.0f, peak));
        }
        waveform.closeSubPath();

        juce::ColourGradient gradient(juce::Colour(0xff00e5ff),
                                      0.0f, waveformBounds.getY(),
                                      juce::Colour(0xff00e5ff),
                                      0.0f, waveformBounds.getBottom(), false);
        gradient.addColour(0.5, juce::Colour(0x80005c8a));
        g.setGradientFill(gradient);
        g.fillPath(waveform);
        g.setColour(juce::Colour(0xff80f3ff));
        g.strokePath(waveform, juce::PathStrokeType(1.0f));
    }

    const float dashLengths[] { 4.0f, 3.0f };
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawDashedLine(juce::Line<float>(waveformBounds.getX(), centreY,
                                       waveformBounds.getRight(), centreY),
                     dashLengths, 2, 1.0f);

    if (thumbnail == nullptr && clip.mediaStatus != AudioMediaStatus::Ready)
    {
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.setFont(10.0f);
        g.drawText(clip.mediaStatus == AudioMediaStatus::DecodeFailed ? "Media unavailable" : "Preparing waveform…",
                   waveformBounds, juce::Justification::centred, false);
    }

    const auto label = clip.clipName.isEmpty() ? clip.sourceFile.getFileName() : clip.clipName;
    const auto labelBounds = juce::Rectangle<float>(body.getX() + 5.0f, body.getY() + 4.0f,
                                                    juce::jmin(body.getWidth() - 10.0f, 150.0f), 18.0f);
    g.setColour(juce::Colour(0xaa1a1a1a));
    g.fillRoundedRectangle(labelBounds, 4.0f);
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    g.drawText(label, labelBounds.reduced(5.0f, 0.0f), juce::Justification::centredLeft, true);
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

TrackHeaderPanel::TrackHeaderPanel(TrackDataModel* model)
    : trackModel(model)
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
        panControls[index].setBounds(getWidth() - 29, rowY + 8, 24, 24);
        volumeControls[index].setBounds(103, rowY + 24, juce::jmax(0, getWidth() - 137), 12);
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
        g.setColour(index == selectedTrack ? juce::Colour(0xff3a4147) : (index % 2 == 0 ? juce::Colour(0xff292c2f) : juce::Colour(0xff25282b)));
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
        juce::PopupMenu menu;
        menu.addItem(1, "New Audio Track Below");
        menu.addItem(2, "New Software Instrument Below");
        menu.addItem(3, "New External MIDI Track Below");
        menu.addItem(4, "New Aux Bus");
        menu.addSeparator();
        menu.addItem(5, "Duplicate Track");
        menu.addItem(6, "Delete Track", trackModel->getTrackCount() > 1);
        const auto safeOwner = juce::Component::SafePointer<TrackHeaderPanel>(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
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
    if (trackModel != nullptr && trackModel->isPlaying())
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

    const auto clampedZoom = juce::jlimit(0.5f, 5.0f, zoom);
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

void TimelineRuler::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff202020));
    if (trackModel == nullptr)
        return;
    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto beatWidth = trackModel->sampleToXPosition(trackModel->getSampleRate() * 60.0 / trackModel->getBpm(),
                                                          pixelsPerSecond);
    for (int beat = 0; beatWidth > 1.0 && beat * beatWidth < getWidth(); ++beat)
    {
        const auto x = static_cast<float>(beat * beatWidth
                                          - trackModel->sampleToXPosition(viewStartSample, pixelsPerSecond));
        g.setColour(beat % trackModel->getTimeSignatureNumerator() == 0
                        ? juce::Colours::white.withAlpha(0.7f)
                        : juce::Colours::white.withAlpha(0.25f));
        g.drawVerticalLine(juce::roundToInt(x), 0.0f, static_cast<float>(getHeight()));
        if (beat % trackModel->getTimeSignatureNumerator() == 0)
            g.drawText(juce::String(beat / trackModel->getTimeSignatureNumerator() + 1),
                       juce::roundToInt(x) + 4, 2, 44, 18, juce::Justification::left, false);
    }
    if (trackModel->isCycleActive())
    {
        const auto start = trackModel->sampleToXPosition(trackModel->getCycleStartSample() - viewStartSample, pixelsPerSecond);
        const auto end = trackModel->sampleToXPosition(trackModel->getCycleEndSample() - viewStartSample, pixelsPerSecond);
        g.setColour(juce::Colour(0x40ffff90));
        g.fillRect(start, 0.0f, end - start, static_cast<float>(getHeight()));
        g.setColour(juce::Colour(0xffffd166));
        g.drawRect(start, 0.0f, end - start, static_cast<float>(getHeight()), 1.0f);
    }
}

void TimelineRuler::mouseDown(const juce::MouseEvent& event)
{
    if (trackModel == nullptr)
        return;
    dragging = true;
    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    dragStart = trackModel->getSnappedSamplePosition(
        viewStartSample + static_cast<double>(event.position.x) / pixelsPerSecond * trackModel->getSampleRate(), 0.25);
    trackModel->setCycle(dragStart, dragStart + 1.0);
}

void TimelineRuler::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragging || trackModel == nullptr)
        return;
    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto current = trackModel->getSnappedSamplePosition(
        viewStartSample + static_cast<double>(event.position.x) / pixelsPerSecond * trackModel->getSampleRate(), 0.25);
    trackModel->setCycle(dragStart, current);
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
        if (trimmingLeft)
            trackModel->trimAudioClip(static_cast<size_t>(selectedTrack), static_cast<size_t>(selectedClip),
                                      trackModel->getSnappedSamplePosition(sample, 0.25),
                                      clip.startSample + clip.durationSamples - sample);
        else
            trackModel->trimAudioClip(static_cast<size_t>(selectedTrack), static_cast<size_t>(selectedClip),
                                      clip.startSample, trackModel->getSnappedSamplePosition(sample, 0.25) - clip.startSample);
        repaint();
        return;
    }

    if (selectedClipId.isValid())
    {
        const auto destinationTrack = juce::jlimit(0, static_cast<int>(trackModel->getTrackCount()) - 1,
                                                    static_cast<int>(event.position.y / trackModel->getTrackHeight()));
        const auto sampleAtMouse = viewStartSample + static_cast<double>(event.position.x) / pixelsPerSecond * trackModel->getSampleRate();
        clipDragPreviewSample = trackModel->getSnappedSamplePosition(sampleAtMouse, 0.25);
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
    const auto newZoom = juce::jlimit(0.5f, 5.0f, currentZoom * zoomMultiplier);
    const auto newPixelsPerSecond = 80.0 * newZoom;
    const auto newStart = pivotSample - static_cast<double>(event.position.x) / newPixelsPerSecond * trackModel->getSampleRate();
    updateView(newStart, newZoom);
}

bool TimelineGrid::keyPressed(const juce::KeyPress& key)
{
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
    g.fillAll(panelBackground);

    if (trackModel == nullptr)
        return;

    const auto width = static_cast<float>(getWidth());
    const auto height = static_cast<float>(getHeight());
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
            const auto rect = juce::Rectangle<float>(displayX, displayY + 8.0f, endX - startX, trackHeight - 16.0f);
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
        trackModel->getPlayheadPosition() - viewStartSample, pixelsPerSecond);
    if (playheadX >= 0.0 && playheadX <= width)
    {
        g.setColour(juce::Colour(0xffff5c5c));
        g.drawVerticalLine(juce::roundToInt(playheadX), 0.0f, height);
    }
}

ArrangeWindow::ArrangeWindow(TrackDataModel* model)
    : trackHeaderPanel(model),
      timelineRuler(model),
      timelineGrid(model)
{
    trackModel = model;
    timelineGrid.setWaveformCache(&waveformCache);
    addAndMakeVisible(trackHeaderPanel);
    addAndMakeVisible(timelineRuler);
    addAndMakeVisible(timelineGrid);
    horizontalZoomSlider.setRange(0.5, 5.0, 0.01);
    horizontalZoomSlider.setValue(model != nullptr ? model->getHorizontalZoom() : 1.0);
    horizontalZoomSlider.onValueChange = [this]
    {
        if (trackModel != nullptr)
        {
            setTimelineView(timelineGrid.getViewStartSample(), static_cast<float>(horizontalZoomSlider.getValue()));
        }
    };
    trackHeightSlider.setRange(36.0, 96.0, 1.0);
    trackHeightSlider.setValue(model != nullptr ? model->getTrackHeight() : 60);
    trackHeightSlider.onValueChange = [this]
    {
        if (trackModel != nullptr)
        {
            trackModel->setTrackHeight(juce::roundToInt(trackHeightSlider.getValue()));
            trackHeaderPanel.repaint();
            timelineGrid.repaint();
        }
    };
    addAndMakeVisible(horizontalZoomSlider);
    addAndMakeVisible(trackHeightSlider);

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

void ArrangeWindow::paint(juce::Graphics& g)
{
    g.fillAll(panelBackground);
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
    timelineRuler.setBounds(210, 0, juce::jmax(0, getWidth() - 210), 29);
    const auto leftWidth = 210;
    trackHeaderPanel.setBounds(area.removeFromLeft(leftWidth));
    timelineGrid.setBounds(area);
    horizontalZoomSlider.setBounds(getWidth() - 180, 5, 80, 16);
    trackHeightSlider.setBounds(getWidth() - 90, 5, 80, 16);
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
