#include "NativeVibeAutotuneProcessor.h"

#include "PluginProcessor.h"

#include <cmath>

NativeVibeAutotuneProcessor::NativeVibeAutotuneProcessor(juce::String persistentIdentifier)
    : processor(std::make_unique<VibeAutotuneProcessor>()), identifier(std::move(persistentIdentifier))
{
}

NativeVibeAutotuneProcessor::~NativeVibeAutotuneProcessor() = default;

void NativeVibeAutotuneProcessor::prepareToPlay(double sampleRate, int samplesPerBlock, int numChannels)
{
    if (processor == nullptr)
        return;

    const auto channels = juce::jlimit(1, 2, numChannels);
    auto layout = processor->getBusesLayout();
    layout.getMainInputChannelSet() = channels == 1 ? juce::AudioChannelSet::mono()
                                                     : juce::AudioChannelSet::stereo();
    layout.getMainOutputChannelSet() = layout.getMainInputChannelSet();
    if (! processor->setBusesLayout(layout))
    {
        prepared = false;
        return;
    }

    processor->releaseResources();
    processor->setPlayConfigDetails(channels, channels, sampleRate, samplesPerBlock);
    processor->prepareToPlay(sampleRate, samplesPerBlock);

    // All safety storage is allocated here. The callback only copies samples,
    // advances indices and publishes atomics.
    const auto maximumBlock = juce::jmax(4096, samplesPerBlock);
    dryInputBuffer.setSize(channels, maximumBlock, false, true, true);
    dryHistoryLatency = juce::jmax(0, processor->getLatencySamples());
    dryHistoryBuffer.setSize(channels, juce::jmax(4096, dryHistoryLatency + maximumBlock + 1), false, true, true);
    alignedDryBuffer.setSize(channels, maximumBlock, false, true, true);
    dryHistoryWrite = 0;
    dryHistorySamples = 0;
    healthyWetBlocks = 0;
    consecutiveOverloads = 0;
    continuityTailSamples = 0;
    expectedChannels = channels;
    confidenceGate.reset();
    preparedSampleRate = sampleRate;
    wetMix = 0.0f;
    wasBypassed = true;
    liveState.store(LiveProcessorState::ready, std::memory_order_relaxed);
    publishedWetMix.store(0.0f, std::memory_order_relaxed);
    publishedTrackerVoiced.store(false, std::memory_order_relaxed);
    publishedTrackerConfidence.store(0.0f, std::memory_order_relaxed);
    publishedTrackerF0Hz.store(0.0f, std::memory_order_relaxed);
    publishedTrackerDetectedMidiNote.store(-1, std::memory_order_relaxed);
    publishedTrackerTargetMidiNote.store(-1, std::memory_order_relaxed);
    publishedTrackerOutputMidiNote.store(-1, std::memory_order_relaxed);
    publishedTrackerInputCents.store(0.0f, std::memory_order_relaxed);
    publishedCorrectionCents.store(0.0f, std::memory_order_relaxed);
    publishedRendererCollapsed.store(false, std::memory_order_relaxed);
    publishedRendererExcessive.store(false, std::memory_order_relaxed);
    publishedRendererInvalid.store(false, std::memory_order_relaxed);
    publishedCallbackOverloaded.store(false, std::memory_order_relaxed);
    publishedHardDryRescue.store(false, std::memory_order_relaxed);
    prepared = true;
}

void NativeVibeAutotuneProcessor::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (! prepared || processor == nullptr || buffer.getNumChannels() < expectedChannels)
        return;
    midiBuffer.clear();
    processAndProtect(buffer, midiBuffer);
}

void NativeVibeAutotuneProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    if (! prepared || processor == nullptr || buffer.getNumChannels() < expectedChannels)
        return;
    processAndProtect(buffer, midi);
}

