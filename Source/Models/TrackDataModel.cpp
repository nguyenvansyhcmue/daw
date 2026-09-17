#include "TrackDataModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

TrackDataModel::RealtimeSnapshotRead::RealtimeSnapshotRead(const TrackDataModel& owner) noexcept
    : model(owner)
{
    model.realtimeSnapshotReaders.fetch_add(1, std::memory_order_seq_cst);
    audioClips = model.publishedAudioClipSnapshot.load(std::memory_order_seq_cst);
    renderStructure = model.publishedRenderStructure.load(std::memory_order_seq_cst);
    midiClips = model.publishedMidiClipSnapshot.load(std::memory_order_seq_cst);
}

TrackDataModel::RealtimeSnapshotRead::~RealtimeSnapshotRead()
{
    model.realtimeSnapshotReaders.fetch_sub(1, std::memory_order_seq_cst);
}

const std::vector<AudioClipState>* TrackDataModel::RealtimeSnapshotRead::getAudioClips() const noexcept
{
    return audioClips;
}

const TrackDataModel::RenderStructureSnapshot* TrackDataModel::RealtimeSnapshotRead::getRenderStructure() const noexcept
{
    return renderStructure;
}

const TrackDataModel::FxRackSnapshot* TrackDataModel::RealtimeSnapshotRead::getFxRack(size_t trackIndex) const noexcept
{
    return renderStructure != nullptr && trackIndex < renderStructure->trackCount
        ? renderStructure->tracks[trackIndex].fxRack : nullptr;
}
const std::vector<MidiClipState>* TrackDataModel::RealtimeSnapshotRead::getMidiClips() const noexcept { return midiClips; }

int TrackDataModel::RenderStructureSnapshot::getTrackIndex(TrackId id) const noexcept
{
    for (size_t index = 0; index < trackCount; ++index)
        if (tracks[index].id == id) return static_cast<int>(index);
    return -1;
}
int TrackDataModel::RenderStructureSnapshot::getBusIndex(BusId id) const noexcept
{
    for (size_t index = 0; index < busCount; ++index) if (buses[index].id == id) return static_cast<int>(index);
    return -1;
}

TrackDataModel::TrackDataModel()
{
    tempoMap.push_back({ 0.0, 120.0 });
    audioClipSnapshot = std::make_unique<const std::vector<AudioClipState>>();
    publishedAudioClipSnapshot.store(audioClipSnapshot.get(), std::memory_order_seq_cst);
    midiClipSnapshot = std::make_unique<const std::vector<MidiClipState>>();
    publishedMidiClipSnapshot.store(midiClipSnapshot.get(), std::memory_order_seq_cst);
    for (size_t index = 0; index < maxTracks; ++index)
    {
        fxRacks[index] = std::make_unique<const FxRackSnapshot>();
        publishedFxRacks[index].store(fxRacks[index].get(), std::memory_order_seq_cst);
    }
    for (size_t index = 0; index < maxBuses; ++index)
    {
        busFxRacks[index] = std::make_unique<const FxRackSnapshot>();
        publishedBusFxRacks[index].store(busFxRacks[index].get(), std::memory_order_seq_cst);
    }
    publishRenderStructureSnapshot();
    startTimerHz(30);
}

void TrackDataModel::setSampleRate(double value) noexcept { sampleRate.store(std::max(1.0, value), std::memory_order_relaxed); }
void TrackDataModel::setBpm(double value) noexcept
{
    const auto clamped = std::clamp(value, 40.0, 220.0);
    if (bpm.load(std::memory_order_relaxed) != clamped)
    {
        bpm.store(clamped, std::memory_order_relaxed);
        markProjectModified();
    }
}
double TrackDataModel::getBpm() const noexcept { return bpm.load(); }
void TrackDataModel::setTimeSignatureNumerator(int value) noexcept
{
    const auto clamped = std::clamp(value, 1, 32);
    if (timeSignatureNumerator.load(std::memory_order_relaxed) != clamped)
    {
        timeSignatureNumerator.store(clamped, std::memory_order_relaxed);
        markProjectModified();
    }
}
int TrackDataModel::getTimeSignatureNumerator() const noexcept { return timeSignatureNumerator.load(); }

void TrackDataModel::addTempoEvent(double samplePosition, double eventBpm)
{
    tempoMap.push_back({ samplePosition, std::clamp(eventBpm, 40.0, 220.0) });
    std::sort(tempoMap.begin(), tempoMap.end(), [] (const TempoEvent& lhs, const TempoEvent& rhs)
    {
        return lhs.samplePosition < rhs.samplePosition;
    });
    markProjectModified();
}

void TrackDataModel::clearTempoMap()
{
    tempoMap.clear();
    tempoMap.push_back({ 0.0, 120.0 });
    markProjectModified();
}

double TrackDataModel::getTempoAtSample(double samplePosition) const noexcept
{
    if (tempoMap.empty()) return 120.0;
    auto iter = tempoMap.begin();
    while (iter + 1 != tempoMap.end() && (iter + 1)->samplePosition <= samplePosition) ++iter;
    return iter->bpm;
}

TrackDataModel::BarBeatInfo TrackDataModel::getBarBeatInfo(double samplePosition) const noexcept
{
    const auto totalBeats = (samplePosition / getSampleRate()) * getTempoAtSample(samplePosition) / 60.0;
    const auto bars = static_cast<int>(totalBeats / 4.0);
    const auto phase = totalBeats - (bars * 4.0);
    const auto beats = static_cast<int>(phase);
    const auto subBeat = phase - beats;
    const auto pulses = static_cast<int>(subBeat * 960.0);
    return { bars, beats, pulses, subBeat - (pulses / 960.0) };
}

double TrackDataModel::barBeatToSamples(int bars, int beats, int pulses, double subPulse) const noexcept
{
    const auto totalBeats = (bars * 4.0) + static_cast<double>(beats) + (static_cast<double>(pulses) / 960.0) + subPulse;
    return ((totalBeats * 60.0) / getTempoAtSample(0.0)) * getSampleRate();
}

double TrackDataModel::sampleToXPosition(double samplePos, double pixelsPerSecond) const noexcept
{
    return (samplePos / getSampleRate()) * std::max(0.0, pixelsPerSecond);
}

double TrackDataModel::getSnappedSamplePosition(double rawSamplePos, double snapResolution) const noexcept
{
    const auto samplesPerBeat = (getSampleRate() * 60.0) / std::max(1.0, getBpm());
    const auto interval = std::max(1.0, samplesPerBeat * std::max(0.0625, snapResolution * 4.0));
    return std::max(0.0, std::round(rawSamplePos / interval) * interval);
}

bool TrackDataModel::isPlaying() const noexcept { return playing.load(std::memory_order_relaxed); }
void TrackDataModel::setPlaying(bool value) noexcept { playing.store(value, std::memory_order_relaxed); }
double TrackDataModel::getPlayheadPosition() const noexcept { return playheadPosition.load(std::memory_order_relaxed); }
void TrackDataModel::setPlayheadPosition(double value) noexcept { playheadPosition.store(std::max(0.0, value), std::memory_order_relaxed); }
bool TrackDataModel::canChangeTrackStructure() const noexcept { return true; }

