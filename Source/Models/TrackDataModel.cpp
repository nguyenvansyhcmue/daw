#include "TrackDataModel.h"

#include <algorithm>
#include <cmath>

TrackDataModel::TrackDataModel()
{
    tempoMap.push_back({ 0.0, 120.0 });
    audioClipSnapshot = std::make_shared<const std::vector<AudioClipState>>();
    for (auto& rack : fxRacks)
        rack = std::make_shared<const FxRackSnapshot>();
}

void TrackDataModel::setSampleRate(double sampleRate) noexcept
{
    this->sampleRate = std::max(1.0, sampleRate);
}

void TrackDataModel::setBpm(double newBpm) noexcept
{
    bpm.store(std::clamp(newBpm, 40.0, 220.0));
}

double TrackDataModel::getBpm() const noexcept
{
    return bpm.load();
}

void TrackDataModel::setTimeSignatureNumerator(int numerator) noexcept
{
    timeSignatureNumerator.store(std::clamp(numerator, 1, 32));
}

int TrackDataModel::getTimeSignatureNumerator() const noexcept
{
    return timeSignatureNumerator.load();
}

void TrackDataModel::addTempoEvent(double samplePosition, double bpm)
{
    tempoMap.push_back({ samplePosition, std::max(40.0, std::min(220.0, bpm)) });
    std::sort(tempoMap.begin(), tempoMap.end(), [](const TempoEvent& lhs, const TempoEvent& rhs)
              {
                  return lhs.samplePosition < rhs.samplePosition;
              });
}

void TrackDataModel::clearTempoMap()
{
    tempoMap.clear();
    tempoMap.push_back({ 0.0, 120.0 });
}

double TrackDataModel::getTempoAtSample(double samplePosition) const noexcept
{
    if (tempoMap.empty())
        return 120.0;

    auto iter = tempoMap.begin();
    while (iter + 1 != tempoMap.end() && (iter + 1)->samplePosition <= samplePosition)
        ++iter;

    return iter->bpm;
}

TrackDataModel::BarBeatInfo TrackDataModel::getBarBeatInfo(double samplePosition) const noexcept
{
    const double bpm = getTempoAtSample(samplePosition);
    const double seconds = samplePosition / sampleRate;
    const double totalBeats = seconds * bpm / 60.0;
    const int bars = static_cast<int>(totalBeats / 4.0);
    const double phase = totalBeats - (bars * 4.0);
    const int beats = static_cast<int>(phase);
    const double subBeat = phase - beats;
    const int pulses = static_cast<int>(subBeat * 960.0);

    BarBeatInfo result;
    result.bars = bars;
    result.beats = beats;
    result.pulses = pulses;
    result.subPulse = subBeat - (pulses / 960.0);
    return result;
}

double TrackDataModel::barBeatToSamples(int bars, int beats, int pulses, double subPulse) const noexcept
{
    const double bpm = getTempoAtSample(0.0);
    const double totalBeats = (bars * 4.0) + static_cast<double>(beats) + (static_cast<double>(pulses) / 960.0) + subPulse;
    const double seconds = (totalBeats * 60.0) / bpm;
    return seconds * sampleRate;
}

double TrackDataModel::sampleToXPosition(double samplePos, double pixelsPerSecond) const noexcept
{
    return (samplePos / std::max(1.0, sampleRate)) * std::max(0.0, pixelsPerSecond);
}

double TrackDataModel::getSnappedSamplePosition(double rawSamplePos, double snapResolution) const noexcept
{
    const auto samplesPerBeat = (std::max(1.0, sampleRate) * 60.0) / std::max(1.0, getBpm());
    const auto interval = std::max(1.0, samplesPerBeat * std::max(0.0625, snapResolution * 4.0));
    return std::max(0.0, std::round(rawSamplePos / interval) * interval);
}

void TrackDataModel::ensureTrackCount(size_t count)
{
    if (trackStates.size() >= count)
        return;

    trackStates.resize(count);
}

size_t TrackDataModel::getTrackCount() const noexcept
{
    return trackStates.size();
}

TrackDataModel::TrackState& TrackDataModel::getTrack(size_t index)
{
    ensureTrackCount(index + 1);
    return trackStates[index];
}

