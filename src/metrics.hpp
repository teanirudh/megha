#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/logger.hpp"
#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "types.hpp"

namespace megha
{

struct TenantPairHash
{
    std::size_t
    operator()(const std::pair<TenantId, TenantId> &p) const noexcept
    {
        std::uint64_t a = p.first;
        std::uint64_t b = p.second;
        std::uint64_t x = (a << 32) ^ b;
        x ^= (x >> 33);
        x *= 0xff51afd7ed558ccdULL;
        x ^= (x >> 33);
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= (x >> 33);
        return static_cast<std::size_t>(x);
    }
};

class MetricsCollector
{
  public:
    void on_invocation_arrival(const Invocation &inv)
    {
        tenant_arrivals_[inv.tenant_id()] += 1;
    }

    void on_invocation_drop(const Invocation &inv, WorkerId wid, TimePoint now)
    {
        (void)wid;
        (void)now;
        tenant_drops_[inv.tenant_id()] += 1;
    }
    void on_invocation_start(const Invocation &inv, WorkerId worker_id,
                             const PlatformState &, TimePoint /*now*/)
    {
        TenantId t = inv.tenant_id();

        auto &tenant_counts = active_tenants_per_worker_[worker_id];

        for (const auto &kv : tenant_counts)
        {
            TenantId other = kv.first;
            if (other == t)
                continue;

            std::pair<TenantId, TenantId> key{t, other};
            colocations_[key] += 1;

            if (TraceLogger::enabled())
            {
                TraceLogger::log("[coloc]", " tenant_a=", t,
                                 " tenant_b=", other, " worker=", worker_id,
                                 " count=", colocations_[key]);
            }
        }

        tenant_counts[t] += 1;
    }

    void on_invocation_complete(const Invocation &inv, WorkerId worker_id,
                                const PlatformState & /*platform*/)
    {
        TenantId t = inv.tenant_id();

        auto worker_it = active_tenants_per_worker_.find(worker_id);
        if (worker_it == active_tenants_per_worker_.end())
            return;

        auto &tenant_counts = worker_it->second;
        auto t_it = tenant_counts.find(t);
        if (t_it == tenant_counts.end())
            return;

        if (t_it->second > 1)
            t_it->second -= 1;
        else
            tenant_counts.erase(t_it);
    }

    void on_invocation_finish(const Invocation &inv, TimePoint now)
    {
        double latency = now - inv.arrival_time();
        tenant_latencies_[inv.tenant_id()].push_back(latency);
    }

    void on_cold_start(const Invocation &inv, WorkerId /*worker_id*/,
                       const PlatformState & /*platform*/)
    {
        function_cold_starts_[inv.function_id()] += 1;
    }

    void on_warm_start(const Invocation &inv, WorkerId /*worker_id*/,
                       const PlatformState & /*platform*/)
    {
        function_warm_starts_[inv.function_id()] += 1;
    }

    double goodput(TimePoint simulation_time) const
    {
        if (simulation_time <= 0.0)
            return 0.0;
        return static_cast<double>(arrivals_total() - drops_total()) /
               simulation_time;
    }

    double tail_latency() const
    {
        std::vector<double> all;
        for (const auto &kv : tenant_latencies_)
            all.insert(all.end(), kv.second.begin(), kv.second.end());
        if (all.empty())
            return 0.0;
        return percentile_95(all);
    }

    double cold_start_rate() const
    {
        const std::uint64_t cold = cold_starts_total();
        const std::uint64_t warm = warm_starts_total();
        const std::uint64_t total = cold + warm;
        if (total == 0)
            return 0.0;
        return static_cast<double>(cold) / static_cast<double>(total);
    }

    double colocation_probability(const std::vector<TenantId> &victims,
                                  TenantId attacker) const
    {
        std::vector<TenantId> v =
            victims.empty() ? std::vector<TenantId>{1} : victims;
        std::uint64_t coloc_total = 0;
        std::uint64_t victim_arrivals = 0;
        for (TenantId tenant : v)
        {
            coloc_total += colocation_count(tenant, attacker);
            victim_arrivals += arrivals_for_tenant(tenant);
        }
        if (victim_arrivals == 0)
            return 0.0;
        return static_cast<double>(coloc_total) /
               static_cast<double>(victim_arrivals);
    }

  private:
    std::uint64_t arrivals_total() const
    {
        std::uint64_t s = 0;
        for (const auto &kv : tenant_arrivals_)
            s += kv.second;
        return s;
    }

    std::uint64_t drops_total() const
    {
        std::uint64_t s = 0;
        for (const auto &kv : tenant_drops_)
            s += kv.second;
        return s;
    }

    std::uint64_t arrivals_for_tenant(TenantId t) const
    {
        auto it = tenant_arrivals_.find(t);
        return it == tenant_arrivals_.end() ? 0 : it->second;
    }

    std::uint64_t colocation_count(TenantId src, TenantId other) const
    {
        std::pair<TenantId, TenantId> key{src, other};
        auto it = colocations_.find(key);
        return (it == colocations_.end()) ? 0 : it->second;
    }

    std::uint64_t cold_starts_total() const
    {
        std::uint64_t sum = 0;
        for (const auto &kv : function_cold_starts_)
            sum += kv.second;
        return sum;
    }

    std::uint64_t warm_starts_total() const
    {
        std::uint64_t sum = 0;
        for (const auto &kv : function_warm_starts_)
            sum += kv.second;
        return sum;
    }

    static double percentile_95(const std::vector<double> &v)
    {
        auto sorted = v;
        std::sort(sorted.begin(), sorted.end());
        std::size_t idx = static_cast<std::size_t>(0.95 * (sorted.size() - 1));
        return sorted[idx];
    }

    std::unordered_map<WorkerId, std::unordered_map<TenantId, std::uint64_t>>
        active_tenants_per_worker_;

    std::unordered_map<std::pair<TenantId, TenantId>, std::uint64_t,
                       TenantPairHash>
        colocations_;

    std::unordered_map<FunctionId, std::uint64_t> function_cold_starts_;
    std::unordered_map<FunctionId, std::uint64_t> function_warm_starts_;

    std::unordered_map<TenantId, std::uint64_t> tenant_arrivals_;
    std::unordered_map<TenantId, std::uint64_t> tenant_drops_;
    std::unordered_map<TenantId, std::vector<double>> tenant_latencies_;
};

} // namespace megha
