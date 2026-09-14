#include "MidiCore.h"

#include <algorithm>
#include <cmath>

void MidiScheduler::scheduleBlock(const std::vector<MidiClipState>& clips, double blockStart,
                                  int numSamples, MidiEventBuffer& destination) noexcept
{
    destination.clear();
    const auto blockEnd = blockStart + numSamples;
    for (const auto& clip : clips)
        for (const auto& note : clip.notes)
        {
            const auto on = clip.startSample + note.startSample;
            const auto off = on + note.durationSamples;
            if (on >= blockStart && on < blockEnd)
                destination.add({ clip.trackId, static_cast<int>(on - blockStart), note.pitch, note.velocity, note.channel, true });
            if (off >= blockStart && off < blockEnd)
                destination.add({ clip.trackId, static_cast<int>(off - blockStart), note.pitch, 0.0f, note.channel, false });
        }
}

double MidiScheduler::quantizeSample(double sample, double gridSamples) noexcept
{
    return gridSamples > 0.0 ? std::round(sample / gridSamples) * gridSamples : sample;
}
