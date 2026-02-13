// src/metrics/metrics.hpp
#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>
#include <cstdint>
#include <algorithm>
#include <deque>

#include "common/types.hpp"
#include "model/platform_state.hpp"
#include "model/app.hpp"
#include "core/trace_logger.hpp"

namespace kumo {

/**
 * Hash for std::pair<TenantId, TenantId> to use in unordered_map.
 * We normalize the pair (min, max) so (A,B) and (B,A) are identical.
 */
struct TenantPairHash {
    std::size_t operator()(const std::pair<TenantId, TenantId>& p) const noexcept {
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

/**
 * MetricsCollector tracks:
 *  - per-worker invocation counts,
 *  - per-tenant total invocations,
 *  - co-location counts between tenants sharing a worker,
 *  - per-function cold / warm starts (NEW).
 *
 * Engine (or scenario driver) should call:
 *  - on_invocation_start(inv, worker, platform)
 *  - on_invocation_complete(inv, worker, platform)
 *  - on_cold_start(inv, worker, platform)   // NEW
 *  - on_warm_start(inv, worker, platform)   // NEW
 */
class MetricsCollector {
public:
    // Called when an invocation arrives into the system (before placement).
    void on_invocation_arrival(const Invocation& inv) {
        tenant_arrivals_[inv.tenant_id()] += 1;
    }

    // Called when an invocation is dropped (e.g., queue full).
    void on_invocation_drop(const Invocation& inv, WorkerId wid, TimePoint now) {
        (void)wid; (void)now;
        tenant_drops_[inv.tenant_id()] += 1;
    }

    // Called when an invocation actually begins execution (after queueing).
    void on_invocation_execute(const Invocation& inv, WorkerId wid, TimePoint now) {
        (void)wid;
        inv_exec_start_[inv.id()] = now;
    }

    std::uint64_t arrivals_total() const {
        std::uint64_t s = 0;
        for (auto& kv : tenant_arrivals_) s += kv.second;
        return s;
    }

    std::uint64_t drops_total() const {
        std::uint64_t s = 0;
        for (auto& kv : tenant_drops_) s += kv.second;
        return s;
    }

    std::uint64_t arrivals_for_tenant_or_zero(TenantId t) const { return arrivals_for_tenant(t); }
    std::uint64_t drops_for_tenant_or_zero(TenantId t) const { return drops_for_tenant(t); }

    /// Called when an invocation starts running on a worker (placement).
    void on_invocation_start(const Invocation& inv,
                             WorkerId worker_id,
                             const PlatformState&,
                             TimePoint now)
    {
        TenantId t = inv.tenant_id();

        // Per-worker and per-tenant counts.
        worker_invocations_[worker_id] += 1;
        tenant_invocations_[t]         += 1;

        auto& tenant_counts = active_tenants_per_worker_[worker_id];

        // Before we add this tenant, any *existing* tenant on this worker
        // is now co-located with t.
        for (const auto& kv : tenant_counts) {
            TenantId other = kv.first;
            if (other == t) continue;

            std::pair<TenantId, TenantId> key{t, other};
            
            bool first = (colocations_.find(key) == colocations_.end());
            
            colocations_[key] += 1;

            // if this is the first co-location for this pair, record time
            if (first) {
                first_coloc_time_[key] = now;
            }

            if (TraceLogger::enabled()) {
                TraceLogger::log("[coloc]",
                                 " tenant_a=", t,
                                 " tenant_b=", other,
                                 " worker=", worker_id,
                                 " count=", colocations_[key]);
            }
        }

        // Mark tenant as active on this worker.
        tenant_counts[t] += 1;
    }

    /// Called when an invocation completes on a worker.
    void on_invocation_complete(const Invocation& inv,
                                WorkerId worker_id,
                                const PlatformState& /*platform*/)
    {
        TenantId t = inv.tenant_id();

        auto worker_it = active_tenants_per_worker_.find(worker_id);
        if (worker_it == active_tenants_per_worker_.end()) {
            return;
        }

        auto& tenant_counts = worker_it->second;
        auto t_it = tenant_counts.find(t);
        if (t_it == tenant_counts.end()) {
            return;
        }

        if (t_it->second > 1) {
            t_it->second -= 1;
        } else {
            tenant_counts.erase(t_it);
        }

        // Latency accounting (finish - arrival)
        auto it = inv_exec_start_.find(inv.id());
        // If we have an exec start time, use it; otherwise still compute end-to-end.
        // Note: we do not know 'now' here, so we compute latency in Engine and call a new hook OR
        // store it via a separate on_invocation_finish(). Easiest: add a finish hook.
    }

    void on_invocation_finish(const Invocation& inv, TimePoint now) {
        // End-to-end latency includes waiting + cold/warm delay + service time
        double latency = now - inv.arrival_time();
        tenant_latencies_[inv.tenant_id()].push_back(latency);
        inv_exec_start_.erase(inv.id());
    }


    /// NEW: record a cold start for this invocation's function.
    void on_cold_start(const Invocation& inv,
                       WorkerId /*worker_id*/,
                       const PlatformState& /*platform*/)
    {
        function_cold_starts_[inv.function_id()] += 1;
    }

    /// NEW: record a warm start for this invocation's function.
    void on_warm_start(const Invocation& inv,
                       WorkerId /*worker_id*/,
                       const PlatformState& /*platform*/)
    {
        function_warm_starts_[inv.function_id()] += 1;
    }

    //
    // ---- Read-side API ----
    //

    /// Total invocations started on each worker.
    const std::unordered_map<WorkerId, std::uint64_t>&
    worker_invocations() const noexcept
    {
        return worker_invocations_;
    }

    /// Total invocations per tenant.
    const std::unordered_map<TenantId, std::uint64_t>&
    tenant_invocations() const noexcept
    {
        return tenant_invocations_;
    }

    /// Co-location counts between tenant pairs (canonicalized pair).
    const std::unordered_map<std::pair<TenantId, TenantId>,
                             std::uint64_t,
                             TenantPairHash>&
    colocations() const noexcept
    {
        return colocations_;
    }

    // NEW: directional colocation count:
    // how many invocations from 'src' started on a worker that also had 'other'
    std::uint64_t colocation_count(TenantId src, TenantId other) const {
        std::pair<TenantId, TenantId> key{src, other};   // ORDER MATTERS NOW
        auto it = colocations_.find(key);
        return (it == colocations_.end()) ? 0 : it->second;
    }

    /// NEW: per-function cold start counts.
    const std::unordered_map<FunctionId, std::uint64_t>&
    function_cold_starts() const noexcept
    {
        return function_cold_starts_;
    }

    /// NEW: per-function warm start counts.
    const std::unordered_map<FunctionId, std::uint64_t>&
    function_warm_starts() const noexcept
    {
        return function_warm_starts_;
    }

    /// NEW: convenience accessors for a single function.
    std::uint64_t cold_starts_for(FunctionId f) const noexcept {
        auto it = function_cold_starts_.find(f);
        return (it == function_cold_starts_.end()) ? 0 : it->second;
    }

    std::uint64_t warm_starts_for(FunctionId f) const noexcept {
        auto it = function_warm_starts_.find(f);
        return (it == function_warm_starts_.end()) ? 0 : it->second;
    }

    std::uint64_t invocations_for_tenant(TenantId t) const {
        auto it = tenant_invocations_.find(t);
        return (it == tenant_invocations_.end()) ? 0 : it->second;
    }

    // NEW: directional first-colocation time:
    // time of first invocation from 'src' that co-locates with 'other'
    double first_colocation_time(TenantId src, TenantId other) const {
        std::pair<TenantId, TenantId> key{src, other};
        auto it = first_coloc_time_.find(key);
        return (it == first_coloc_time_.end()) ? -1.0 : it->second;
    }

    std::uint64_t arrivals_for_tenant(TenantId t) const {
        auto it = tenant_arrivals_.find(t);
        return it == tenant_arrivals_.end() ? 0 : it->second;
    }

    std::uint64_t drops_for_tenant(TenantId t) const {
        auto it = tenant_drops_.find(t);
        return it == tenant_drops_.end() ? 0 : it->second;
    }

    double p95_latency_for_tenant(TenantId t) const {
        auto it = tenant_latencies_.find(t);
        if (it == tenant_latencies_.end() || it->second.empty()) return 0.0;
        auto v = it->second; // copy
        std::sort(v.begin(), v.end());
        std::size_t idx = static_cast<std::size_t>(0.95 * (v.size() - 1));
        return v[idx];
    }

    double mean_latency_for_tenant(TenantId t) const {
        auto it = tenant_latencies_.find(t);
        if (it == tenant_latencies_.end() || it->second.empty()) return 0.0;
        double sum = 0.0;
        for (double x : it->second) sum += x;
        return sum / static_cast<double>(it->second.size());
    }


private:
    static std::pair<TenantId, TenantId> canonical_pair(TenantId a, TenantId b) {
        if (a < b) return {a, b};
        return {b, a};
    }

private:
    // How many invocations have started on each worker.
    std::unordered_map<WorkerId, std::uint64_t> worker_invocations_;

    // How many invocations each tenant has started in total.
    std::unordered_map<TenantId, std::uint64_t> tenant_invocations_;

    // For each worker, how many *active* invocations per tenant exist.
    // (Used internally to reason about co-location.)
    std::unordered_map<WorkerId,
        std::unordered_map<TenantId, std::uint64_t>> active_tenants_per_worker_;

    // Co-location counts between tenant pairs.
    std::unordered_map<std::pair<TenantId, TenantId>,
                       std::uint64_t,
                       TenantPairHash> colocations_;

    // per-function counts of cold vs warm starts.
    std::unordered_map<FunctionId, std::uint64_t> function_cold_starts_;
    std::unordered_map<FunctionId, std::uint64_t> function_warm_starts_;

    std::unordered_map<std::pair<TenantId, TenantId>, double, TenantPairHash> first_coloc_time_;

    std::unordered_map<TenantId, std::uint64_t> tenant_arrivals_;
    std::unordered_map<TenantId, std::uint64_t> tenant_drops_;
    std::unordered_map<TenantId, std::vector<double>> tenant_latencies_;
    std::unordered_map<InvocationId, TimePoint> inv_exec_start_;
};

} // namespace kumo
