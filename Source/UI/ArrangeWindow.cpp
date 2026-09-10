#include "ArrangeWindow.h"

#include <cmath>
#include <limits>

namespace
{
const auto panelBackground = juce::Colour(0xff1a1a1a);
const auto panelAlt = juce::Colour(0xff2b2b2b);
const auto accentBlue = juce::Colour(0xff00a8ff);
const auto accentCyan = juce::Colour(0xff00ffe0);

void drawPremiumWaveform(juce::Graphics& g, const AudioClipState& clip,
                         juce::Rectangle<float> clipBounds)
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
    const auto sampleCount = clip.cachedBuffer != nullptr ? clip.cachedBuffer->getNumSamples() : 0;
    const auto channelCount = clip.cachedBuffer != nullptr ? clip.cachedBuffer->getNumChannels() : 0;

    if (sampleCount > 0 && channelCount > 0 && waveformBounds.getWidth() > 2.0f)
    {
        juce::Path waveform;
        const auto pixelCount = juce::jmax(1, juce::roundToInt(waveformBounds.getWidth()));
        waveform.startNewSubPath(waveformBounds.getX(), centreY);

        for (int pixel = 0; pixel <= pixelCount; ++pixel)
        {
            const auto firstSample = juce::jmin(sampleCount - 1, static_cast<int>(
                (static_cast<double>(pixel) / pixelCount) * sampleCount));
            const auto lastSample = juce::jmax(firstSample + 1,
                                               static_cast<int>(
                                                   (static_cast<double>(pixel + 1) / pixelCount)
                                                   * sampleCount));
            const auto count = juce::jmin(sampleCount, lastSample) - firstSample;
            auto peak = 0.0f;
            for (int channel = 0; channel < channelCount; ++channel)
            {
                const auto range = juce::FloatVectorOperations::findMinAndMax(
                    clip.cachedBuffer->getReadPointer(channel, firstSample), count);
                peak = juce::jmax(peak, juce::jmax(std::abs(range.getStart()),
                                                   std::abs(range.getEnd())));
            }

            const auto x = waveformBounds.getX()
                + waveformBounds.getWidth() * static_cast<float>(pixel) / pixelCount;
            waveform.lineTo(x, centreY - halfHeight * juce::jlimit(0.0f, 1.0f, peak));
        }

        for (int pixel = pixelCount; pixel >= 0; --pixel)
        {
            const auto firstSample = juce::jmin(sampleCount - 1, static_cast<int>(
                (static_cast<double>(pixel) / pixelCount) * sampleCount));
            const auto lastSample = juce::jmax(firstSample + 1,
                                               static_cast<int>(
                                                   (static_cast<double>(pixel + 1) / pixelCount)
                                                   * sampleCount));
            const auto count = juce::jmin(sampleCount, lastSample) - firstSample;
            auto peak = 0.0f;
            for (int channel = 0; channel < channelCount; ++channel)
            {
                const auto range = juce::FloatVectorOperations::findMinAndMax(
                    clip.cachedBuffer->getReadPointer(channel, firstSample), count);
                peak = juce::jmax(peak, juce::jmax(std::abs(range.getStart()),
                                                   std::abs(range.getEnd())));
            }

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
    AudioLoadJob(juce::Component::SafePointer<ArrangeWindow> owner,
                 TrackDataModel* model, juce::File file, int trackID, double startSample)
        : juce::ThreadPoolJob("Audio file loader"),
          safeOwner(owner),
          trackModel(model),
          sourceFile(std::move(file)),
          targetTrack(trackID),
          targetSample(startSample)
    {
    }

    JobStatus runJob() override
    {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(sourceFile));
        if (reader == nullptr || reader->lengthInSamples <= 0
            || reader->lengthInSamples > std::numeric_limits<int>::max())
            return jobHasFinished;

        auto buffer = std::make_shared<juce::AudioBuffer<float>>(
            static_cast<int>(reader->numChannels),
            static_cast<int>(reader->lengthInSamples));
        if (!reader->read(buffer.get(), 0, buffer->getNumSamples(), 0, true, true))
            return jobHasFinished;

        auto owner = safeOwner;
        auto* model = trackModel;
        auto file = sourceFile;
        const auto track = targetTrack;
        const auto start = targetSample;
        juce::MessageManager::callAsync([owner, model, file, track, start, buffer]
        {
            if (owner != nullptr && model != nullptr)
                model->addClipToTrack(track, file, start, buffer);
            if (owner != nullptr)
                owner->repaint();
        });
        return jobHasFinished;
    }

private:
    juce::Component::SafePointer<ArrangeWindow> safeOwner;
    TrackDataModel* trackModel = nullptr;
    juce::File sourceFile;
    int targetTrack = 0;
    double targetSample = 0.0;
};
}