void TrackDataModel::ensureTrackCount(size_t count)
{
    if (!canChangeTrackStructure()) return;
    while (trackStates.size() < std::min(count, maxTracks)) addTrack();
}

size_t TrackDataModel::getTrackCount() const noexcept { return trackStates.size(); }

const TrackDataModel::TrackState& TrackDataModel::getTrack(size_t index) const
{
    static const TrackState emptyState;
    return index < trackStates.size() ? trackStates[index] : emptyState;
}

TrackId TrackDataModel::getTrackId(size_t index) const noexcept { return index < trackStates.size() ? trackStates[index].id : TrackId {}; }

int TrackDataModel::getTrackIndex(TrackId id) const noexcept
{
    for (size_t index = 0; index < trackStates.size(); ++index)
        if (trackStates[index].id == id) return static_cast<int>(index);
    return -1;
}

TrackId TrackDataModel::addTrack(TrackType type)
{
    if (!canChangeTrackStructure() || trackStates.size() >= maxTracks) return {};
    auto before = captureEditState();
    trackStates.emplace_back();
    trackStates.back().id = { nextTrackId++ };
    trackStates.back().type = type;
    const auto number = juce::String(static_cast<int>(trackStates.size()));
    trackStates.back().name = type == TrackType::instrument ? "Instrument " + number
        : type == TrackType::externalMidi ? "External MIDI " + number : "Audio " + number;
    for (size_t slot = 0; slot < maxTracks; ++slot)
        if (!fxRackTrackIds[slot].isValid())
        {
            fxRackTrackIds[slot] = trackStates.back().id;
            break;
        }
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    return trackStates.back().id;
}

bool TrackDataModel::removeTrack(TrackId id)
{
    if (!canChangeTrackStructure()) return false;
    const auto index = getTrackIndex(id);
    if (index < 0) return false;
    auto before = captureEditState();
    const auto rackSlot = getFxRackSlot(id);
    trackStates.erase(trackStates.begin() + index);
    if (rackSlot >= 0)
    {
        fxRackTrackIds[static_cast<size_t>(rackSlot)] = {};
        publishFxRackSnapshot(static_cast<size_t>(rackSlot), std::make_unique<FxRackSnapshot>());
    }
    publishAudioClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    return true;
}

bool TrackDataModel::reorderTrack(TrackId id, size_t destinationIndex)
{
    if (!canChangeTrackStructure()) return false;
    const auto sourceIndex = getTrackIndex(id);
    if (sourceIndex < 0 || destinationIndex >= trackStates.size()) return false;
    if (static_cast<size_t>(sourceIndex) == destinationIndex) return true;
    auto before = captureEditState();
    auto track = std::move(trackStates[static_cast<size_t>(sourceIndex)]);
    trackStates.erase(trackStates.begin() + sourceIndex);
    trackStates.insert(trackStates.begin() + static_cast<std::ptrdiff_t>(destinationIndex), std::move(track));
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    return true;
}
BusId TrackDataModel::addBus()
{
    if (busStates.size() >= maxBuses) return {};
    busStates.push_back({ { nextBusId++ } });
    publishRenderStructureSnapshot();
    markProjectModified();
    return busStates.back().id;
}
bool TrackDataModel::removeBus(BusId id)
{
    const auto found = std::find_if(busStates.begin(), busStates.end(), [id] (const BusState& bus) { return bus.id == id; });
    if (found == busStates.end()) return false;
    busStates.erase(found);
    for (auto& track : trackStates) { if (track.outputBus == id) track.outputBus = {}; for (size_t i = 0; i < track.activeSendCount;) { if (track.sends[i].targetBus == id) { for (size_t j = i + 1; j < track.activeSendCount; ++j) track.sends[j - 1] = track.sends[j]; --track.activeSendCount; } else ++i; } }
    publishRenderStructureSnapshot();
    markProjectModified();
    return true;
}
bool TrackDataModel::setBusGain(BusId id, float gain)
{
    const auto found = std::find_if(busStates.begin(), busStates.end(), [id] (const BusState& bus) { return bus.id == id; });
    if (found == busStates.end()) return false;
    const auto value = juce::jlimit(0.0f, 2.0f, gain);
    if (found->gain == value) return true;
    found->gain = value;
    publishRenderStructureSnapshot();
    markProjectModified();
    return true;
}
bool TrackDataModel::setBusMuted(BusId id, bool muted)
{
    const auto found = std::find_if(busStates.begin(), busStates.end(), [id] (const BusState& bus) { return bus.id == id; });
    if (found == busStates.end()) return false;
    if (found->muted == muted) return true;
    found->muted = muted;
    publishRenderStructureSnapshot();
    markProjectModified();
    return true;
}
const TrackDataModel::FxRackSnapshot* TrackDataModel::getBusFxRackSnapshot(BusId bus) const noexcept
{
    const auto index = std::find_if(busStates.begin(), busStates.end(), [bus] (const BusState& state) { return state.id == bus; });
    if (index == busStates.end()) return nullptr;
    return publishedBusFxRacks[static_cast<size_t>(std::distance(busStates.begin(), index))].load(std::memory_order_acquire);
}
void TrackDataModel::setBusFxProcessor(BusId bus, size_t slot, std::shared_ptr<AudioEffectProcessor> processor)
{
    if (slot >= maxFxSlots) return;
    const auto found = std::find_if(busStates.begin(), busStates.end(), [bus] (const BusState& state) { return state.id == bus; });
    if (found == busStates.end()) return;
    const auto index = static_cast<size_t>(std::distance(busStates.begin(), found));
    auto updated = std::make_unique<FxRackSnapshot>();
    if (const auto* current = getBusFxRackSnapshot(bus); current != nullptr) *updated = *current;
    updated->processors[slot] = std::move(processor);
    publishBusFxRackSnapshot(index, std::move(updated));
    publishRenderStructureSnapshot();
}
void TrackDataModel::setBusFxBypassed(BusId bus, size_t slot, bool bypassed) noexcept
{
    if (slot >= maxFxSlots) return;
    const auto found = std::find_if(busStates.begin(), busStates.end(), [bus] (const BusState& state) { return state.id == bus; });
    if (found == busStates.end()) return;
    const auto index = static_cast<size_t>(std::distance(busStates.begin(), found));
    auto updated = std::make_unique<FxRackSnapshot>();
    if (const auto* current = getBusFxRackSnapshot(bus); current != nullptr) *updated = *current;
    updated->bypass[slot] = bypassed;
    publishBusFxRackSnapshot(index, std::move(updated));
    publishRenderStructureSnapshot();
}
bool TrackDataModel::setTrackOutputBus(TrackId trackId, BusId bus)
{
    if (bus.isValid() && std::none_of(busStates.begin(), busStates.end(), [bus] (const BusState& state) { return state.id == bus; })) return false;
    const auto index = getTrackIndex(trackId); if (index < 0) return false;
    trackStates[static_cast<size_t>(index)].outputBus = bus; publishRenderStructureSnapshot(); markProjectModified(); return true;
}
bool TrackDataModel::setTrackSend(TrackId trackId, BusId bus, float amount)
{
    if (! bus.isValid() || std::none_of(busStates.begin(), busStates.end(), [bus] (const BusState& state) { return state.id == bus; })) return false;
    const auto index = getTrackIndex(trackId); if (index < 0) return false;
    return setTrackSendRoute(trackId, 0, bus, amount, false);
}
bool TrackDataModel::setTrackSendRoute(TrackId trackId, size_t route, BusId bus, float amount, bool preFader)
{
    if (route >= maxSendsPerTrack || ! bus.isValid() || std::none_of(busStates.begin(), busStates.end(), [bus] (const BusState& state) { return state.id == bus; })) return false;
    const auto index = getTrackIndex(trackId); if (index < 0) return false;
    auto& track = trackStates[static_cast<size_t>(index)];
    track.sends[route] = { bus, juce::jlimit(0.0f, 1.0f, amount), preFader };
    track.activeSendCount = static_cast<uint8_t>(juce::jmax<int>(track.activeSendCount, static_cast<int>(route + 1)));
    publishRenderStructureSnapshot(); markProjectModified(); return true;
}
bool TrackDataModel::clearTrackSend(TrackId trackId)
{
    const auto index = getTrackIndex(trackId); if (index < 0) return false;
    auto& track = trackStates[static_cast<size_t>(index)];
    if (track.activeSendCount == 0) return true;
    track.activeSendCount = 0;
    publishRenderStructureSnapshot();
    markProjectModified();
    return true;
}

