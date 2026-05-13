#pragma once

#include <functional>
#include <random>
#include <string>
#include <vector>

#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/scheduler.hpp"

namespace kumo
{

/**
 * PASchScheduler:
 *
 *  - Builds a simple hash ring over workers.
 *  - Uses the first package name of the function as the key.
 *  - Maps that key to a worker (consistent-ish hashing).
 *  - If that worker lacks capacity, falls back to random healthy.
 */
class PASchScheduler : public Scheduler
{
  public:
    PASchScheduler() : rng_(std::random_device{}()) {}

    explicit PASchScheduler(std::uint64_t seed) : rng_(seed) {}

    std::string name() const override { return "pasch"; }

    SchedulingDecision schedule(const Invocation &inv,
                                const PlatformState &state) override
    {
        const FunctionProfile *func = state.get_function(inv.function_id());
        if (!func)
        {
            return SchedulingDecision::fail("unknown function id");
        }

        const auto workers = state.workers();
        if (workers.empty())
        {
            return SchedulingDecision::fail("no workers");
        }

        // 1. Build "hash ring": here, just a vector of worker IDs.
        // (We could add virtual nodes, but this is enough for now.)
        std::vector<WorkerId> ring = workers;

        // 2. Determine key from the first package (or fallback to function id).
        std::string key;
        if (!func->packages.empty())
        {
            key = func->packages.front();
        }
        else
        {
            key = "func-" + std::to_string(func->id);
        }

        std::size_t h = std::hash<std::string>{}(key);
        std::size_t idx = h % ring.size();
        WorkerId home = ring[idx];

        // 3. If home has capacity, use it.
        if (state.can_host(home, *func))
        {
            return SchedulingDecision::ok(home);
        }

        // 4. Else, fall back to random healthy worker.
        std::vector<WorkerId> healthy;
        healthy.reserve(workers.size());
        for (WorkerId wid : workers)
        {
            if (state.can_host(wid, *func))
            {
                healthy.push_back(wid);
            }
        }

        if (healthy.empty())
        {
            return SchedulingDecision::fail("no worker has enough capacity");
        }

        std::uniform_int_distribution<std::size_t> dist(0, healthy.size() - 1);
        WorkerId chosen = healthy[dist(rng_)];
        return SchedulingDecision::ok(chosen);
    }

  private:
    std::mt19937_64 rng_;
};

} // namespace kumo
