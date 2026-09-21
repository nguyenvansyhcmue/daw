#pragma once

#include <array>
#include <atomic>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct VocalistConfig
{
    uint32_t id = 0;
    int inputChannel = 0;
    float gain = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
};

struct VocalistRackSnapshot
{
    static constexpr size_t maxVocalists = 8;
    size_t count = 0;
    std::array<VocalistConfig, maxVocalists> vocalists {};
};

// Control-thread writer / realtime-reader rack. Writers publish complete
// immutable snapshots; readers hold a guard for the duration of processing.
class VocalistRack final
{
public:
    class Read final
    {
    public:
        explicit Read(const VocalistRack& owner) noexcept : rack(owner)
        {
            rack.readers.fetch_add(1, std::memory_order_seq_cst);
            snapshot = rack.published.load(std::memory_order_seq_cst);
        }

        ~Read() { rack.readers.fetch_sub(1, std::memory_order_seq_cst); }
        Read(const Read&) = delete;
        Read& operator=(const Read&) = delete;
        const VocalistRackSnapshot* operator->() const noexcept { return snapshot; }
        const VocalistRackSnapshot& get() const noexcept { return *snapshot; }

    private:
        const VocalistRack& rack;
        const VocalistRackSnapshot* snapshot = nullptr;
    };

    VocalistRack()
    {
        auto initial = std::make_unique<VocalistRackSnapshot>();
        current = std::move(initial);
        published.store(current.get(), std::memory_order_release);
    }

    Read acquire() const noexcept { return Read(*this); }

    bool addVocalist(int inputChannel, uint32_t& createdId)
    {
        if (current->count >= VocalistRackSnapshot::maxVocalists)
            return false;
        auto next = std::make_unique<VocalistRackSnapshot>(*current);
        createdId = nextId++;
        next->vocalists[next->count++] = { createdId, inputChannel, 1.0f, 0.0f, false, false };
        publish(std::move(next));
        return true;
    }

    bool removeVocalist(uint32_t id)
    {
        auto next = std::make_unique<VocalistRackSnapshot>();
        const auto removed = copyVocalistsExcept(*next, *current, id);
        if (! removed)
            return false;
        publish(std::move(next));
        return true;
    }

    bool setGain(uint32_t id, float value)
    {
        return updateVocalist(id, [value] (auto& vocalist) { vocalist.gain = std::clamp(value, 0.0f, 2.0f); });
    }

    bool setMuted(uint32_t id, bool value)
    {
        return updateVocalist(id, [value] (auto& vocalist) { vocalist.muted = value; });
    }

    bool setInputChannel(uint32_t id, int channel)
    {
        return updateVocalist(id, [channel] (auto& vocalist) { vocalist.inputChannel = std::max(0, channel); });
    }

    size_t retiredCount() const noexcept { return retired.size(); }
    void reclaim() noexcept
    {
        if (readers.load(std::memory_order_seq_cst) == 0)
            retired.clear();
    }

private:
    static bool copyVocalistsExcept(VocalistRackSnapshot& destination,
                                    const VocalistRackSnapshot& source,
                                    uint32_t excludedId) noexcept
    {
        for (size_t index = 0; index < source.count; ++index)
            if (source.vocalists[index].id != excludedId)
                destination.vocalists[destination.count++] = source.vocalists[index];
        return destination.count != source.count;
    }

    template <typename Mutation>
    bool updateVocalist(uint32_t id, Mutation mutation)
    {
        auto next = std::make_unique<VocalistRackSnapshot>(*current);
        for (size_t i = 0; i < next->count; ++i)
            if (next->vocalists[i].id == id)
            {
                mutation(next->vocalists[i]);
                publish(std::move(next));
                return true;
            }
        return false;
    }

    void publish(std::unique_ptr<VocalistRackSnapshot> next)
    {
        auto previous = std::move(current);
        current = std::move(next);
        published.store(current.get(), std::memory_order_release);
        retired.push_back(std::move(previous));
    }

    uint32_t nextId = 1;
    std::unique_ptr<VocalistRackSnapshot> current;
    std::vector<std::unique_ptr<VocalistRackSnapshot>> retired;
    mutable std::atomic<const VocalistRackSnapshot*> published { nullptr };
    mutable std::atomic<unsigned int> readers { 0 };
};
