// src/scheduler/spread_scheduler.hpp
#pragma once

#include <vector>
#include <random>
#include <algorithm>

#include "scheduler/scheduler.hpp"
#include "model/platform_state.hpp"
#include "model/app.hpp"

namespace kumo {

/**
 * SpreadScheduler:
 *
 *  - Tries to *minimize tenant variety* on each worker.
 *  - Prefer workers that already host this tenant (same-tenant affinity).
 *  - Among eligible workers, choose the one with the smallest number
 *    of distinct tenants_present.
 *  - Break ties randomly (optional, but avoids bias).
 *
 * Intuition: placing a tenant where it already runs avoids increasing
 * the number of co-located tenants and can reduce co-location risk.
 */
class SpreadScheduler : public Scheduler {
public:
    SpreadScheduler()
        : rng_(std::random_device{}())
    {}

    explicit SpreadScheduler(std::uint64_t seed)
        : rng_(seed)
    {}

    std::string name() const override {
        return "spread";
    }

    SchedulingDecision schedule(const Invocation& inv,
                                const PlatformState& state) override
    {
        const FunctionProfile* func = state.get_function(inv.function_id());
        if (!func) {
            return SchedulingDecision::fail("unknown function id");
        }

        struct Candidate {
            WorkerId    id;
            std::size_t tenant_variety;
            bool        has_same_tenant;
        };

        std::vector<Candidate> candidates;
        candidates.reserve(state.num_workers());

        // Gather capacity-eligible workers and their tenant variety.
        for (WorkerId wid : state.workers()) {
            if (!state.can_host(wid, *func)) {
                continue;
            }

            const auto& wv = state.worker_view(wid);

            std::size_t variety = wv.tenants_present.size();
            bool has_same = std::find(wv.tenants_present.begin(),
                                      wv.tenants_present.end(),
                                      inv.tenant_id()) != wv.tenants_present.end();

            candidates.push_back(Candidate{
                .id             = wid,
                .tenant_variety = variety,
                .has_same_tenant = has_same
            });
        }

        if (candidates.empty()) {
            return SchedulingDecision::fail("no worker has enough capacity");
        }

        // First, try to find workers that already host this tenant.
        std::vector<Candidate> same_tenant;
        same_tenant.reserve(candidates.size());
        for (const auto& c : candidates) {
            if (c.has_same_tenant) {
                same_tenant.push_back(c);
            }
        }

        const auto& pool = same_tenant.empty() ? candidates : same_tenant;

        // Find minimal tenant variety in the chosen pool.
        std::size_t min_variety = pool.front().tenant_variety;
        for (const auto& c : pool) {
            if (c.tenant_variety < min_variety) {
                min_variety = c.tenant_variety;
            }
        }

        // Collect those with that minimal variety.
        std::vector<WorkerId> best;
        for (const auto& c : pool) {
            if (c.tenant_variety == min_variety) {
                best.push_back(c.id);
            }
        }

        // Tie-break randomly among best candidates.
        WorkerId chosen;
        if (best.size() == 1) {
            chosen = best[0];
        } else {
            std::uniform_int_distribution<std::size_t> dist(0, best.size() - 1);
            chosen = best[dist(rng_)];
        }

        return SchedulingDecision::ok(chosen);
    }

private:
    std::mt19937_64 rng_;
};

} // namespace kumo
