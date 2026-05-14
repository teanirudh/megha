#pragma once

#include <algorithm>
#include <limits>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../model/app.hpp"
#include "../model/platform_state.hpp"
#include "scheduler.hpp"

namespace megha
{

/**
 * Guardian: cache-aware scheduler
 *
 * Each worker maintains EMA-smoothed C_thrash and C_probe from per-cycle
 * deltas (LLC misses, instructions, cycles) aggregated over busy
 * containers using their MicroarchProfile.
 *
 * Candidates are probe-hot if intrinsic C_probe exceeds kProbeHotThreshold.
 * Workers that ever receive a probe-hot placement stay tainted: hot picks
 * prefer existing taint (max observed C_probe), else seed taint on argmin
 * C_thrash; cool picks are uniform random over non-tainted workers, or
 * over all candidates if every worker is tainted.
 */
class GuardianScheduler : public Scheduler
{
  public:
    explicit GuardianScheduler(std::uint64_t seed) : rng_(seed) {}

    std::string name() const override { return "guardian"; }

    SchedulingDecision schedule(const Invocation &inv,
                                const PlatformState &state) override
    {
        const FunctionProfile *func = state.get_function(inv.function_id());
        if (!func)
        {
            return SchedulingDecision::fail("unknown function id");
        }

        const TimePoint now = inv.arrival_time();

        for (WorkerId wid : state.workers())
        {
            advance_counters(wid, state, now);
            update_ema(wid);
        }

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

        const MicroarchProfile &m = func->microarch;
        const bool cand_hot = intrinsic_probe(m) > kProbeHotThreshold;

        WorkerId chosen;
        if (cand_hot)
        {
            chosen = pick_hot(candidates);
            tainted_.insert(chosen);
        }
        else
        {
            chosen = pick_cool(candidates);
        }
        return SchedulingDecision::ok(chosen);
    }

  private:
    WorkerId pick_hot(const std::vector<WorkerId> &candidates)
    {
        std::vector<WorkerId> hot;
        hot.reserve(candidates.size());
        for (WorkerId wid : candidates)
        {
            if (tainted_.count(wid))
            {
                hot.push_back(wid);
            }
        }

        if (!hot.empty())
        {
            return argmax_random(hot, [&](WorkerId w)
                                 { return worker_state_[w].c_probe; });
        }

        return argmin_random(candidates, [&](WorkerId w)
                             { return worker_state_[w].c_thrash; });
    }

    WorkerId pick_cool(const std::vector<WorkerId> &candidates)
    {
        std::vector<WorkerId> cool;
        cool.reserve(candidates.size());
        for (WorkerId wid : candidates)
        {
            if (!tainted_.count(wid))
            {
                cool.push_back(wid);
            }
        }
        const std::vector<WorkerId> &pool = cool.empty() ? candidates : cool;
        std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
        return pool[pick(rng_)];
    }

    template <typename Score>
    WorkerId argmin_random(const std::vector<WorkerId> &pool, Score score)
    {
        double best = std::numeric_limits<double>::infinity();
        std::vector<WorkerId> tied;
        tied.reserve(pool.size());
        for (WorkerId wid : pool)
        {
            const double s = score(wid);
            if (s < best)
            {
                best = s;
                tied.clear();
                tied.push_back(wid);
            }
            else if (s == best)
            {
                tied.push_back(wid);
            }
        }
        std::uniform_int_distribution<std::size_t> dist(0, tied.size() - 1);
        return tied[dist(rng_)];
    }

    template <typename Score>
    WorkerId argmax_random(const std::vector<WorkerId> &pool, Score score)
    {
        double best = -std::numeric_limits<double>::infinity();
        std::vector<WorkerId> tied;
        tied.reserve(pool.size());
        for (WorkerId wid : pool)
        {
            const double s = score(wid);
            if (s > best)
            {
                best = s;
                tied.clear();
                tied.push_back(wid);
            }
            else if (s == best)
            {
                tied.push_back(wid);
            }
        }
        std::uniform_int_distribution<std::size_t> dist(0, tied.size() - 1);
        return tied[dist(rng_)];
    }

