#pragma once

namespace AudioGainRange
{
// +12 dB is the visible channel/master-fader ceiling.  Keeping this in one
// shared conversion point prevents the UI scale and realtime gain limits from
// disagreeing.
inline constexpr float maximumChannelGain = 3.9810717f;
// +6.0 dB expressed as a linear amplitude multiplier. Shared by send UI,
// project validation, and the realtime renderer.
inline constexpr float maximumSendGain = 1.9952623f;
}
