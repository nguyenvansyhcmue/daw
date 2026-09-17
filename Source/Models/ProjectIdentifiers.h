#pragma once

#include <cstdint>

struct TrackId
{
    uint64_t value = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(TrackId, TrackId) noexcept = default;
};

struct ClipId
{
    uint64_t value = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(ClipId, ClipId) noexcept = default;
};

struct AudioSourceId
{
    uint64_t value = 0;
    [[nodiscard]] constexpr bool isValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(AudioSourceId, AudioSourceId) noexcept = default;
};

struct BusId
{
    uint64_t value = 0;
    [[nodiscard]] constexpr bool isValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(BusId, BusId) noexcept = default;
};

struct MidiClipId { uint64_t value = 0; [[nodiscard]] constexpr bool isValid() const noexcept { return value != 0; } friend constexpr bool operator==(MidiClipId, MidiClipId) noexcept = default; };
struct MidiEventId { uint64_t value = 0; [[nodiscard]] constexpr bool isValid() const noexcept { return value != 0; } friend constexpr bool operator==(MidiEventId, MidiEventId) noexcept = default; };