TrackHeaderPanel::TrackHeaderPanel(TrackDataModel* model)
    : trackModel(model)
{
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
        g.setColour(index % 2 == 0 ? panelAlt : panelBackground);
        g.fillRect(row);

        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.setFont(15.0f);
        g.drawText("Track " + juce::String(index + 1), row.reduced(12.0f, 10.0f), juce::Justification::centredLeft, true);

        const auto mute = juce::Rectangle<float>(row.getRight() - 66.0f, row.getY() + 16.0f, 20.0f, 20.0f);
        const auto solo = mute.translated(24.0f, 0.0f);
        g.setColour(juce::Colour(0xff333333));
        g.fillRoundedRectangle(mute, 3.0f);
        g.fillRoundedRectangle(solo, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.drawText("M", mute, juce::Justification::centred, true);
        g.drawText("S", solo, juce::Justification::centred, true);
    }
}

TimelineGrid::TimelineGrid(TrackDataModel* model)
    : trackModel(model)
{
    setWantsKeyboardFocus(true);
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
        const auto x = static_cast<float>(beat * beatWidth);
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
        const auto start = trackModel->sampleToXPosition(trackModel->getCycleStartSample(), pixelsPerSecond);
        const auto end = trackModel->sampleToXPosition(trackModel->getCycleEndSample(), pixelsPerSecond);
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
        static_cast<double>(event.position.x) / pixelsPerSecond * trackModel->getSampleRate(), 0.25);
    trackModel->setCycle(dragStart, dragStart + 1.0);
}

void TimelineRuler::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragging || trackModel == nullptr)
        return;
    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto current = trackModel->getSnappedSamplePosition(
        static_cast<double>(event.position.x) / pixelsPerSecond * trackModel->getSampleRate(), 0.25);
    trackModel->setCycle(dragStart, current);
    repaint();
}

void TimelineGrid::mouseDown(const juce::MouseEvent& event)
{
    if (trackModel == nullptr)
        return;

    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto sampleAtMouse = viewStartSample
        + (static_cast<double>(event.position.x) / pixelsPerSecond) * trackModel->getSampleRate();
    const auto track = juce::jlimit(0, static_cast<int>(trackModel->getTrackCount()) - 1,
                                     static_cast<int>(event.position.y / trackModel->getTrackHeight()));
    grabKeyboardFocus();
    const auto& audioClips = trackModel->getTrack(static_cast<size_t>(track)).clips;
    for (size_t index = 0; index < audioClips.size(); ++index)
    {
        const auto& clip = audioClips[index];
        const auto startX = trackModel->sampleToXPosition(clip.startSample - viewStartSample, pixelsPerSecond);
        const auto endX = trackModel->sampleToXPosition(clip.startSample + clip.durationSamples - viewStartSample, pixelsPerSecond);
        if (event.position.x >= startX && event.position.x <= endX)
        {
            selectedTrack = track;
            selectedClip = static_cast<int>(index);
            if (trackModel->getActiveTool() == TrackDataModel::EditTool::Split)
            {
                trackModel->splitAudioClip(static_cast<size_t>(track), index, sampleAtMouse);
                repaint();
                return;
            }
            trimmingLeft = std::abs(event.position.x - startX) <= 5.0;
            trimmingRight = std::abs(event.position.x - endX) <= 5.0;
            setMouseCursor(trimmingLeft || trimmingRight
                               ? juce::MouseCursor::LeftRightResizeCursor
                               : juce::MouseCursor::NormalCursor);
            return;
        }
    }

    const auto& clips = trackModel->getClipBlocks();
    for (size_t index = 0; index < clips.size(); ++index)
    {
        const auto& clip = clips[index];
        if (clip.trackIndex != track)
            continue;

        const auto clipStart = trackModel->sampleToXPosition(clip.startSample - viewStartSample, pixelsPerSecond);
        const auto clipEnd = trackModel->sampleToXPosition(clip.startSample + clip.lengthSamples - viewStartSample, pixelsPerSecond);
        if (event.position.x >= clipStart && event.position.x <= clipEnd)
        {
            draggedClipIndex = static_cast<int>(index);
            dragOffsetSamples = sampleAtMouse - clip.startSample;
            break;
        }
    }
}

