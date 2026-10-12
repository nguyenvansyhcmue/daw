#include "WaveformRenderer.h"

#include <cmath>
#include <vector>

namespace
{
float peakMagnitude(WaveformPeak peak) noexcept
{
    return juce::jmax(std::abs(peak.minimum), std::abs(peak.maximum));
}
}

void WaveformRenderer::render(juce::Graphics& g, const WaveformRenderRequest& request) const
{
    const auto body = request.bounds.reduced(1.0f);
    if (body.isEmpty())
        return;

    const auto background = request.state.selected ? style.clipBackground.brighter(0.08f) : style.clipBackground;
    g.setColour(background.withMultipliedAlpha(request.state.muted ? 0.48f : 1.0f));
    g.fillRoundedRectangle(body, 3.0f);
    g.setColour(style.clipOutline.withAlpha(request.state.hover ? 0.92f : 0.68f));
    g.drawRoundedRectangle(body, 3.0f, request.state.selected ? 1.5f : 1.0f);

    const auto waveformBounds = body.reduced(4.0f, 3.0f);
    if (request.thumbnail == nullptr || request.thumbnail->isEmpty())
    {
        g.setColour(style.unavailableText.withAlpha(0.72f));
        g.setFont(10.0f);
        const auto text = request.clip.mediaStatus == AudioMediaStatus::DecodeFailed
            ? "Media unavailable" : "Preparing waveform…";
        g.drawText(text, waveformBounds, juce::Justification::centred, false);
        return;
    }

    // Keep stereo as one silhouette in the arrangement. Dual-mono sources
    // otherwise look like a duplicated waveform; channel detail belongs in
    // the dedicated audio editor.
    drawChannel(g, request, waveformBounds, -1, resolveDisplayReference(request, -1));

    drawFades(g, request, body);
    if (body.getWidth() > 96.0f)
    {
        g.setColour(style.regionText.withAlpha(request.state.muted ? 0.48f : 0.88f));
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::plain)));
        const auto name = request.clip.clipName.isNotEmpty() ? request.clip.clipName
                                                               : request.clip.sourceFile.getFileNameWithoutExtension();
        g.drawText(name, body.reduced(7.0f, 3.0f).withHeight(13.0f), juce::Justification::topLeft, true);
    }
    if (request.state.selected)
    {
        g.setColour(style.selectionOutline.withAlpha(0.85f));
        g.drawRoundedRectangle(body.reduced(0.5f), 3.0f, 1.3f);
    }
}

float WaveformRenderer::resolveDisplayReference(const WaveformRenderRequest& request, int channel) const noexcept
{
    const auto start = request.clip.sourceOffsetSamples;
    const auto visibleLength = request.clip.loopEnabled ? juce::jmax(1.0, request.clip.loopLengthSamples)
                                                        : request.clip.durationSamples;
    const auto end = start + visibleLength;
    auto reference = 0.0f;
    const auto firstChannel = channel < 0 ? 0 : channel;
    const auto lastChannel = channel < 0 ? request.thumbnail->getDisplayChannelCount() - 1 : channel;
    for (auto current = firstChannel; current <= lastChannel; ++current)
        reference = juce::jmax(reference, peakMagnitude(request.thumbnail->peakForSourceRange(start, end, current)));
    if (request.normalization == WaveformNormalization::sourceAccurate)
        reference = request.thumbnail->getDisplayPeak(channel);
    else if (request.normalization == WaveformNormalization::detailAssist)
        reference = juce::jmax(0.045f, reference * 0.72f);
    return juce::jmax(0.045f, reference * juce::jmax(0.01f, request.clip.gain));
}