void TrackDataModel::setTrackVolume(size_t index, float value) noexcept
{
    if (index < trackStates.size())
    {
        if (trackStates[index].volume.load(std::memory_order_relaxed) == juce::jlimit(0.0f, 2.0f, value)) return;
        auto before = captureEditState();
        trackStates[index].volume.store(juce::jlimit(0.0f, 2.0f, value), std::memory_order_relaxed);
        publishRenderStructureSnapshot();
        commitEdit(std::move(before));
    }
}
void TrackDataModel::setTrackName(size_t index, const juce::String& value)
{
    if (index >= trackStates.size())
        return;

    const auto name = value.trim().substring(0, 64);
    if (name.isEmpty() || trackStates[index].name == name)
        return;

    auto before = captureEditState();
    trackStates[index].name = name;
    commitEdit(std::move(before));
    sendChangeMessage();
}
void TrackDataModel::setTrackPan(size_t index, float value) noexcept
{
    if (index < trackStates.size())
    {
        if (trackStates[index].pan.load(std::memory_order_relaxed) == juce::jlimit(-1.0f, 1.0f, value)) return;
        auto before = captureEditState();
        trackStates[index].pan.store(juce::jlimit(-1.0f, 1.0f, value), std::memory_order_relaxed);
        publishRenderStructureSnapshot();
        commitEdit(std::move(before));
    }
}
void TrackDataModel::setTrackMuted(size_t index, bool value) noexcept
{
    if (index < trackStates.size())
    {
        if (trackStates[index].muted.load(std::memory_order_relaxed) == value) return;
        auto before = captureEditState();
        trackStates[index].muted.store(value, std::memory_order_relaxed);
        publishRenderStructureSnapshot();
        commitEdit(std::move(before));
    }
}
void TrackDataModel::setTrackSolo(size_t index, bool value) noexcept
{
    if (index < trackStates.size())
    {
        if (trackStates[index].solo.load(std::memory_order_relaxed) == value) return;
        auto before = captureEditState();
        trackStates[index].solo.store(value, std::memory_order_relaxed);
        publishRenderStructureSnapshot();
        commitEdit(std::move(before));
    }
}
void TrackDataModel::setTrackArmed(size_t index, bool value) noexcept
{
    if (index < trackStates.size()) trackStates[index].armed.store(value, std::memory_order_relaxed);
}
bool TrackDataModel::isTrackArmed(size_t index) const noexcept
{
    return index < trackStates.size() && trackStates[index].armed.load(std::memory_order_relaxed);
}
void TrackDataModel::setTrackInputMonitoring(size_t index, bool enabled, int channel) noexcept
{
    if (index >= trackStates.size()) return;
    auto& track = trackStates[index];
    const auto clampedChannel = juce::jmax(0, channel);
    if (track.inputMonitoring.load(std::memory_order_relaxed) == enabled
        && track.inputChannel.load(std::memory_order_relaxed) == clampedChannel)
        return;
    auto before = captureEditState();
    track.inputMonitoring.store(enabled, std::memory_order_relaxed);
    track.inputChannel.store(clampedChannel, std::memory_order_relaxed);
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
}
bool TrackDataModel::isTrackInputMonitoring(size_t index) const noexcept
{
    return index < trackStates.size() && trackStates[index].inputMonitoring.load(std::memory_order_relaxed);
}
int TrackDataModel::getFirstArmedTrackIndex() const noexcept
{
    for (size_t index = 0; index < trackStates.size(); ++index)
        if (trackStates[index].armed.load(std::memory_order_relaxed))
            return static_cast<int>(index);
    return -1;
}
MidiClipId TrackDataModel::addMidiClip(TrackId track, double startSample)
{
    if (getTrackIndex(track) < 0) return {};
    auto before = captureEditState();
    midiClips.push_back({ { nextMidiClipId++ }, track, juce::jmax(0.0, startSample), {} });
    publishMidiClipSnapshot();
    commitEdit(std::move(before));
    return midiClips.back().id;
}
MidiEventId TrackDataModel::addMidiNote(MidiClipId clipId, int pitch, float velocity, double startSample, double durationSamples, int channel)
{
    for (auto& clip : midiClips) if (clip.id == clipId)
    {
        auto before = captureEditState();
        MidiNoteEvent note { { nextMidiEventId++ }, juce::jlimit(0, 127, pitch), juce::jlimit(0.0f, 1.0f, velocity),
                             juce::jmax(0.0, startSample), juce::jmax(1.0, durationSamples), juce::jlimit(1, 16, channel) };
        clip.notes.push_back(note); publishMidiClipSnapshot(); commitEdit(std::move(before)); return note.id;
    }
    return {};
}
const std::vector<MidiClipState>& TrackDataModel::getMidiClips() const noexcept { return midiClips; }
bool TrackDataModel::deleteMidiNote(MidiClipId clipId, MidiEventId noteId)
{
    for (auto& clip : midiClips) if (clip.id == clipId)
    {
        const auto it = std::find_if(clip.notes.begin(), clip.notes.end(), [noteId] (const MidiNoteEvent& note) { return note.id == noteId; });
        if (it == clip.notes.end()) return false;
        auto before = captureEditState();
        clip.notes.erase(it); publishMidiClipSnapshot(); commitEdit(std::move(before)); return true;
    }
    return false;
}
bool TrackDataModel::moveMidiNote(MidiClipId clipId, MidiEventId noteId, int pitch, double startSample)
{
    for (auto& clip : midiClips) if (clip.id == clipId) for (auto& note : clip.notes) if (note.id == noteId)
    { auto before = captureEditState(); note.pitch = juce::jlimit(0, 127, pitch); note.startSample = juce::jmax(0.0, startSample); publishMidiClipSnapshot(); commitEdit(std::move(before)); return true; }
    return false;
}
bool TrackDataModel::resizeMidiNote(MidiClipId clipId, MidiEventId noteId, double durationSamples)
{
    for (auto& clip : midiClips) if (clip.id == clipId) for (auto& note : clip.notes) if (note.id == noteId)
    { auto before = captureEditState(); note.durationSamples = juce::jmax(1.0, durationSamples); publishMidiClipSnapshot(); commitEdit(std::move(before)); return true; }
    return false;
}
bool TrackDataModel::setMidiNoteVelocity(MidiClipId clipId, MidiEventId noteId, float velocity)
{
    for (auto& clip : midiClips) if (clip.id == clipId) for (auto& note : clip.notes) if (note.id == noteId)
    { auto before = captureEditState(); note.velocity = juce::jlimit(0.0f, 1.0f, velocity); publishMidiClipSnapshot(); commitEdit(std::move(before)); return true; }
    return false;
}

