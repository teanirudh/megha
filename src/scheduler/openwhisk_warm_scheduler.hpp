#pragma once

#include <algorithm>
#include <random>
#include <vector>

#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/scheduler.hpp"

namespace kumo
{

/**
 * OpenWhiskScheduler_v2-style:
 *
 *  - If any worker already has this function in warm_functions and
 *    sufficient capacity, pick that worker (first-fit).
 *  - Otherwise, pick a random worker with enough capacity.
 */
class OpenWhiskWarmScheduler : public Scheduler
{
  public:
    OpenWhiskWarmScheduler() : rng_(std::random_device{}()) {}

    explicit OpenWhiskWarmScheduler(std::uint64_t seed) : rng_(seed) {}

    std::string name() const override { return "openwhisk_warm"; }

    SchedulingDecision schedule(const Invocation &inv,
                                const PlatformState &state) override
    {
        const FunctionProfile *func = state.get_function(inv.function_id());
        if (!func)
        {
            return SchedulingDecision::fail("unknown function id");
        }

        const auto workers = state.workers();

        // 1. Prefer warm containers.
        for (WorkerId wid : workers)
        {
            const auto &wv = state.worker_view(wid);
            bool has_warm =
                std::find(wv.warm_functions.begin(), wv.warm_functions.end(),
                          inv.function_id()) != wv.warm_functions.end();
            if (has_warm && state.can_host(wid, *func))
            {
                return SchedulingDecision::ok(wid);
            }
        }

        // 2. Fallback: random healthy worker.
        std::vector<WorkerId> healthy;
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
