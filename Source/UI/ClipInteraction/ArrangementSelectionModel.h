#pragma once

#include "../../Models/ProjectIdentifiers.h"

#include <set>

// Identity-based selection survives track reorder and vector reallocation.
class ArrangementSelectionModel final
{
public:
    void clear() noexcept { audio.clear(); midi.clear(); }
    void selectOnly(ClipId id) { clear(); if (id.isValid()) audio.insert(id.value); }
    void selectOnly(MidiClipId id) { clear(); if (id.isValid()) midi.insert(id.value); }
    void extend(ClipId id) { if (id.isValid()) audio.insert(id.value); }
    void extend(MidiClipId id) { if (id.isValid()) midi.insert(id.value); }
    void toggle(ClipId id) { toggleId(audio, id.value); }
    void toggle(MidiClipId id) { toggleId(midi, id.value); }
    bool contains(ClipId id) const noexcept { return audio.contains(id.value); }
    bool contains(MidiClipId id) const noexcept { return midi.contains(id.value); }
    bool empty() const noexcept { return audio.empty() && midi.empty(); }
private:
    static void toggleId(std::set<uint64_t>& ids, uint64_t id) { if (id != 0 && ! ids.erase(id)) ids.insert(id); }
    std::set<uint64_t> audio;
    std::set<uint64_t> midi;
};
