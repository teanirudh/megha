#pragma once

#include <memory>
#include <random>
#include <unordered_set>
#include <vector>

#include "../model/app.hpp"
#include "../types.hpp"
#include "workload.hpp"

namespace megha
{

/**
 * Configuration for an "attacker" tenant.
 *
 * - attacker_tenant / attacker_function: ids used for attack invocations
 * - victims: victim tenants to follow when injecting attacks
 * - attack_intensity: expected attack invocations per victim invocation
 *   (1.0 ~ one attack per victim, 2.0 ~ two, 0.5 ~ one every two victims)
 * - total_invocations: optional cap on attacker-side volume (if used)
 * - pattern: attack-side pattern label (e.g. "poisson")
 */
struct AttackConfig
{
    TenantId attacker_tenant = 0;
    FunctionId attacker_function = 0;

    std::vector<TenantId> victims;
    double attack_intensity = 1.0;       // ratio
    std::uint64_t total_invocations = 1; // optional cap

    std::string pattern = "poisson"; // attack pattern type
};

/**
 * AttackWorkload:
 *
 * Wraps a "baseline" workload (victims) and injects attacker invocations
 * that track the baseline's victim invocations.
 *
 * At each next_batch(now):
 *  - Ask the baseline workload for its batch.
 *  - For each victim invocation in that batch:
 *      * Generate ~attack_intensity attacker invocations that arrive
 *        at the same time (or very slightly later, if desired).
 *  - Return baseline + attack invocations.
 *
 * This provides a clean analogue to your old Attacker behavior, but
 * now decoupled from the scheduler and engine.
 */
class AttackWorkload : public Workload
{
  public:
    AttackWorkload(std::unique_ptr<Workload> baseline, AttackConfig cfg,
                   std::uint64_t seed = 0)
        : baseline_(std::move(baseline)), cfg_(std::move(cfg)),
          rng_(seed ? seed : std::random_device{}())
    {
        victim_set_.reserve(cfg_.victims.size());
        for (auto t : cfg_.victims)
        {
            victim_set_.insert(t);
        }
    }

    std::vector<Invocation> next_batch(TimePoint now) override
    {
        std::vector<Invocation> batch;
        if (!baseline_ || !baseline_->has_more())
        {
            return batch;
        }

        // 1. Get the baseline (victim) batch.
        std::vector<Invocation> victims = baseline_->next_batch(now);
        if (victims.empty())
        {
            return victims; // nothing to do; also no attack this step
        }

        // 2. Prepare attacker invocations.
        std::vector<Invocation> attacks;
        attacks.reserve(static_cast<std::size_t>(
            victims.size() * (cfg_.attack_intensity + 1.0)));

        // We implement attack_intensity using a Poisson-like process:
        // For each victim invocation:
        //  - Let lambda = attack_intensity.
        //  - Generate k ~ Poisson(lambda) approx by:
        //      * integer part floor(lambda),
        //      * plus 1 extra attack with probability (lambda - floor(lambda)).
        std::uniform_real_distribution<double> uni01(0.0, 1.0);

        for (const auto &inv : victims)
        {
            if (!is_victim(inv.tenant_id()))
            {
                continue;
            }

            double lambda = cfg_.attack_intensity;
            if (lambda <= 0.0)
                continue;

            int base_count = static_cast<int>(lambda);
            double frac = lambda - base_count;

            int k = base_count;
            if (uni01(rng_) < frac)
            {
                k += 1;
            }

            if (cfg_.total_invocations > 100)
                k *= cfg_.total_invocations / 100;
            for (int i = 0; i < k; ++i)
            {
                InvocationId id = next_attack_id_++;
                // Attacks arrive at same time as victim for now.
                TimePoint arrival = inv.arrival_time();
                Duration service_time = default_attack_service_time_;

                // We also encode the victim tenant id in the label
                // for debugging / offline analysis.
                std::string label =
                    "attack_on_tenant_" + std::to_string(inv.tenant_id());

                attacks.emplace_back(id, cfg_.attacker_function,
                                     cfg_.attacker_tenant, arrival,
                                     service_time, label);
            }
        }

        // 3. Merge victim and attacker batches.
        batch.reserve(victims.size() + attacks.size());
        batch.insert(batch.end(), std::make_move_iterator(victims.begin()),
                     std::make_move_iterator(victims.end()));
        batch.insert(batch.end(), std::make_move_iterator(attacks.begin()),
                     std::make_move_iterator(attacks.end()));

        return batch;
    }

    bool has_more() const noexcept override
    {
        return baseline_ && baseline_->has_more();
    }

    // Optionally override the default attack service time.
    void set_attack_service_time(Duration d) noexcept
    {
        default_attack_service_time_ = d;
    }

  private:
    bool is_victim(TenantId t) const noexcept
    {
        return victim_set_.find(t) != victim_set_.end();
    }

  private:
    std::unique_ptr<Workload> baseline_;
    AttackConfig cfg_;

    // Start attack IDs from a large offset so they do not collide
    // with IDs produced by typical workloads (which usually start at 1).
    static constexpr InvocationId kAttackIdBase = 1'000'000'000ULL;
    InvocationId next_attack_id_ = kAttackIdBase;
    Duration default_attack_service_time_ = 50.0;

    std::mt19937_64 rng_;
    std::unordered_set<TenantId> victim_set_;
};

} // namespace megha
