#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

struct SendRouteSnapshot
{
    uint32_t auxId = 0;
    float level = 0.0f;
    bool preFader = false;
};

struct AuxReturnSnapshot
{
    uint32_t id = 0;
    float gain = 1.0f;
    bool muted = false;
};

struct AuxRoutingSnapshot
{
    static constexpr size_t maxVocalists = 8;
    static constexpr size_t maxAuxReturns = 4;
    static constexpr size_t maxSendsPerVocalist = 4;

    size_t auxCount = 0;
    std::array<AuxReturnSnapshot, maxAuxReturns> auxReturns {};
    std::array<std::array<SendRouteSnapshot, maxSendsPerVocalist>, maxVocalists> sends {};
};

class AuxRouting final
{
public:
    class Read final
    {
    public:
        explicit Read(const AuxRouting& owner) noexcept : routing(owner)
        {
            routing.readers.fetch_add(1, std::memory_order_seq_cst);
            snapshot = routing.published.load(std::memory_order_seq_cst);
        }
        ~Read() { routing.readers.fetch_sub(1, std::memory_order_seq_cst); }
        Read(const Read&) = delete;
        const AuxRoutingSnapshot* operator->() const noexcept { return snapshot; }
        const AuxRoutingSnapshot& get() const noexcept { return *snapshot; }

    private:
        const AuxRouting& routing;
        const AuxRoutingSnapshot* snapshot = nullptr;
    };

    AuxRouting()
    {
        current = std::make_unique<AuxRoutingSnapshot>();
        published.store(current.get(), std::memory_order_release);
    }

    Read acquire() const noexcept { return Read(*this); }

    bool addAux(uint32_t& id)
    {
        if (current->auxCount >= AuxRoutingSnapshot::maxAuxReturns) return false;
        auto next = std::make_unique<AuxRoutingSnapshot>(*current);
        id = nextId++;
        next->auxReturns[next->auxCount++] = { id, 1.0f, false };
        publish(std::move(next));
        return true;
    }

    bool setSend(size_t vocalist, size_t slot, uint32_t auxId, float level, bool preFader)
    {
        if (vocalist >= AuxRoutingSnapshot::maxVocalists || slot >= AuxRoutingSnapshot::maxSendsPerVocalist)
            return false;
        const auto exists = std::any_of(current->auxReturns.begin(),
                                        current->auxReturns.begin() + static_cast<ptrdiff_t>(current->auxCount),
                                        [auxId] (const auto& aux) { return aux.id == auxId; });
        if (! exists) return false;
        auto next = std::make_unique<AuxRoutingSnapshot>(*current);
        next->sends[vocalist][slot] = { auxId, std::clamp(level, 0.0f, 1.0f), preFader };
        publish(std::move(next));
        return true;
    }

    void reclaim() noexcept
    {
        if (readers.load(std::memory_order_seq_cst) == 0) retired.clear();
    }

private:
    void publish(std::unique_ptr<AuxRoutingSnapshot> next)
    {
        retired.push_back(std::move(current));
        current = std::move(next);
        published.store(current.get(), std::memory_order_release);
    }

    uint32_t nextId = 1;
    std::unique_ptr<AuxRoutingSnapshot> current;
    std::vector<std::unique_ptr<AuxRoutingSnapshot>> retired;
    mutable std::atomic<const AuxRoutingSnapshot*> published { nullptr };
    mutable std::atomic<unsigned int> readers { 0 };
};