ClipId TrackDataModel::addClipToTrack(int trackIndex, const juce::File& file, double startSample,
                                      std::shared_ptr<juce::AudioBuffer<float>> loadedBuffer)
{
    if (trackIndex < 0 || loadedBuffer == nullptr) return {};
    ensureTrackCount(static_cast<size_t>(trackIndex + 1));
    if (static_cast<size_t>(trackIndex) >= trackStates.size()) return {};
    auto before = captureEditState();

    AudioClipState clip;
    clip.id = { nextClipId++ };
    const auto& source = audioMediaPool.registerDecodedSource(file, std::move(loadedBuffer));
    clip.sourceId = source.id;
    clip.sourceFile = source.sourceFile;
    clip.startSample = std::max(0.0, startSample);
    clip.durationSamples = source.playbackData->getNumSamples();
    clip.trackId = trackStates[static_cast<size_t>(trackIndex)].id;
    clip.clipName = file.getFileNameWithoutExtension();
    clip.cachedBuffer = source.playbackData;
    clip.mediaStatus = AudioMediaStatus::Ready;
    trackStates[static_cast<size_t>(trackIndex)].clips.push_back(clip);
    publishAudioClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
    return clip.id;
}

TrackDataModel::RealtimeSnapshotRead TrackDataModel::acquireRealtimeSnapshot() const noexcept { return RealtimeSnapshotRead(*this); }

const TrackDataModel::FxRackSnapshot* TrackDataModel::getFxRackSnapshot(size_t trackIndex) const noexcept
{
    if (trackIndex >= trackStates.size()) return nullptr;
    const auto rackSlot = getFxRackSlot(trackStates[trackIndex].id);
    return rackSlot >= 0 ? publishedFxRacks[static_cast<size_t>(rackSlot)].load(std::memory_order_acquire) : nullptr;
}

TrackDataModel::EditTool TrackDataModel::getActiveTool() const noexcept { return activeTool.load(); }
void TrackDataModel::setActiveTool(EditTool tool) noexcept { activeTool.store(tool); sendChangeMessage(); }
float TrackDataModel::getHorizontalZoom() const noexcept { return horizontalZoom.load(); }
void TrackDataModel::setHorizontalZoom(float zoom) noexcept { horizontalZoom.store(std::clamp(zoom, 0.5f, 5.0f)); sendChangeMessage(); }
int TrackDataModel::getTrackHeight() const noexcept { return trackHeight.load(); }
void TrackDataModel::setTrackHeight(int height) noexcept { trackHeight.store(std::clamp(height, 40, 120)); sendChangeMessage(); }
bool TrackDataModel::isCycleActive() const noexcept { return cycleActive.load(); }
double TrackDataModel::getCycleStartSample() const noexcept { return cycleStartSample.load(); }
double TrackDataModel::getCycleEndSample() const noexcept { return cycleEndSample.load(); }
void TrackDataModel::setCycle(double startSample, double endSample) noexcept
{
    cycleStartSample.store(std::max(0.0, std::min(startSample, endSample)));
    cycleEndSample.store(std::max(0.0, std::max(startSample, endSample)));
    cycleActive.store(endSample != startSample);
    markProjectModified();
    sendChangeMessage();
}
void TrackDataModel::clearCycle() noexcept
{
    if (cycleActive.exchange(false, std::memory_order_relaxed))
        markProjectModified();
    sendChangeMessage();
}

void TrackDataModel::splitAudioClip(size_t trackIndex, size_t clipIndex, double splitSample)
{
    if (trackIndex >= trackStates.size() || clipIndex >= trackStates[trackIndex].clips.size()) return;
    auto before = captureEditState();
    auto& clips = trackStates[trackIndex].clips;
    auto second = clips[clipIndex];
    const auto split = std::clamp(splitSample, second.startSample + 1.0, second.startSample + second.durationSamples - 1.0);
    second.id = { nextClipId++ };
    second.startSample = split;
    second.sourceOffsetSamples += split - clips[clipIndex].startSample;
    second.durationSamples -= split - clips[clipIndex].startSample;
    clips[clipIndex].durationSamples = split - clips[clipIndex].startSample;
    clips.insert(clips.begin() + static_cast<std::ptrdiff_t>(clipIndex + 1), second);
    publishAudioClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
}

void TrackDataModel::trimAudioClip(size_t trackIndex, size_t clipIndex, double startSample, double durationSamples)
{
    if (trackIndex >= trackStates.size() || clipIndex >= trackStates[trackIndex].clips.size()) return;
    auto before = captureEditState();
    auto& clip = trackStates[trackIndex].clips[clipIndex];
    const auto end = clip.startSample + clip.durationSamples;
    const auto newStart = std::clamp(startSample, 0.0, end - 1.0);
    const auto newEnd = std::clamp(newStart + durationSamples, newStart + 1.0, end);
    clip.sourceOffsetSamples += newStart - clip.startSample;
    clip.startSample = newStart;
    clip.durationSamples = newEnd - newStart;
    publishAudioClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
}

void TrackDataModel::deleteAudioClip(size_t trackIndex, size_t clipIndex)
{
    if (trackIndex >= trackStates.size() || clipIndex >= trackStates[trackIndex].clips.size()) return;
    auto before = captureEditState();
    trackStates[trackIndex].clips.erase(trackStates[trackIndex].clips.begin() + static_cast<std::ptrdiff_t>(clipIndex));
    publishAudioClipSnapshot();
    publishRenderStructureSnapshot();
    commitEdit(std::move(before));
    sendChangeMessage();
}

bool TrackDataModel::moveAudioClip(ClipId clipId, TrackId destinationTrack, double startSample)
{
    const auto destinationIndex = getTrackIndex(destinationTrack);
    if (destinationIndex < 0) return false;
    auto before = captureEditState();
    for (auto& track : trackStates)
    {
        const auto iter = std::find_if(track.clips.begin(), track.clips.end(), [clipId] (const AudioClipState& clip) { return clip.id == clipId; });
        if (iter == track.clips.end()) continue;
        if (track.id == destinationTrack)
            iter->startSample = std::max(0.0, startSample);
        else
        {
            auto moved = *iter;
            track.clips.erase(iter);
            moved.trackId = destinationTrack;
            moved.startSample = std::max(0.0, startSample);
            trackStates[static_cast<size_t>(destinationIndex)].clips.push_back(std::move(moved));
        }
        publishAudioClipSnapshot();
        publishRenderStructureSnapshot();
        commitEdit(std::move(before));
        sendChangeMessage();
        return true;
    }
    return false;
}

