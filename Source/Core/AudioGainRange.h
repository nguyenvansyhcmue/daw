#pragma once

namespace AudioGainRange
{
// +6.0 dB expressed as a linear amplitude multiplier. Shared by send UI,
// project validation, and the realtime renderer.
inline constexpr float maximumSendGain = 1.9952623f;
}
