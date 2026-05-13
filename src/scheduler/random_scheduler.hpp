// src/scheduler/random_scheduler.hpp
#pragma once

#include <random>
#include <vector>

#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/scheduler.hpp"

namespace kumo
{

/**
 * A simple capacity-aware random scheduler.
 *
 * - Looks up the function profile for the invocation.
 * - Builds a list of workers that have enough remaining capacity.
 * - Picks one uniformly at random.
 */
class RandomScheduler : public Scheduler
{
  public:
    RandomScheduler() : rng_(std::random_device{}()) {}

    explicit RandomScheduler(std::uint64_t seed) : rng_(seed) {}

    std::string name() const override { return "random"; }

    SchedulingDecision schedule(const Invocation &inv,
                                const PlatformState &state) override
    {
        // Look up the function profile to know resource requirements.
        const FunctionProfile *func = state.get_function(inv.function_id());
        if (!func)
        {
            return SchedulingDecision::fail("unknown function id");
        }

        // Collect workers that can host this function.
        std::vector<WorkerId> candidates;
        candidates.reserve(state.num_workers());
        for (WorkerId wid : state.workers())
        {
            if (state.can_host(wid, *func))
            {
                candidates.push_back(wid);
            }
        }

        if (candidates.empty())
        {
            return SchedulingDecision::fail("no worker has enough capacity");
        }

        // Uniform random pick among candidates.
        std::uniform_int_distribution<std::size_t> dist(0,
                                                        candidates.size() - 1);
        WorkerId chosen = candidates[dist(rng_)];
        return SchedulingDecision::ok(chosen);
    }

  private:
    std::mt19937_64 rng_;
};

} // namespace kumo