ClipId TrackDataModel::duplicateAudioClip(ClipId clipId, TrackId destinationTrack, double startSample)
{
    const auto destinationIndex = getTrackIndex(destinationTrack);
    if (destinationIndex < 0) return {};
    for (const auto& track : trackStates)
        for (const auto& clip : track.clips)
            if (clip.id == clipId)
            {
                auto before = captureEditState();
                auto copy = clip;
                copy.id = { nextClipId++ };
                copy.trackId = destinationTrack;
                copy.startSample = std::max(0.0, startSample);
                trackStates[static_cast<size_t>(destinationIndex)].clips.push_back(std::move(copy));
                publishAudioClipSnapshot();
                publishRenderStructureSnapshot();
                commitEdit(std::move(before));
                sendChangeMessage();
                return trackStates[static_cast<size_t>(destinationIndex)].clips.back().id;
            }
    return {};
}

bool TrackDataModel::trimAudioClip(ClipId clipId, double startSample, double durationSamples)
{
    for (size_t trackIndex = 0; trackIndex < trackStates.size(); ++trackIndex)
        for (size_t clipIndex = 0; clipIndex < trackStates[trackIndex].clips.size(); ++clipIndex)
            if (trackStates[trackIndex].clips[clipIndex].id == clipId)
            {
                trimAudioClip(trackIndex, clipIndex, startSample, durationSamples);
                return true;
            }
    return false;
}

bool TrackDataModel::deleteAudioClip(ClipId clipId)
{
    for (size_t trackIndex = 0; trackIndex < trackStates.size(); ++trackIndex)
        for (size_t clipIndex = 0; clipIndex < trackStates[trackIndex].clips.size(); ++clipIndex)
            if (trackStates[trackIndex].clips[clipIndex].id == clipId)
            {
                deleteAudioClip(trackIndex, clipIndex);
                return true;
            }
    return false;
}

bool TrackDataModel::setClipGain(ClipId clipId, float value)
{
    for (auto& track : trackStates)
        for (auto& clip : track.clips)
            if (clip.id == clipId)
            {
                const auto gain = juce::jlimit(0.0f, 4.0f, value);
                if (clip.gain == gain) return true;
                auto before = captureEditState();
                clip.gain = gain;
                publishAudioClipSnapshot();
                publishRenderStructureSnapshot();
                commitEdit(std::move(before));
                sendChangeMessage();
                return true;
            }
    return false;
}

bool TrackDataModel::setClipFades(ClipId clipId, double fadeIn, double fadeOut)
{
    for (auto& track : trackStates)
        for (auto& clip : track.clips)
            if (clip.id == clipId)
            {
                const auto in = juce::jlimit(0.0, clip.durationSamples, fadeIn);
                const auto out = juce::jlimit(0.0, clip.durationSamples, fadeOut);
                if (clip.fadeInSamples == in && clip.fadeOutSamples == out) return true;
                auto before = captureEditState();
                clip.fadeInSamples = in;
                clip.fadeOutSamples = out;
                publishAudioClipSnapshot();
                publishRenderStructureSnapshot();
                commitEdit(std::move(before));
                sendChangeMessage();
                return true;
            }
    return false;
}

bool TrackDataModel::setClipMediaResource(ClipId clipId, const juce::File& file,
                                          std::shared_ptr<juce::AudioBuffer<float>> decodedBuffer)
{
    if (decodedBuffer == nullptr || decodedBuffer->getNumChannels() <= 0 || decodedBuffer->getNumSamples() <= 0)
        return false;
    for (auto& track : trackStates)
        for (auto& clip : track.clips)
            if (clip.id == clipId)
            {
                const auto& source = audioMediaPool.registerDecodedSource(file, std::move(decodedBuffer));
                clip.sourceId = source.id;
                clip.sourceFile = source.sourceFile;
                clip.cachedBuffer = source.playbackData;
                clip.mediaStatus = AudioMediaStatus::Ready;
                publishAudioClipSnapshot();
                publishRenderStructureSnapshot();
                sendChangeMessage();
                return true;
            }
    return false;
}

std::shared_ptr<juce::AudioBuffer<float>> TrackDataModel::findDecodedAudioSource(const juce::File& sourceFile) const
{
    return audioMediaPool.findPlaybackData(sourceFile);
}

bool TrackDataModel::setClipMediaStatus(ClipId clipId, AudioMediaStatus status)
{
    for (auto& track : trackStates)
        for (auto& clip : track.clips)
            if (clip.id == clipId)
            {
                clip.cachedBuffer.reset();
                clip.mediaStatus = status;
                publishAudioClipSnapshot();
                publishRenderStructureSnapshot();
                sendChangeMessage();
                return true;
            }
    return false;
}

AudioMediaStatus TrackDataModel::getClipMediaStatus(ClipId clipId) const noexcept
{
    for (const auto& track : trackStates)
        for (const auto& clip : track.clips)
            if (clip.id == clipId) return clip.mediaStatus;
    return AudioMediaStatus::Unloaded;
}

bool TrackDataModel::canUndo() const noexcept { return ! undoHistory.empty(); }
bool TrackDataModel::canRedo() const noexcept { return ! redoHistory.empty(); }

TrackDataModel::EditState TrackDataModel::captureEditState() const
{
    EditState state;
    state.tracks = trackStates;
    state.buses = busStates;
    state.midiClips = midiClips;
    state.tempo = tempoMap;
    state.bpm = bpm.load(std::memory_order_relaxed);
    state.timeSignatureNumerator = timeSignatureNumerator.load(std::memory_order_relaxed);
    state.cycleActive = cycleActive.load(std::memory_order_relaxed);
    state.cycleStartSample = cycleStartSample.load(std::memory_order_relaxed);
    state.cycleEndSample = cycleEndSample.load(std::memory_order_relaxed);
    state.fxRackTrackIds = fxRackTrackIds;
    for (size_t slot = 0; slot < maxTracks; ++slot)
        if (fxRacks[slot] != nullptr) state.fxRacks[slot] = *fxRacks[slot];
    return state;
}

void TrackDataModel::commitEdit(EditState before)
{
    if (undoHistory.size() == maxUndoEntries) undoHistory.erase(undoHistory.begin());
    undoHistory.push_back(std::move(before));
    redoHistory.clear();
    markProjectModified();
}

void TrackDataModel::restoreEditState(EditState state)
{
    trackStates = std::move(state.tracks);
    busStates = std::move(state.buses);
    midiClips = std::move(state.midiClips);
    tempoMap = std::move(state.tempo);
    bpm.store(state.bpm, std::memory_order_relaxed);
    timeSignatureNumerator.store(state.timeSignatureNumerator, std::memory_order_relaxed);
    cycleActive.store(state.cycleActive, std::memory_order_relaxed);
    cycleStartSample.store(state.cycleStartSample, std::memory_order_relaxed);
    cycleEndSample.store(state.cycleEndSample, std::memory_order_relaxed);
    fxRackTrackIds = state.fxRackTrackIds;
    for (size_t slot = 0; slot < maxTracks; ++slot)
        publishFxRackSnapshot(slot, std::make_unique<FxRackSnapshot>(std::move(state.fxRacks[slot])));
    publishAudioClipSnapshot();
    publishMidiClipSnapshot();
    publishRenderStructureSnapshot();
    markProjectModified();
    sendChangeMessage();
}