void NativeVibeAutotuneProcessor::processAndProtect(juce::AudioBuffer<float>& buffer,
                                                     juce::MidiBuffer& midi) noexcept
{
    const auto bypassed = processor->getBypassParameter() != nullptr
                       && processor->getBypassParameter()->getValue() >= 0.5f;
    if (bypassed)
    {
        wasBypassed = true;
        wetMix = 0.0f;
        healthyWetBlocks = 0;
        continuityTailSamples = 0;
        confidenceGate.reset();
        liveState.store(LiveProcessorState::ready, std::memory_order_relaxed);
        publishedWetMix.store(0.0f, std::memory_order_relaxed);
        publishedVoiced.store(false, std::memory_order_relaxed);
        publishedTrackerVoiced.store(false, std::memory_order_relaxed);
        publishedTrackerConfidence.store(0.0f, std::memory_order_relaxed);
        publishedTrackerF0Hz.store(0.0f, std::memory_order_relaxed);
        publishedTrackerDetectedMidiNote.store(-1, std::memory_order_relaxed);
        publishedTrackerTargetMidiNote.store(-1, std::memory_order_relaxed);
        publishedTrackerOutputMidiNote.store(-1, std::memory_order_relaxed);
        publishedTrackerInputCents.store(0.0f, std::memory_order_relaxed);
        publishedCorrectionCents.store(0.0f, std::memory_order_relaxed);
        publishedRendererCollapsed.store(false, std::memory_order_relaxed);
        publishedRendererExcessive.store(false, std::memory_order_relaxed);
        publishedRendererInvalid.store(false, std::memory_order_relaxed);
        publishedCallbackOverloaded.store(false, std::memory_order_relaxed);
        publishedHardDryRescue.store(false, std::memory_order_relaxed);
        return;
    }

    const auto samples = buffer.getNumSamples();
    if (samples <= 0)
        return;
    if (samples > dryInputBuffer.getNumSamples())
    {
        // Never run an unprotected renderer when a host violates the prepared
        // maximum block size. The incoming microphone block remains audible.
        liveState.store(LiveProcessorState::protectedDry, std::memory_order_relaxed);
        protectionCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    auto dryPeak = 0.0f;
    const auto historySize = dryHistoryBuffer.getNumSamples();
    const auto historyReady = dryHistoryLatency == 0 || dryHistorySamples >= dryHistoryLatency;

    // Input safety: sanitize non-finite interface/driver samples before they
    // can enter either the renderer or the fallback history.
    for (int channel = 0; channel < expectedChannels; ++channel)
    {
        auto* input = buffer.getWritePointer(channel);
        auto* dry = dryInputBuffer.getWritePointer(channel);
        auto* aligned = alignedDryBuffer.getWritePointer(channel);
        auto* history = dryHistoryBuffer.getWritePointer(channel);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto clean = std::isfinite(input[sample]) ? input[sample] : 0.0f;
            input[sample] = clean;
            dry[sample] = clean;
            dryPeak = juce::jmax(dryPeak, std::abs(clean));
            const auto write = (dryHistoryWrite + sample) % historySize;
            const auto read = (write + historySize - dryHistoryLatency) % historySize;
            aligned[sample] = dryHistoryLatency == 0 ? clean : history[read];
            history[write] = clean;
        }
    }
    dryHistoryWrite = (dryHistoryWrite + samples) % historySize;
    dryHistorySamples = juce::jmin<int64_t>(dryHistoryLatency, dryHistorySamples + samples);

    if (wasBypassed)
    {
        // A fresh enable must prove a valid delayed path before wet can enter.
        wetMix = 0.0f;
        healthyWetBlocks = 0;
        continuityTailSamples = 0;
        wasBypassed = false;
    }

    const auto startedAt = juce::Time::getHighResolutionTicks();
    processor->processBlock(buffer, midi);
    const auto elapsedTicks = juce::Time::getHighResolutionTicks() - startedAt;
    const auto elapsedSeconds = static_cast<double>(elapsedTicks)
                              / static_cast<double>(juce::Time::getHighResolutionTicksPerSecond());
    const auto callbackBudget = static_cast<double>(samples) / juce::jmax(1.0, preparedSampleRate);
    const auto overloaded = elapsedSeconds > callbackBudget * 0.92;
    consecutiveOverloads = overloaded ? consecutiveOverloads + 1 : 0;
    if (overloaded)
        overloadCount.fetch_add(1, std::memory_order_relaxed);

    auto wetPeak = 0.0f;
    auto hasInvalidSample = false;
    for (int channel = 0; channel < expectedChannels; ++channel)
    {
        auto* wet = buffer.getWritePointer(channel);
        const auto* aligned = alignedDryBuffer.getReadPointer(channel);
        for (int sample = 0; sample < samples; ++sample)
        {
            if (! std::isfinite(wet[sample]))
            {
                hasInvalidSample = true;
                wet[sample] = aligned[sample];
            }
            wetPeak = juce::jmax(wetPeak, std::abs(wet[sample]));
        }
    }

    // Do not use dry/wet correlation as a health signal: valid pitch shifting
    // deliberately changes phase. Detect only objective renderer failures.
    const auto wetCollapsed = historyReady && dryPeak > 0.01f && wetPeak < dryPeak * 0.08f;
    const auto wetExcessive = wetPeak > juce::jmax(1.5f, dryPeak * 8.0f + 0.1f);
    auto rendererFault = hasInvalidSample || wetCollapsed || wetExcessive || consecutiveOverloads >= 3;
    publishedRendererCollapsed.store(wetCollapsed, std::memory_order_relaxed);
    publishedRendererExcessive.store(wetExcessive, std::memory_order_relaxed);
    publishedRendererInvalid.store(hasInvalidSample, std::memory_order_relaxed);
    publishedCallbackOverloaded.store(overloaded, std::memory_order_relaxed);
    publishedHardDryRescue.store(false, std::memory_order_relaxed);
    const auto tracking = processor->getPitchTrackingData();
    // Publish the raw tracker decision separately from the native safety
    // gate. A live trace can therefore identify detector rejection, the
    // outer confidence policy, or a renderer fault without guessing from a
    // zero wet-mix value. Sanitise NaN at the audio-thread boundary.
    publishedTrackerVoiced.store(tracking.voiced, std::memory_order_relaxed);
    publishedTrackerConfidence.store(std::isfinite(tracking.confidence)
                                          ? static_cast<float>(tracking.confidence) : 0.0f,
                                      std::memory_order_relaxed);
    publishedTrackerF0Hz.store(std::isfinite(tracking.stableF0Hz) && tracking.stableF0Hz > 0.0
                                    ? static_cast<float>(tracking.stableF0Hz) : 0.0f,
                                std::memory_order_relaxed);
    publishedTrackerDetectedMidiNote.store(tracking.detectedMidiNote, std::memory_order_relaxed);
    publishedTrackerTargetMidiNote.store(tracking.targetMidiNote, std::memory_order_relaxed);
    publishedTrackerOutputMidiNote.store(tracking.outputMidiNote, std::memory_order_relaxed);
    publishedTrackerInputCents.store(std::isfinite(tracking.inputCents) ? tracking.inputCents : 0.0f,
                                     std::memory_order_relaxed);
    publishedCorrectionCents.store(std::isfinite(tracking.correctionCents) ? tracking.correctionCents : 0.0f,
                                   std::memory_order_relaxed);
    // Enter wet only when the tracker is credible. Once live, retain it
    // through a short, bounded tracker dropout so natural vibrato and brief
    // consonants do not pump dry/wet. Safety faults still close immediately.
    const auto voiced = confidenceGate.advance(tracking.voiced, tracking.confidence,
                                               samples, preparedSampleRate);
    publishedVoiced.store(voiced, std::memory_order_relaxed);

    // The detector gate is intentionally separate from host continuity. Once
    // a credible target exists, retain the renderer through one additional,
    // bounded 40 ms tail after the gate closes. This covers consonants and
    // ordinary target hand-offs without treating tracker uncertainty as a
    // renderer fault or changing Vibe's pitch trajectory.
    if (voiced)
        continuityTailSamples = juce::jmax(1, static_cast<int>(std::ceil(preparedSampleRate * 0.040)));
    else
        continuityTailSamples = juce::jmax(0, continuityTailSamples - samples);
    const auto continuityHolding = ! voiced && continuityTailSamples > 0;

    const auto wetHealthy = historyReady && !rendererFault;
    healthyWetBlocks = wetHealthy ? juce::jmin(8, healthyWetBlocks + 1) : 0;
    const auto permitWet = wetHealthy && healthyWetBlocks >= 3 && (voiced || continuityHolding);
    const auto attackSeconds = 0.012;
    const auto releaseSeconds = rendererFault ? 0.003 : 0.008;
    const auto rampSamples = juce::jmax(1.0, preparedSampleRate * (permitWet ? attackSeconds : releaseSeconds));
    const auto targetMix = permitWet ? 1.0f : 0.0f;
    const auto step = static_cast<float>(samples / rampSamples);
    const auto nextMix = targetMix > wetMix ? juce::jmin(targetMix, wetMix + step)
                                             : juce::jmax(targetMix, wetMix - step);

    // The safety path is deliberately the immediate microphone whenever the
    // corrected stream is not audible.  The renderer continues to analyse in
    // the background, but an unverified renderer must never sit in series
    // with a live monitor path: a bad phase-renderer block would otherwise
    // turn a working microphone into silence or broadband noise.  At the
    // first wet-ramp sample we switch to the latency-aligned dry stream, so
    // the established dry/wet crossfade remains phase coherent.
    const auto rendererIsAudible = wetMix > 0.0001f || nextMix > 0.0001f;
    const auto& fallback = rendererIsAudible && historyReady ? alignedDryBuffer
                                                              : dryInputBuffer;
    for (int channel = 0; channel < expectedChannels; ++channel)
    {
        auto* wet = buffer.getWritePointer(channel);
        const auto* dry = fallback.getReadPointer(channel);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto alpha = static_cast<float>(sample + 1) / static_cast<float>(samples);
            const auto mix = wetMix + (nextMix - wetMix) * alpha;
            wet[sample] = dry[sample] + (wet[sample] - dry[sample]) * mix;
        }
    }
    wetMix = nextMix;

    // Final live-audibility invariant. A renderer fault can coincide with the
    // leading edge of a phrase, when the latency-aligned history still holds
    // silence. Never let that turn an audible microphone callback into an
    // inaudible output callback: rescue this block with the sanitized direct
    // input. Healthy renderer output is untouched.
    auto finalPeak = 0.0f;
    for (int channel = 0; channel < expectedChannels; ++channel)
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax(buffer.getReadPointer(channel), samples);
        finalPeak = juce::jmax(finalPeak, juce::jmax(std::abs(range.getStart()), std::abs(range.getEnd())));
    }
    const auto hardDryRescue = dryPeak > 0.003f && finalPeak < dryPeak * 0.05f;
    if (hardDryRescue)
    {
        for (int channel = 0; channel < expectedChannels; ++channel)
            buffer.copyFrom(channel, 0, dryInputBuffer, channel, 0, samples);
        wetMix = 0.0f;
        healthyWetBlocks = 0;
        continuityTailSamples = 0;
        rendererFault = true;
        confidenceGate.reset();
        publishedHardDryRescue.store(true, std::memory_order_relaxed);
    }
    publishedWetMix.store(wetMix, std::memory_order_relaxed);

    if (rendererFault)
    {
        liveState.store(LiveProcessorState::protectedDry, std::memory_order_relaxed);
        protectionCount.fetch_add(1, std::memory_order_relaxed);
    }
    else if (! historyReady || healthyWetBlocks < 3)
        liveState.store(LiveProcessorState::priming, std::memory_order_relaxed);
    else if (voiced)
        liveState.store(LiveProcessorState::tracking, std::memory_order_relaxed);
    else if (continuityHolding)
        liveState.store(LiveProcessorState::holding, std::memory_order_relaxed);
    else
        liveState.store(LiveProcessorState::ready, std::memory_order_relaxed);
}