void WaveformRenderer::drawChannel(juce::Graphics& g, const WaveformRenderRequest& request,
                                   juce::Rectangle<float> bounds, int channel, float displayReference) const
{
    if (bounds.getWidth() <= 1.0f || bounds.getHeight() <= 2.0f)
        return;

    const auto pixels = juce::jmax(1, juce::roundToInt(bounds.getWidth()));
    const auto sourceStart = request.clip.sourceOffsetSamples;
    const auto loopLength = request.clip.loopEnabled ? juce::jmax(1.0, request.clip.loopLengthSamples) : 0.0;
    const auto centre = bounds.getCentreY();
    const auto halfHeight = bounds.getHeight() * 0.44f
        * juce::jlimit(0.25f, 8.0f, request.amplitudeZoom);
    const auto bodyColour = request.state.muted ? style.mutedWaveform : style.waveformBody;

    g.setColour(style.centreLine.withAlpha(0.16f));
    g.drawHorizontalLine(juce::roundToInt(centre), bounds.getX(), bounds.getRight());
    const auto samplesPerPixel = request.clip.durationSamples / pixels;
    if (request.thumbnail->canReadSamples() && loopLength == 0.0 && samplesPerPixel <= 4.0)
    {
        juce::Path line;
        for (int pixel = 0; pixel < pixels; ++pixel)
        {
            const auto source = sourceStart + request.clip.durationSamples * pixel / juce::jmax(1, pixels - 1);
            auto sample = request.thumbnail->sampleAt(source, channel < 0 ? 0 : channel) * request.clip.gain;
            if (channel < 0 && request.thumbnail->getDisplayChannelCount() == 2)
                sample = 0.5f * (sample + request.thumbnail->sampleAt(source, 1) * request.clip.gain);
            const auto x = bounds.getX() + bounds.getWidth() * pixel / juce::jmax(1, pixels - 1);
            const auto y = centre - halfHeight * juce::jlimit(-1.0f, 1.0f, sample / displayReference);
            if (pixel == 0) line.startNewSubPath(x, y); else line.lineTo(x, y);
        }
        g.setColour(bodyColour.withAlpha(0.92f));
        g.strokePath(line, juce::PathStrokeType(1.35f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        return;
    }
    // One envelope column per physical pixel keeps consonants, drum attacks
    // and small edits legible. The cache—not a blur filter—does the LOD work.
    const auto columns = pixels;
    std::vector<juce::Point<float>> upper;
    std::vector<juce::Point<float>> lower;
    std::vector<juce::Point<float>> rmsUpper;
    std::vector<juce::Point<float>> rmsLower;
    upper.reserve(static_cast<size_t>(columns));
    lower.reserve(static_cast<size_t>(columns));
    rmsUpper.reserve(static_cast<size_t>(columns));
    rmsLower.reserve(static_cast<size_t>(columns));
    for (int pixel = 0; pixel < columns; ++pixel)
    {
        const auto timelineFirst = request.clip.durationSamples * pixel / columns;
        const auto timelineLast = request.clip.durationSamples * (pixel + 1) / columns;
        // Keep every visual repetition aligned with playback. A column that
        // crosses a loop seam is sampled from the start of the next pass;
        // renderLoopBoundaries below makes that discontinuity intentional.
        const auto first = sourceStart + (loopLength > 0.0 ? std::fmod(timelineFirst, loopLength) : timelineFirst);
        const auto last = sourceStart + (loopLength > 0.0 ? std::fmod(timelineLast, loopLength) : timelineLast);
        const auto rangeEnd = last > first ? last : first + 1.0;
        auto peak = request.thumbnail->peakForSourceRange(first, rangeEnd, channel < 0 ? 0 : channel);
        if (channel < 0)
            for (auto current = 1; current < request.thumbnail->getDisplayChannelCount(); ++current)
            {
                const auto other = request.thumbnail->peakForSourceRange(first, rangeEnd, current);
                peak.minimum = juce::jmin(peak.minimum, other.minimum);
                peak.maximum = juce::jmax(peak.maximum, other.maximum);
            }
        peak.minimum = juce::jlimit(-1.0f, 1.0f, peak.minimum * request.clip.gain);
        peak.maximum = juce::jlimit(-1.0f, 1.0f, peak.maximum * request.clip.gain);
        const auto top = centre - halfHeight * juce::jlimit(0.0f, 1.0f, peak.maximum / displayReference);
        const auto bottom = centre - halfHeight * juce::jlimit(-1.0f, 0.0f, peak.minimum / displayReference);
        const auto x = bounds.getX() + bounds.getWidth() * pixel / juce::jmax(1, columns - 1);
        upper.emplace_back(x, top);
        lower.emplace_back(x, bottom);
        const auto rmsHeight = halfHeight * juce::jlimit(0.0f, 1.0f,
            peak.rms() * request.clip.gain / displayReference);
        rmsUpper.emplace_back(x, centre - rmsHeight);
        rmsLower.emplace_back(x, centre + rmsHeight);
    }

    if (! upper.empty())
    {
        juce::Path envelope;
        envelope.startNewSubPath(upper.front());
        for (size_t index = 1; index < upper.size(); ++index) envelope.lineTo(upper[index]);
        envelope.lineTo(upper.back());
        for (auto index = lower.size(); index-- > 1;) envelope.lineTo(lower[index - 1]);
        envelope.lineTo(lower.front());
        envelope.closeSubPath();
        g.setColour(bodyColour.withAlpha(request.state.muted ? 0.34f : 0.78f));
        g.fillPath(envelope);
        g.setColour(bodyColour.withAlpha(request.state.muted ? 0.38f : 0.94f));
        g.strokePath(envelope, juce::PathStrokeType(0.75f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

        juce::Path energy;
        energy.startNewSubPath(rmsUpper.front());
        for (size_t index = 1; index < rmsUpper.size(); ++index) energy.lineTo(rmsUpper[index]);
        for (auto index = rmsLower.size(); index-- > 0;) energy.lineTo(rmsLower[index]);
        energy.closeSubPath();
        g.setColour(style.waveformCore.withAlpha(request.state.muted ? 0.04f : 0.055f));
        g.fillPath(energy);
    }

    if (loopLength > 0.0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.46f));
        for (auto loopStart = loopLength; loopStart < request.clip.durationSamples; loopStart += loopLength)
        {
            const auto x = bounds.getX() + bounds.getWidth() * static_cast<float>(loopStart / request.clip.durationSamples);
            g.drawVerticalLine(juce::roundToInt(x), bounds.getY(), bounds.getY() + 8.0f);
            g.drawVerticalLine(juce::roundToInt(x), bounds.getBottom() - 8.0f, bounds.getBottom());
        }
    }
}

void WaveformRenderer::drawFades(juce::Graphics& g, const WaveformRenderRequest& request,
                                 juce::Rectangle<float> body) const
{
    const auto duration = request.clip.durationSamples;
    if (duration <= 0.0)
        return;
    const auto fadeInWidth = body.getWidth() * static_cast<float>(request.clip.fadeInSamples / duration);
    const auto fadeOutWidth = body.getWidth() * static_cast<float>(request.clip.fadeOutSamples / duration);
    g.setColour(style.selectionOutline.withAlpha(0.58f));
    if (fadeInWidth > 0.5f)
    {
        juce::Path fade;
        fade.startNewSubPath(body.getX(), body.getBottom());
        fade.cubicTo(body.getX() + fadeInWidth * 0.20f, body.getBottom(),
                      body.getX() + fadeInWidth * 0.68f, body.getBottom() - body.getHeight() * 0.18f,
                      body.getX() + fadeInWidth, body.getY());
        g.strokePath(fade, juce::PathStrokeType(1.15f));
    }
    if (fadeOutWidth > 0.5f)
    {
        juce::Path fade;
        fade.startNewSubPath(body.getRight() - fadeOutWidth, body.getY());
        fade.cubicTo(body.getRight() - fadeOutWidth * 0.68f, body.getY() + body.getHeight() * 0.18f,
                      body.getRight() - fadeOutWidth * 0.20f, body.getBottom(), body.getRight(), body.getBottom());
        g.strokePath(fade, juce::PathStrokeType(1.15f));
    }
}