bool TrackDataModel::undo()
{
    if (undoHistory.empty()) return false;
    auto current = captureEditState();
    auto previous = std::move(undoHistory.back());
    undoHistory.pop_back();
    redoHistory.push_back(std::move(current));
    restoreEditState(std::move(previous));
    return true;
}

bool TrackDataModel::redo()
{
    if (redoHistory.empty()) return false;
    auto current = captureEditState();
    auto next = std::move(redoHistory.back());
    redoHistory.pop_back();
    undoHistory.push_back(std::move(current));
    restoreEditState(std::move(next));
    return true;
}

void TrackDataModel::clearUndoHistory()
{
    undoHistory.clear();
    redoHistory.clear();
}

ProjectState TrackDataModel::createProjectState() const
{
    ProjectState state;
    state.bpm = getBpm();
    state.timeSignatureNumerator = getTimeSignatureNumerator();
    state.cycleActive = isCycleActive();
    state.cycleStartSample = getCycleStartSample();
    state.cycleEndSample = getCycleEndSample();
    state.tempoMap.reserve(tempoMap.size());
    for (const auto& event : tempoMap) state.tempoMap.push_back({ event.samplePosition, event.bpm });
    state.buses.reserve(busStates.size());
    for (const auto& bus : busStates)
        state.buses.push_back({ bus.id, bus.gain, bus.muted });
    state.tracks.reserve(trackStates.size());
    for (const auto& track : trackStates)
    {
        PersistedTrackState savedTrack;
        savedTrack.id = track.id;
        savedTrack.type = track.type;
        savedTrack.name = track.name;
        savedTrack.volume = track.volume.load(std::memory_order_relaxed);
        savedTrack.pan = track.pan.load(std::memory_order_relaxed);
        savedTrack.muted = track.muted.load(std::memory_order_relaxed);
        savedTrack.solo = track.solo.load(std::memory_order_relaxed);
        savedTrack.inputMonitoring = track.inputMonitoring.load(std::memory_order_relaxed);
        savedTrack.inputChannel = track.inputChannel.load(std::memory_order_relaxed);
        savedTrack.outputBus = track.outputBus;
        savedTrack.activeSendCount = track.activeSendCount;
        for (size_t i = 0; i < track.activeSendCount; ++i) savedTrack.sends[i] = { track.sends[i].targetBus, track.sends[i].level, track.sends[i].preFader };
        savedTrack.clips.reserve(track.clips.size());
        for (const auto& clip : track.clips)
            savedTrack.clips.push_back({ clip.id, clip.trackId, clip.sourceFile.getFullPathName(), clip.clipName,
                                         clip.startSample, clip.durationSamples, clip.sourceOffsetSamples,
                                         clip.gain, clip.fadeInSamples, clip.fadeOutSamples });
        state.tracks.push_back(std::move(savedTrack));
    }
    state.midiClips.reserve(midiClips.size());
    for (const auto& clip : midiClips)
    {
        PersistedMidiClipState savedClip;
        savedClip.id = clip.id;
        savedClip.trackId = clip.trackId;
        savedClip.startSample = clip.startSample;
        savedClip.notes.reserve(clip.notes.size());
        for (const auto& note : clip.notes)
            savedClip.notes.push_back({ note.id, note.pitch, note.velocity, note.startSample,
                                        note.durationSamples, note.channel });
        state.midiClips.push_back(std::move(savedClip));
    }
    return state;
}