void NativeVibeAutotuneProcessor::releaseResources()
{
    prepared = false;
    wetMix = 0.0f;
    healthyWetBlocks = 0;
    consecutiveOverloads = 0;
    continuityTailSamples = 0;
    confidenceGate.reset();
    dryHistoryWrite = 0;
    dryHistoryLatency = 0;
    dryHistorySamples = 0;
    dryInputBuffer.setSize(0, 0);
    dryHistoryBuffer.setSize(0, 0);
    alignedDryBuffer.setSize(0, 0);
    if (processor != nullptr)
        processor->releaseResources();
}

juce::String NativeVibeAutotuneProcessor::getName() const { return "VibeAutotune"; }
int NativeVibeAutotuneProcessor::getLatencySamples() const noexcept { return processor != nullptr ? processor->getLatencySamples() : 0; }
LiveProcessorHealth NativeVibeAutotuneProcessor::getLiveProcessorHealth() const noexcept
{
    LiveProcessorHealth health;
    health.state = liveState.load(std::memory_order_relaxed);
    health.wetMix = publishedWetMix.load(std::memory_order_relaxed);
    health.voiced = publishedVoiced.load(std::memory_order_relaxed);
    health.trackerVoiced = publishedTrackerVoiced.load(std::memory_order_relaxed);
    health.trackerConfidence = publishedTrackerConfidence.load(std::memory_order_relaxed);
    health.trackerF0Hz = publishedTrackerF0Hz.load(std::memory_order_relaxed);
    health.trackerDetectedMidiNote = publishedTrackerDetectedMidiNote.load(std::memory_order_relaxed);
    health.trackerTargetMidiNote = publishedTrackerTargetMidiNote.load(std::memory_order_relaxed);
    health.trackerOutputMidiNote = publishedTrackerOutputMidiNote.load(std::memory_order_relaxed);
    health.trackerInputCents = publishedTrackerInputCents.load(std::memory_order_relaxed);
    health.correctionCents = publishedCorrectionCents.load(std::memory_order_relaxed);
    health.rendererCollapsed = publishedRendererCollapsed.load(std::memory_order_relaxed);
    health.rendererExcessive = publishedRendererExcessive.load(std::memory_order_relaxed);
    health.rendererInvalid = publishedRendererInvalid.load(std::memory_order_relaxed);
    health.callbackOverloaded = publishedCallbackOverloaded.load(std::memory_order_relaxed);
    health.hardDryRescue = publishedHardDryRescue.load(std::memory_order_relaxed);
    health.protectionCount = protectionCount.load(std::memory_order_relaxed);
    health.overloadCount = overloadCount.load(std::memory_order_relaxed);
    return health;
}
juce::String NativeVibeAutotuneProcessor::getPersistentIdentifier() const { return identifier; }
double NativeVibeAutotuneProcessor::getTailLengthSeconds() const { return processor != nullptr ? processor->getTailLengthSeconds() : 0.0; }

