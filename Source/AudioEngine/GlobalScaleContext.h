#pragma once

#include <atomic>
#include <cstdint>

enum class MusicalScale
{
    chromatic,
    major,
    minor
};

struct ScaleSnapshot
{
    int rootNote = 0;
    MusicalScale scale = MusicalScale::chromatic;
    uint64_t revision = 0;
};

// Single-writer, lock-free scale publication for realtime readers.
// The audio thread only copies a small immutable value and never allocates.
class GlobalScaleContext final
{
public:
    ScaleSnapshot read() const noexcept
    {
        const auto value = packed.load(std::memory_order_acquire);
        return {
            static_cast<int>(value & 0xffu),
            static_cast<MusicalScale>((value >> 8u) & 0x3u),
            value >> 10u
        };
    }

    void publish(int rootNote, MusicalScale scale) noexcept
    {
        const auto revision = nextRevision++;
        const auto value = (revision << 10u)
                         | ((static_cast<uint64_t>(scale) & 0x3u) << 8u)
                         | (static_cast<uint64_t>(rootNote) & 0xffu);
        packed.store(value, std::memory_order_release);
    }

private:
    std::atomic<uint64_t> packed { 0 };
    uint64_t nextRevision = 1;
};