juce::Result TrackDataModel::applyProjectState(const ProjectState& state)
{
    if (state.tracks.size() > maxTracks)
        return juce::Result::fail("Project exceeds the current " + juce::String(maxTracks) + "-track engine limit");
    if (! std::isfinite(state.bpm) || state.bpm < 40.0 || state.bpm > 220.0)
        return juce::Result::fail("Project BPM is invalid");
    if (state.timeSignatureNumerator < 1 || state.timeSignatureNumerator > 32)
        return juce::Result::fail("Project time signature is invalid");
    if (state.tempoMap.empty()) return juce::Result::fail("Project tempo map is empty");
    if (! std::isfinite(state.cycleStartSample) || ! std::isfinite(state.cycleEndSample)
        || state.cycleStartSample < 0.0 || state.cycleEndSample < state.cycleStartSample
        || (state.cycleActive && state.cycleEndSample <= state.cycleStartSample))
        return juce::Result::fail("Project cycle range is invalid");
    if (state.buses.size() > maxBuses)
        return juce::Result::fail("Project exceeds the current " + juce::String(maxBuses) + "-bus engine limit");

    uint64_t largestTrackId = 0;
    uint64_t largestBusId = 0;
    uint64_t largestClipId = 0;
    uint64_t largestMidiClipId = 0;
    uint64_t largestMidiEventId = 0;
    std::vector<TrackId> trackIds;
    std::vector<BusId> busIds;
    std::vector<ClipId> clipIds;
    std::vector<MidiClipId> midiClipIds;
    std::vector<MidiEventId> midiEventIds;
    trackIds.reserve(state.tracks.size());
    busIds.reserve(state.buses.size());
    for (const auto& bus : state.buses)
    {
        if (! bus.id.isValid() || std::find(busIds.begin(), busIds.end(), bus.id) != busIds.end()
            || ! std::isfinite(bus.gain) || bus.gain < 0.0f || bus.gain > 2.0f)
            return juce::Result::fail("Project bus state is invalid");
        busIds.push_back(bus.id);
        largestBusId = std::max(largestBusId, bus.id.value);
    }
    for (const auto& track : state.tracks)
    {
        if (! track.id.isValid() || std::find(trackIds.begin(), trackIds.end(), track.id) != trackIds.end()
            || (track.type != TrackType::audio && track.type != TrackType::instrument && track.type != TrackType::externalMidi))
            return juce::Result::fail("Project has duplicate or invalid track IDs");
        if (! std::isfinite(track.volume) || track.volume < 0.0f || track.volume > 2.0f
            || ! std::isfinite(track.pan) || track.pan < -1.0f || track.pan > 1.0f)
            return juce::Result::fail("Project mixer values are invalid");
        if (track.inputChannel < 0 || track.inputChannel > 255)
            return juce::Result::fail("Project input routing is invalid");
        if ((track.outputBus.isValid() && std::find(busIds.begin(), busIds.end(), track.outputBus) == busIds.end())
            || track.activeSendCount > maxSendsPerTrack)
            return juce::Result::fail("Project track bus routing is invalid");
        for (size_t i = 0; i < track.activeSendCount; ++i)
            if (! track.sends[i].targetBus.isValid() || std::find(busIds.begin(), busIds.end(), track.sends[i].targetBus) == busIds.end()
                || ! std::isfinite(track.sends[i].level) || track.sends[i].level < 0.0f || track.sends[i].level > 1.0f)
                return juce::Result::fail("Project track send routing is invalid");
        trackIds.push_back(track.id);
        largestTrackId = std::max(largestTrackId, track.id.value);
        for (const auto& clip : track.clips)
        {
            if (! clip.id.isValid() || clip.trackId != track.id
                || std::find(clipIds.begin(), clipIds.end(), clip.id) != clipIds.end())
                return juce::Result::fail("Project has invalid clip IDs or relationships");
            if (! std::isfinite(clip.startSample) || ! std::isfinite(clip.durationSamples)
                || ! std::isfinite(clip.sourceOffsetSamples) || ! std::isfinite(clip.gain)
                || ! std::isfinite(clip.fadeInSamples) || ! std::isfinite(clip.fadeOutSamples) || clip.startSample < 0.0
                || clip.durationSamples <= 0.0 || clip.sourceOffsetSamples < 0.0
                || clip.gain < 0.0f || clip.gain > 4.0f || clip.fadeInSamples < 0.0
                || clip.fadeOutSamples < 0.0 || clip.fadeInSamples > clip.durationSamples
                || clip.fadeOutSamples > clip.durationSamples)
                return juce::Result::fail("Project clip timing is invalid");
            clipIds.push_back(clip.id);
            largestClipId = std::max(largestClipId, clip.id.value);
        }
    }
    midiClipIds.reserve(state.midiClips.size());
    for (const auto& clip : state.midiClips)
    {
        if (! clip.id.isValid() || std::find(midiClipIds.begin(), midiClipIds.end(), clip.id) != midiClipIds.end()
            || std::find(trackIds.begin(), trackIds.end(), clip.trackId) == trackIds.end()
            || ! std::isfinite(clip.startSample) || clip.startSample < 0.0)
            return juce::Result::fail("Project MIDI clip state is invalid");
        midiClipIds.push_back(clip.id);
        largestMidiClipId = std::max(largestMidiClipId, clip.id.value);
        for (const auto& note : clip.notes)
        {
            if (! note.id.isValid() || std::find(midiEventIds.begin(), midiEventIds.end(), note.id) != midiEventIds.end()
                || note.pitch < 0 || note.pitch > 127 || ! std::isfinite(note.velocity)
                || note.velocity < 0.0f || note.velocity > 1.0f || ! std::isfinite(note.startSample)
                || ! std::isfinite(note.durationSamples) || note.startSample < 0.0 || note.durationSamples <= 0.0
                || note.channel < 1 || note.channel > 16)
                return juce::Result::fail("Project MIDI note state is invalid");
            midiEventIds.push_back(note.id);
            largestMidiEventId = std::max(largestMidiEventId, note.id.value);
        }
    }
    double previousTempoPosition = -1.0;
    for (const auto& event : state.tempoMap)
    {
        if (! std::isfinite(event.samplePosition) || ! std::isfinite(event.bpm) || event.samplePosition < 0.0
            || event.samplePosition < previousTempoPosition || event.bpm < 40.0 || event.bpm > 220.0)
            return juce::Result::fail("Project tempo map is invalid");
        previousTempoPosition = event.samplePosition;
    }
    if (largestTrackId == std::numeric_limits<uint64_t>::max() || largestBusId == std::numeric_limits<uint64_t>::max()
        || largestClipId == std::numeric_limits<uint64_t>::max()
        || largestMidiClipId == std::numeric_limits<uint64_t>::max() || largestMidiEventId == std::numeric_limits<uint64_t>::max())
        return juce::Result::fail("Project ID range cannot allocate new objects safely");

    std::vector<TrackState> restoredTracks;
    restoredTracks.reserve(state.tracks.size());
    for (const auto& track : state.tracks)
    {
        restoredTracks.emplace_back();
        auto& restored = restoredTracks.back();
        restored.id = track.id;
        restored.type = track.type;
        restored.name = track.name.isNotEmpty() ? track.name
                                                : "Track " + juce::String(static_cast<int>(restoredTracks.size()));
        restored.volume.store(track.volume, std::memory_order_relaxed);
        restored.pan.store(track.pan, std::memory_order_relaxed);
        restored.muted.store(track.muted, std::memory_order_relaxed);
        restored.solo.store(track.solo, std::memory_order_relaxed);
        restored.inputMonitoring.store(track.inputMonitoring, std::memory_order_relaxed);
        restored.inputChannel.store(track.inputChannel, std::memory_order_relaxed);
        restored.outputBus = track.outputBus;
        restored.activeSendCount = track.activeSendCount;
        for (size_t i = 0; i < track.activeSendCount; ++i) restored.sends[i] = { track.sends[i].targetBus, track.sends[i].level, track.sends[i].preFader };
        restored.clips.reserve(track.clips.size());
        for (const auto& clip : track.clips)
        {
            AudioClipState restoredClip;
            restoredClip.id = clip.id;
            restoredClip.trackId = clip.trackId;
            restoredClip.sourceFile = juce::File(clip.sourcePath);
            restoredClip.clipName = clip.clipName;
            restoredClip.startSample = clip.startSample;
            restoredClip.durationSamples = clip.durationSamples;
            restoredClip.sourceOffsetSamples = clip.sourceOffsetSamples;
            restoredClip.gain = clip.gain;
            restoredClip.fadeInSamples = clip.fadeInSamples;
            restoredClip.fadeOutSamples = clip.fadeOutSamples;
            restored.clips.push_back(std::move(restoredClip));
        }
    }

    std::vector<MidiClipState> restoredMidiClips;
    restoredMidiClips.reserve(state.midiClips.size());
    for (const auto& clip : state.midiClips)
    {
        MidiClipState restoredClip;
        restoredClip.id = clip.id;
        restoredClip.trackId = clip.trackId;
        restoredClip.startSample = clip.startSample;
        restoredClip.notes.reserve(clip.notes.size());
        for (const auto& note : clip.notes)
            restoredClip.notes.push_back({ note.id, note.pitch, note.velocity, note.startSample,
                                           note.durationSamples, note.channel });
        restoredMidiClips.push_back(std::move(restoredClip));
    }

    std::vector<BusState> restoredBuses;
    restoredBuses.reserve(state.buses.size());
    for (const auto& bus : state.buses)
        restoredBuses.push_back({ bus.id, bus.gain, bus.muted });

    audioMediaPool.clear();
    trackStates = std::move(restoredTracks);
    midiClips = std::move(restoredMidiClips);
    busStates = std::move(restoredBuses);
    tempoMap.clear();
    tempoMap.reserve(state.tempoMap.size());
    for (const auto& event : state.tempoMap) tempoMap.push_back({ event.samplePosition, event.bpm });
    bpm.store(state.bpm, std::memory_order_relaxed);
    timeSignatureNumerator.store(state.timeSignatureNumerator, std::memory_order_relaxed);
    cycleStartSample.store(state.cycleStartSample, std::memory_order_relaxed);
    cycleEndSample.store(state.cycleEndSample, std::memory_order_relaxed);
    cycleActive.store(state.cycleActive, std::memory_order_relaxed);
    playing.store(false, std::memory_order_relaxed);
    playheadPosition.store(0.0, std::memory_order_relaxed);
    nextTrackId = largestTrackId + 1;
    nextBusId = largestBusId + 1;
    nextClipId = largestClipId + 1;
    nextMidiClipId = largestMidiClipId + 1;
    nextMidiEventId = largestMidiEventId + 1;

    for (size_t slot = 0; slot < maxTracks; ++slot)
    {
        fxRackTrackIds[slot] = slot < trackStates.size() ? trackStates[slot].id : TrackId {};
        publishFxRackSnapshot(slot, std::make_unique<FxRackSnapshot>());
    }
    publishAudioClipSnapshot();
    publishMidiClipSnapshot();
    publishRenderStructureSnapshot();
    clearUndoHistory();
    markProjectModified();
    sendChangeMessage();
    return juce::Result::ok();
}