bool NativeVibeAutotuneProcessor::getState(juce::MemoryBlock& state) const
{
    if (processor == nullptr) return false;
    processor->getStateInformation(state);
    return ! state.isEmpty();
}

bool NativeVibeAutotuneProcessor::setState(const void* data, size_t size)
{
    if (processor == nullptr || data == nullptr || size == 0) return false;
    processor->setStateInformation(data, static_cast<int>(size));
    return true;
}

bool NativeVibeAutotuneProcessor::setParameterValue(size_t index, float normalisedValue)
{
    if (processor == nullptr || index >= static_cast<size_t>(processor->getParameters().size())) return false;
    auto* parameter = processor->getParameters()[static_cast<int>(index)];
    parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalisedValue));
    return true;
}

juce::AudioProcessorParameter* NativeVibeAutotuneProcessor::findParameter(const juce::String& identifier) const
{
    if (processor == nullptr) return nullptr;
    for (auto* parameter : processor->getParameters())
    {
        auto matches = parameter->getName(128).equalsIgnoreCase(identifier);
        if (const auto* identified = dynamic_cast<const juce::AudioProcessorParameterWithID*>(parameter); identified != nullptr)
            matches = matches || identified->getParameterID().equalsIgnoreCase(identifier);
        if (matches) return parameter;
    }
    return nullptr;
}