const TrackDataModel::TrackState& TrackDataModel::getTrack(size_t index) const
{
    static const TrackState emptyState;
    if (index >= trackStates.size())
        return emptyState;
    return trackStates[index];
}

void TrackDataModel::addClipBlock(const ClipBlock& clip)
{
    clipBlocks.push_back(clip);
}

const std::vector<TrackDataModel::ClipBlock>& TrackDataModel::getClipBlocks() const noexcept
{
    return clipBlocks;
}

std::vector<TrackDataModel::ClipBlock>& TrackDataModel::getClipBlocks() noexcept
{
    return clipBlocks;
}

bool TrackDataModel::isPlaying() const noexcept
{
    return playing.load(std::memory_order_relaxed);
}

void TrackDataModel::setPlaying(bool shouldPlay) noexcept
{
    playing.store(shouldPlay, std::memory_order_relaxed);
}

double TrackDataModel::getPlayheadPosition() const noexcept
{
    return playheadPosition.load(std::memory_order_relaxed);
}

void TrackDataModel::setPlayheadPosition(double samplePosition) noexcept
{
    playheadPosition.store(std::max(0.0, samplePosition), std::memory_order_relaxed);
}

void TrackDataModel::addClipToTrack(int trackID, const juce::File& file, double startSample,
                                    std::shared_ptr<juce::AudioBuffer<float>> loadedBuffer)
{
    if (trackID < 0 || loadedBuffer == nullptr)
        return;

    ensureTrackCount(static_cast<size_t>(trackID + 1));
    AudioClipState clip;
    clip.sourceFile = file;
    clip.startSample = std::max(0.0, startSample);
    clip.durationSamples = loadedBuffer->getNumSamples();
    clip.trackID = trackID;
    clip.clipName = file.getFileNameWithoutExtension();
    clip.cachedBuffer = std::move(loadedBuffer);
    trackStates[static_cast<size_t>(trackID)].clips.push_back(clip);

    auto updated = std::make_shared<std::vector<AudioClipState>>(
        audioClipSnapshot != nullptr ? *audioClipSnapshot : std::vector<AudioClipState> {});
    updated->push_back(std::move(clip));
    std::atomic_store_explicit(&audioClipSnapshot,
                               std::shared_ptr<const std::vector<AudioClipState>>(std::move(updated)),
                               std::memory_order_release);
    sendChangeMessage();
}

std::shared_ptr<const std::vector<AudioClipState>> TrackDataModel::getAudioClipSnapshot() const noexcept
{
    return std::atomic_load_explicit(&audioClipSnapshot, std::memory_order_acquire);
}

std::shared_ptr<const TrackDataModel::FxRackSnapshot>
TrackDataModel::getFxRackSnapshot(size_t trackID) const noexcept
{
    if (trackID >= fxRacks.size())
        return {};
    return std::atomic_load_explicit(&fxRacks[trackID], std::memory_order_acquire);
}

TrackDataModel::EditTool TrackDataModel::getActiveTool() const noexcept { return activeTool.load(); }
void TrackDataModel::setActiveTool(EditTool tool) noexcept
{
    activeTool.store(tool);
    sendChangeMessage();
}
float TrackDataModel::getHorizontalZoom() const noexcept { return horizontalZoom.load(); }
void TrackDataModel::setHorizontalZoom(float zoom) noexcept
{
    horizontalZoom.store(std::clamp(zoom, 0.5f, 5.0f));
    sendChangeMessage();
}
int TrackDataModel::getTrackHeight() const noexcept { return trackHeight.load(); }
void TrackDataModel::setTrackHeight(int height) noexcept
{
    trackHeight.store(std::clamp(height, 40, 120));
    sendChangeMessage();
}
bool TrackDataModel::isCycleActive() const noexcept { return cycleActive.load(); }
double TrackDataModel::getCycleStartSample() const noexcept { return cycleStartSample.load(); }
double TrackDataModel::getCycleEndSample() const noexcept { return cycleEndSample.load(); }
void TrackDataModel::setCycle(double startSample, double endSample) noexcept
{
    cycleStartSample.store(std::max(0.0, std::min(startSample, endSample)));
    cycleEndSample.store(std::max(0.0, std::max(startSample, endSample)));
    cycleActive.store(endSample != startSample);
    sendChangeMessage();
}
void TrackDataModel::clearCycle() noexcept { cycleActive.store(false); sendChangeMessage(); }