void TrackDataModel::setFxProcessor(size_t trackIndex, size_t slot, std::shared_ptr<AudioEffectProcessor> processor)
{
    if (trackIndex >= trackStates.size() || slot >= maxFxSlots) return;
    const auto rackSlot = getFxRackSlot(trackStates[trackIndex].id);
    if (rackSlot < 0) return;
    auto updated = std::make_unique<FxRackSnapshot>();
    if (const auto* current = getFxRackSnapshot(trackIndex); current != nullptr) *updated = *current;
    updated->processors[slot] = std::move(processor);
    publishFxRackSnapshot(static_cast<size_t>(rackSlot), std::move(updated));
    publishRenderStructureSnapshot();
}

void TrackDataModel::setFxBypassed(size_t trackIndex, size_t slot, bool bypassed) noexcept
{
    if (trackIndex >= trackStates.size() || slot >= maxFxSlots) return;
    const auto rackSlot = getFxRackSlot(trackStates[trackIndex].id);
    if (rackSlot < 0) return;
    auto updated = std::make_unique<FxRackSnapshot>();
    if (const auto* current = getFxRackSnapshot(trackIndex); current != nullptr) *updated = *current;
    updated->bypass[slot] = bypassed;
    publishFxRackSnapshot(static_cast<size_t>(rackSlot), std::move(updated));
    publishRenderStructureSnapshot();
}

void TrackDataModel::publishAudioClipSnapshot()
{
    auto updated = std::make_unique<std::vector<AudioClipState>>();
    for (const auto& state : trackStates) updated->insert(updated->end(), state.clips.begin(), state.clips.end());
    std::unique_ptr<const std::vector<AudioClipState>> next = std::move(updated);
    auto previous = std::move(audioClipSnapshot);
    audioClipSnapshot = std::move(next);
    publishedAudioClipSnapshot.store(audioClipSnapshot.get(), std::memory_order_seq_cst);
    if (previous != nullptr) retiredAudioClipSnapshots.push_back(std::move(previous));
}
void TrackDataModel::publishMidiClipSnapshot()
{
    auto previous = std::move(midiClipSnapshot);
    midiClipSnapshot = std::make_unique<const std::vector<MidiClipState>>(midiClips);
    publishedMidiClipSnapshot.store(midiClipSnapshot.get(), std::memory_order_seq_cst);
    if (previous != nullptr) retiredMidiClipSnapshots.push_back(std::move(previous));
}

void TrackDataModel::publishFxRackSnapshot(size_t trackIndex, std::unique_ptr<const FxRackSnapshot> snapshot)
{
    auto previous = std::move(fxRacks[trackIndex]);
    fxRacks[trackIndex] = std::move(snapshot);
    publishedFxRacks[trackIndex].store(fxRacks[trackIndex].get(), std::memory_order_seq_cst);
    if (previous != nullptr) retiredFxRackSnapshots[trackIndex].push_back(std::move(previous));
}
void TrackDataModel::publishBusFxRackSnapshot(size_t busIndex, std::unique_ptr<const FxRackSnapshot> snapshot)
{
    if (busIndex >= maxBuses || snapshot == nullptr) return;
    auto previous = std::move(busFxRacks[busIndex]);
    busFxRacks[busIndex] = std::move(snapshot);
    publishedBusFxRacks[busIndex].store(busFxRacks[busIndex].get(), std::memory_order_release);
    if (previous != nullptr) retiredBusFxRacks[busIndex].push_back(std::move(previous));
}

void TrackDataModel::publishRenderStructureSnapshot()
{
    auto next = std::make_unique<RenderStructureSnapshot>();
    next->trackCount = trackStates.size();
    next->busCount = busStates.size();
    for (size_t index = 0; index < next->busCount; ++index)
        next->buses[index] = { busStates[index].id, busStates[index].gain, busStates[index].muted,
                               publishedBusFxRacks[index].load(std::memory_order_acquire) };
    for (size_t index = 0; index < next->trackCount; ++index)
    {
        const auto& track = trackStates[index];
        auto& renderTrack = next->tracks[index];
        renderTrack.id = track.id;
        renderTrack.type = track.type;
        renderTrack.volume = track.volume.load(std::memory_order_relaxed);
        renderTrack.pan = track.pan.load(std::memory_order_relaxed);
        renderTrack.muted = track.muted.load(std::memory_order_relaxed);
        renderTrack.solo = track.solo.load(std::memory_order_relaxed);
        renderTrack.inputMonitoring = track.inputMonitoring.load(std::memory_order_relaxed);
        renderTrack.inputChannel = track.inputChannel.load(std::memory_order_relaxed);
        const auto rackSlot = getFxRackSlot(track.id);
        renderTrack.fxRack = rackSlot >= 0 ? publishedFxRacks[static_cast<size_t>(rackSlot)].load(std::memory_order_acquire) : nullptr;
        renderTrack.outputBus = track.outputBus;
        renderTrack.activeSendCount = track.activeSendCount;
        for (size_t i = 0; i < track.activeSendCount; ++i) renderTrack.sends[i] = { track.sends[i].targetBus, track.sends[i].level, track.sends[i].preFader };
    }
    std::unique_ptr<const RenderStructureSnapshot> snapshot = std::move(next);
    auto previous = std::move(renderStructureSnapshot);
    renderStructureSnapshot = std::move(snapshot);
    publishedRenderStructure.store(renderStructureSnapshot.get(), std::memory_order_seq_cst);
    if (previous != nullptr) retiredRenderStructures.push_back(std::move(previous));
    reclaimRetiredRealtimeSnapshots();
}

int TrackDataModel::getFxRackSlot(TrackId trackId) const noexcept
{
    for (size_t slot = 0; slot < maxTracks; ++slot)
        if (fxRackTrackIds[slot] == trackId) return static_cast<int>(slot);
    return -1;
}

void TrackDataModel::reclaimRetiredRealtimeSnapshots()
{
    if (realtimeSnapshotReaders.load(std::memory_order_seq_cst) != 0) return;
    retiredAudioClipSnapshots.clear();
    retiredMidiClipSnapshots.clear();
    for (auto& retiredRacks : retiredFxRackSnapshots) retiredRacks.clear();
    for (auto& retiredRacks : retiredBusFxRacks) retiredRacks.clear();
    retiredRenderStructures.clear();
}

size_t TrackDataModel::getRetiredRealtimeSnapshotCount() const noexcept
{
    size_t count = retiredAudioClipSnapshots.size();
    count += retiredMidiClipSnapshots.size();
    for (const auto& retiredRacks : retiredFxRackSnapshots) count += retiredRacks.size();
    for (const auto& retiredRacks : retiredBusFxRacks) count += retiredRacks.size();
    count += retiredRenderStructures.size();
    return count;
}

void TrackDataModel::timerCallback()
{
    reclaimRetiredRealtimeSnapshots();
}