bool NativeVibeAutotuneProcessor::setParameterValue(const juce::String& identifier, float normalisedValue)
{
    if (auto* parameter = findParameter(identifier); parameter != nullptr)
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalisedValue));
        parameter->endChangeGesture();
        return true;
    }
    return false;
}

bool NativeVibeAutotuneProcessor::setParameterText(const juce::String& identifier, const juce::String& value)
{
    if (auto* parameter = findParameter(identifier); parameter != nullptr)
        return setParameterValue(identifier, parameter->getValueForText(value));
    return false;
}

juce::String NativeVibeAutotuneProcessor::getParameterText(const juce::String& identifier) const
{
    if (const auto* parameter = findParameter(identifier); parameter != nullptr)
        return parameter->getCurrentValueAsText();
    return {};
}

std::vector<EffectParameterDescriptor> NativeVibeAutotuneProcessor::getParameterDescriptors() const
{
    std::vector<EffectParameterDescriptor> descriptors;
    if (processor == nullptr) return descriptors;
    descriptors.reserve(static_cast<size_t>(processor->getParameters().size()));
    for (auto* parameter : processor->getParameters())
    {
        EffectParameterDescriptor descriptor { {}, parameter->getName(128) };
        if (const auto* identified = dynamic_cast<const juce::AudioProcessorParameterWithID*>(parameter); identified != nullptr)
            descriptor.identifier = identified->getParameterID();
        if (descriptor.identifier.isEmpty()) descriptor.identifier = descriptor.name;
        descriptors.push_back(std::move(descriptor));
    }
    return descriptors;
}

bool NativeVibeAutotuneProcessor::hasEditor() const { return processor != nullptr && processor->hasEditor(); }

std::unique_ptr<juce::Component> NativeVibeAutotuneProcessor::createEditor()
{
    return processor != nullptr ? std::unique_ptr<juce::Component>(processor->createEditor()) : nullptr;
}