void TrackDataModel::splitAudioClip(size_t trackID, size_t clipIndex, double splitSample)
{
    if (trackID >= trackStates.size() || clipIndex >= trackStates[trackID].clips.size())
        return;
    auto& clips = trackStates[trackID].clips;
    auto original = clips[clipIndex];
    const auto split = std::clamp(splitSample, original.startSample + 1.0,
                                  original.startSample + original.durationSamples - 1.0);
    auto second = original;
    second.startSample = split;
    second.sourceOffsetSamples += split - original.startSample;
    second.durationSamples = original.durationSamples - (split - original.startSample);
    clips[clipIndex].durationSamples = split - original.startSample;
    clips.insert(clips.begin() + static_cast<std::ptrdiff_t>(clipIndex + 1), second);
    auto snapshot = std::make_shared<std::vector<AudioClipState>>();
    for (const auto& state : trackStates)
        snapshot->insert(snapshot->end(), state.clips.begin(), state.clips.end());
    std::atomic_store(&audioClipSnapshot, std::shared_ptr<const std::vector<AudioClipState>>(std::move(snapshot)));
    sendChangeMessage();
}

void TrackDataModel::trimAudioClip(size_t trackID, size_t clipIndex, double startSample, double durationSamples)
{
    if (trackID >= trackStates.size() || clipIndex >= trackStates[trackID].clips.size())
        return;
    auto& clip = trackStates[trackID].clips[clipIndex];
    const auto end = clip.startSample + clip.durationSamples;
    const auto newStart = std::clamp(startSample, 0.0, end - 1.0);
    const auto newEnd = std::clamp(newStart + durationSamples, newStart + 1.0, end);
    clip.sourceOffsetSamples += newStart - clip.startSample;
    clip.startSample = newStart;
    clip.durationSamples = newEnd - newStart;
    auto snapshot = std::make_shared<std::vector<AudioClipState>>();
    for (const auto& state : trackStates)
        snapshot->insert(snapshot->end(), state.clips.begin(), state.clips.end());
    std::atomic_store(&audioClipSnapshot, std::shared_ptr<const std::vector<AudioClipState>>(std::move(snapshot)));
    sendChangeMessage();
}

void TrackDataModel::deleteAudioClip(size_t trackID, size_t clipIndex)
{
    if (trackID >= trackStates.size() || clipIndex >= trackStates[trackID].clips.size())
        return;
    trackStates[trackID].clips.erase(trackStates[trackID].clips.begin() + static_cast<std::ptrdiff_t>(clipIndex));
    auto snapshot = std::make_shared<std::vector<AudioClipState>>();
    for (const auto& state : trackStates)
        snapshot->insert(snapshot->end(), state.clips.begin(), state.clips.end());
    std::atomic_store(&audioClipSnapshot, std::shared_ptr<const std::vector<AudioClipState>>(std::move(snapshot)));
    sendChangeMessage();
}

void TrackDataModel::setFxProcessor(size_t trackID, size_t slot,
                                    std::shared_ptr<AudioEffectProcessor> processor)
{
    if (trackID >= fxRacks.size() || slot >= maxFxSlots)
        return;

    const auto current = getFxRackSnapshot(trackID);
    auto updated = std::make_shared<FxRackSnapshot>();
    if (current != nullptr)
        *updated = *current;
    updated->processors[slot] = std::move(processor);
    std::atomic_store_explicit(&fxRacks[trackID],
                               std::shared_ptr<const FxRackSnapshot>(std::move(updated)),
                               std::memory_order_release);
    sendChangeMessage();
}

void TrackDataModel::setFxBypassed(size_t trackID, size_t slot, bool bypassed) noexcept
{
    if (trackID >= fxRacks.size() || slot >= maxFxSlots)
        return;

    const auto current = getFxRackSnapshot(trackID);
    if (current != nullptr)
        const_cast<std::atomic<bool>&>(current->bypass[slot]).store(bypassed,
                                                                      std::memory_order_release);
}
