#pragma once

#include <random>
#include <unordered_map>
#include <vector>

#include "../model/app.hpp"
#include "../model/platform_state.hpp"
#include "scheduler.hpp"

namespace megha
{

/**
 * HelperScheduler (Cloud Run–like):
 *
 *  - For each function, track an "invocation frequency" per worker.
 *  - If a low-frequency function: stick to an existing worker.
 *  - When the counter for (func, worker) would exceed threshold,
 *    "scale out" to another random worker (not used yet by that func).
 */
class HelperScheduler : public Scheduler
{
  public:
    HelperScheduler() : rng_(std::random_device{}()) {}

    explicit HelperScheduler(std::uint64_t seed,
                             int threshold = default_threshold)
        : rng_(seed), threshold_(threshold)
    {
    }

    std::string name() const override { return "helper"; }

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

        // Shortcut: list all healthy workers.
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

        auto &freq_map = inv_freq_[inv.function_id()]; // worker -> counter

        // Case 1: function never been scheduled:
        // pick random healthy worker
        if (freq_map.empty())
        {
            WorkerId wid = pick_random(healthy);
            maintain_inv_freq(inv.function_id(), wid);
            return SchedulingDecision::ok(wid);
        }

        // Case 2: function has some workers already:
        // try to stay on one that doesn't exceed threshold.
        std::vector<WorkerId> existing_workers;
        existing_workers.reserve(freq_map.size());
        for (const auto &kv : freq_map)
        {
            WorkerId wid = kv.first;
            if (state.can_host(wid, *func))
            {
                existing_workers.push_back(wid);
            }
        }

        if (!existing_workers.empty())
        {
            // Randomly pick among existing hosts and
            // check if it's still "low freq".
            WorkerId chosen = pick_random(existing_workers);
            if (maintain_inv_freq(inv.function_id(), chosen))
            {
                return SchedulingDecision::ok(chosen);
            }
            // else: fall through to "helper" case.
        }

        // Case 3: need a helper. Prefer healthy workers that are not
        // already in freq_map (new hosts).
        std::vector<WorkerId> new_hosts;
        new_hosts.reserve(healthy.size());
        for (WorkerId wid : healthy)
        {
            if (freq_map.find(wid) == freq_map.end())
            {
                new_hosts.push_back(wid);
            }
        }

        if (new_hosts.empty())
        {
            // Everyone is already a host; just pick a healthy worker.
            WorkerId wid = pick_random(healthy);
            maintain_inv_freq(inv.function_id(), wid);
            return SchedulingDecision::ok(wid);
        }
        else
        {
            WorkerId wid = pick_random(new_hosts);
            maintain_inv_freq(inv.function_id(), wid);
            return SchedulingDecision::ok(wid);
        }
    }

  private:
    static constexpr int default_threshold = 32; // configurable heuristic

    // inv_freq_[func][worker] -> "recent invocation frequency"
    std::unordered_map<FunctionId, std::unordered_map<WorkerId, int>> inv_freq_;

    std::mt19937_64 rng_;
    int threshold_ = default_threshold;

    WorkerId pick_random(const std::vector<WorkerId> &ws)
    {
        std::uniform_int_distribution<std::size_t> dist(0, ws.size() - 1);
        return ws[dist(rng_)];
    }

    // Return true if still "low frequency"
    // false if threshold would be exceeded.
    bool maintain_inv_freq(FunctionId func, WorkerId wid)
    {
        auto &freq_map = inv_freq_[func];

        // If this worker is new for this function,
        // insert with a small starting value.
        auto it = freq_map.find(wid);
        if (it == freq_map.end())
        {
            freq_map[wid] = 1;

            // Small decay of other workers for this function (not global).
            decay_func_except(freq_map, wid);
            return true;
        }

        // Existing worker: check threshold.
        if (it->second + 1 > threshold_)
        {
            return false; // force scale-out
        }

        // Increment chosen worker, decay others for this function only.
        it->second += 1;
        decay_func_except(freq_map, wid);
        return true;
    }

    // Decay counters only within a single function's map.
    // (Never touch other functions' maps.)
    static void decay_func_except(std::unordered_map<WorkerId, int> &freq_map,
                                  WorkerId keep)
    {
        for (auto iter = freq_map.begin(); iter != freq_map.end();)
        {
            if (iter->first == keep)
            {
                ++iter;
                continue;
            }
            iter->second -= 1;
            if (iter->second <= 0)
            {
                iter = freq_map.erase(iter);
            }
            else
            {
                ++iter;
            }
        }
    }
};

} // namespace megha