    struct WorkerCounters
    {
        double cycles = 0.0;
        double instructions = 0.0;
        double llc_loads = 0.0;
        double llc_stores = 0.0;
        double llc_load_misses = 0.0;
        double llc_store_misses = 0.0;
    };

    struct WorkerState
    {
        WorkerCounters cumulative{};
        WorkerCounters prev_sample{};
        TimePoint last_sample_time = 0.0;
        double c_thrash = 0.0;
        double c_probe = 0.0;
    };

    static constexpr double kEmaWeight = 0.2;
    static constexpr double kCyclesPerTimeUnit = 1e9;
    static constexpr double kProbeHotThreshold = 10.0;

    WorkerState &ensure_state(WorkerId wid) { return worker_state_[wid]; }

    void advance_counters(WorkerId wid, const PlatformState &state,
                          TimePoint now)
    {
        WorkerState &ws = ensure_state(wid);
        const TimePoint t0 = ws.last_sample_time;
        const double dt = now - t0;

        if (dt <= 0.0)
        {
            return;
        }

        const auto &w = state.worker_view(wid);
        bool any_busy = false;
        for (const auto &c : w.containers)
        {
            if (c.busy)
            {
                any_busy = true;
                break;
            }
        }

        if (any_busy)
        {
            const double dcyc = dt * kCyclesPerTimeUnit;
            for (const auto &c : w.containers)
            {
                if (!c.busy)
                {
                    continue;
                }
                const FunctionProfile *fp = state.get_function(c.function_id);
                if (!fp)
                {
                    continue;
                }
                const MicroarchProfile &m = fp->microarch;
                ws.cumulative.cycles += dcyc;
                ws.cumulative.instructions += dcyc * m.instructions_per_cycle;
                ws.cumulative.llc_loads += dcyc * m.llc_loads_per_cycle;
                ws.cumulative.llc_stores += dcyc * m.llc_stores_per_cycle;
                ws.cumulative.llc_load_misses +=
                    dcyc * m.llc_load_misses_per_cycle;
                ws.cumulative.llc_store_misses +=
                    dcyc * m.llc_store_misses_per_cycle;
            }
        }

        ws.last_sample_time = now;
    }

    void update_ema(WorkerId wid)
    {
        WorkerState &ws = ensure_state(wid);

        const double d_cyc = ws.cumulative.cycles - ws.prev_sample.cycles;
        if (d_cyc <= 0.0)
        {
            return;
        }

        const double d_lm =
            ws.cumulative.llc_load_misses - ws.prev_sample.llc_load_misses;
        const double d_sm =
            ws.cumulative.llc_store_misses - ws.prev_sample.llc_store_misses;
        const double d_instr =
            ws.cumulative.instructions - ws.prev_sample.instructions;

        const double raw_thrash = (d_lm + d_sm) / d_cyc;
        const double raw_probe =
            (d_lm / (d_sm + 1.0)) * (d_cyc / std::max(d_instr, 1.0));

        ws.c_thrash =
            kEmaWeight * raw_thrash + (1.0 - kEmaWeight) * ws.c_thrash;
        ws.c_probe = kEmaWeight * raw_probe + (1.0 - kEmaWeight) * ws.c_probe;
        ws.prev_sample = ws.cumulative;
    }

    static double intrinsic_probe(const MicroarchProfile &m)
    {
        const double sm = std::max(m.llc_store_misses_per_cycle, 1e-12);
        const double ipc = std::max(m.instructions_per_cycle, 1e-12);
        return (m.llc_load_misses_per_cycle / sm) * (1.0 / ipc);
    }

    std::unordered_map<WorkerId, WorkerState> worker_state_;
    std::unordered_set<WorkerId> tainted_;
    std::mt19937_64 rng_;
};

} // namespace megha