void TimelineGrid::mouseDrag(const juce::MouseEvent& event)
{
    if (trackModel == nullptr)
        return;

    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
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
    if (draggedClipIndex < 0)
        return;
    const auto rawSample = viewStartSample
        + (static_cast<double>(event.position.x) / pixelsPerSecond) * trackModel->getSampleRate()
        - dragOffsetSamples;
    auto& clip = trackModel->getClipBlocks()[static_cast<size_t>(draggedClipIndex)];
    const auto snapped = trackModel->getSnappedSamplePosition(rawSample, 0.25);
    if (std::abs(snapped - clip.startSample) >= 1.0)
    {
        clip.startSample = snapped;
        repaint();
    }
}

void TimelineGrid::mouseUp(const juce::MouseEvent&)
{
    draggedClipIndex = -1;
    dragOffsetSamples = 0.0;
    trimmingLeft = false;
    trimmingRight = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

bool TimelineGrid::keyPressed(const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        && selectedTrack >= 0 && selectedClip >= 0 && trackModel != nullptr)
    {
        trackModel->deleteAudioClip(static_cast<size_t>(selectedTrack), static_cast<size_t>(selectedClip));
        selectedTrack = selectedClip = -1;
        repaint();
        return true;
    }
    return false;
}

void TimelineGrid::paint(juce::Graphics& g)
{
    g.fillAll(panelBackground);

    const auto width = static_cast<float>(getWidth());
    const auto height = static_cast<float>(getHeight());
    const auto trackHeight = static_cast<float>(trackModel->getTrackHeight());
    if (trackModel == nullptr)
        return;

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
        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.fillRect(0.0f, rowY, width, trackHeight);

        for (const auto& block : trackModel->getClipBlocks())
        {
            if (block.trackIndex != track)
                continue;

            const auto startX = trackModel->sampleToXPosition(block.startSample - viewStartSample, pixelsPerSecond);
            const auto endX = trackModel->sampleToXPosition(block.startSample + block.lengthSamples - viewStartSample, pixelsPerSecond);
            const auto rect = juce::Rectangle<float>(startX, rowY + 8.0f, endX - startX, trackHeight - 16.0f);

            if (rect.getWidth() <= 0.0f)
                continue;

            g.setColour(block.isMidi ? juce::Colour(0xff4d7fff) : juce::Colour(0xff00ffe0));
            g.fillRoundedRectangle(rect.reduced(2.0f), 8.0f);
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.drawText(block.name.isEmpty() ? "Clip" : block.name, rect.reduced(8.0f), juce::Justification::centredLeft, true);
        }

        const auto& state = trackModel->getTrack(static_cast<size_t>(track));
        for (const auto& clip : state.clips)
        {
            const auto startX = trackModel->sampleToXPosition(clip.startSample - viewStartSample, pixelsPerSecond);
            const auto endX = trackModel->sampleToXPosition(clip.startSample + clip.durationSamples - viewStartSample, pixelsPerSecond);
            const auto rect = juce::Rectangle<float>(startX, rowY + 8.0f, endX - startX, trackHeight - 16.0f);
            if (rect.getWidth() <= 0.0f)
                continue;

            drawPremiumWaveform(g, clip, rect);
        }
    }

    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillRect(0.0f, static_cast<float>(trackCount) * trackHeight, width, 2.0f);
}

