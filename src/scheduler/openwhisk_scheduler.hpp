#pragma once

#include <vector>
#include <random>
#include <numeric>   // std::gcd
#include <string>

#include "scheduler/scheduler.hpp"
#include "model/platform_state.hpp"
#include "model/app.hpp"

namespace kumo {

/**
 * OpenWhisk-style scheduler:
 *
 *  - Picks a "home" invoker using a hash of the function id.
 *  - Computes a step size that is coprime with the number of workers.
 *  - Probes workers in a ring: home, home+step, home+2*step, ...
 *  - If none have capacity, falls back to a random healthy worker.
 */
class OpenWhiskScheduler : public Scheduler {
public:
    OpenWhiskScheduler()
        : rng_(std::random_device{}())
    {}

    explicit OpenWhiskScheduler(std::uint64_t seed)
        : rng_(seed)
    {}

    std::string name() const override {
        return "openwhisk";
    }

    SchedulingDecision schedule(const Invocation& inv,
                                const PlatformState& state) override
    {
        const FunctionProfile* func = state.get_function(inv.function_id());
        if (!func) {
            return SchedulingDecision::fail("unknown function id");
        }

        const std::size_t num = state.num_workers();
        if (num == 0) {
            return SchedulingDecision::fail("no workers");
        }

        const auto workers = state.workers();

        // Hash function id to a 64-bit value.
        std::size_t h = std::hash<std::string>{}(
            std::to_string(inv.function_id()));

        std::size_t home = h % num;

        // Compute pairwise-coprime step candidates relative to num.
        std::vector<std::size_t> steps = pairwise_coprime_numbers(num);
        std::size_t step = 1;
        if (!steps.empty()) {
            step = steps[h % steps.size()];
        }

        // Probe in a ring starting from home using step.
        std::size_t idx = home;
        bool first = true;
        do {
            WorkerId wid = workers[idx];
            if (state.can_host(wid, *func)) {
                return SchedulingDecision::ok(wid);
            }

            idx = (idx + step) % num;
            first = false;
        } while (idx != home || first);

        // Fallback: random healthy worker if ring search failed.
        std::vector<WorkerId> healthy;
        healthy.reserve(num);
        for (WorkerId wid : workers) {
            if (state.can_host(wid, *func)) {
                healthy.push_back(wid);
            }
        }

        if (healthy.empty()) {
            return SchedulingDecision::fail("no worker has enough capacity");
        }

        std::uniform_int_distribution<std::size_t> dist(0, healthy.size() - 1);
        WorkerId chosen = healthy[dist(rng_)];
        return SchedulingDecision::ok(chosen);
    }

private:
    // Compute a set of pairwise-coprime step sizes in [1, n-1].
    static std::vector<std::size_t> pairwise_coprime_numbers(std::size_t n) {
        std::vector<std::size_t> result;
        for (std::size_t i = 1; i < n; ++i) {
            if (std::gcd(i, n) != 1) continue;

            bool ok = true;
            for (auto s : result) {
                if (std::gcd(s, i) != 1) {
                    ok = false;
                    break;
                }
            }
            if (ok) result.push_back(i);
        }
        if (result.empty()) {
            result.push_back(1);
        }
        return result;
    }

    std::mt19937_64 rng_;
};

} // namespace kumo
