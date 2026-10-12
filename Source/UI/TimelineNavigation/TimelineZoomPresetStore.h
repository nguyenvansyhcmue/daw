#pragma once

#include "TimelineViewportState.h"
#include <array>
#include <optional>

class TimelineZoomPresetStore final
{
public:
    void save(int slot, TimelineViewportState state) noexcept { if (slot >= 1 && slot <= 3) presets[static_cast<size_t>(slot - 1)] = state; }
    [[nodiscard]] std::optional<TimelineViewportState> recall(int slot) const noexcept
    { return slot >= 1 && slot <= 3 ? presets[static_cast<size_t>(slot - 1)] : std::nullopt; }
private:
    std::array<std::optional<TimelineViewportState>, 3> presets;
};