ArrangeWindow::ArrangeWindow(TrackDataModel* model)
    : trackHeaderPanel(model),
      timelineRuler(model),
      timelineGrid(model)
{
    trackModel = model;
    addAndMakeVisible(trackHeaderPanel);
    addAndMakeVisible(timelineRuler);
    addAndMakeVisible(timelineGrid);
    horizontalZoomSlider.setRange(0.5, 5.0, 0.01);
    horizontalZoomSlider.setValue(model != nullptr ? model->getHorizontalZoom() : 1.0);
    horizontalZoomSlider.onValueChange = [this]
    {
        if (trackModel != nullptr)
        {
            trackModel->setHorizontalZoom(static_cast<float>(horizontalZoomSlider.getValue()));
            timelineRuler.repaint();
            timelineGrid.repaint();
        }
    };
    trackHeightSlider.setRange(40.0, 120.0, 1.0);
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

    if (model != nullptr)
    {
        model->ensureTrackCount(8);
        for (int i = 0; i < 8; ++i)
        {
            TrackDataModel::ClipBlock clip;
            clip.id = "clip-" + juce::String(i + 1);
            clip.trackIndex = i;
            clip.startSample = static_cast<double>(i * 180000.0);
            clip.lengthSamples = 120000.0;
            clip.isAudio = true;
            clip.isMidi = false;
            clip.name = "Track " + juce::String(i + 1);
            model->addClipBlock(clip);
        }
    }
}

void ArrangeWindow::paint(juce::Graphics& g)
{
    g.fillAll(panelBackground);
    if (isDraggingOver)
    {
        const auto row = juce::Rectangle<float>(0.0f, draggedTrack * 60.0f,
                                                static_cast<float>(getWidth()), 60.0f);
        g.setColour(accentCyan.withAlpha(0.16f));
        g.fillRect(row);
        g.setColour(accentCyan);
        g.drawRect(row, 2.0f);
    }
}

void ArrangeWindow::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(25);
    timelineRuler.setBounds(150, 0, juce::jmax(0, getWidth() - 150), 25);
    const auto leftWidth = 150;
    trackHeaderPanel.setBounds(area.removeFromLeft(leftWidth));
    timelineGrid.setBounds(area);
    horizontalZoomSlider.setBounds(getWidth() - 180, getHeight() - 22, 80, 16);
    trackHeightSlider.setBounds(getWidth() - 90, getHeight() - 22, 80, 16);
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

void ArrangeWindow::fileDragMove(const juce::StringArray&, int x, int y)
{
    draggedTrack = juce::jlimit(0, 7, y / 60);
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

    const auto track = juce::jlimit(0, 7, (y - 25) / juce::jmax(1, trackModel != nullptr ? trackModel->getTrackHeight() : 60));
    const auto timelineX = juce::jmax(0, x - 150);
    const auto pixelsPerSecond = 80.0 * trackModel->getHorizontalZoom();
    const auto rawSample = (static_cast<double>(timelineX) / pixelsPerSecond)
        * trackModel->getSampleRate();
    const auto snappedSample = trackModel->getSnappedSamplePosition(rawSample, 0.25);
    loader.addJob(new AudioLoadJob(juce::Component::SafePointer<ArrangeWindow>(this),
                                   trackModel, juce::File(files[0]), track, snappedSample),
                 true);
}
