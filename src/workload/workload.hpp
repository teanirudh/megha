// src/workload/workload.hpp
#pragma once

#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "common/types.hpp"
#include "model/app.hpp"

namespace kumo
{

/**
 * Abstract workload generator.
 *
 * A Workload produces batches of invocations over (simulation) time.
 * The driver (or a scenario class) calls next_batch(now) and then
 * feeds the returned invocations into the Engine.
 */
class Workload
{
  public:
    virtual ~Workload() = default;

    /**
     * Generate a batch of invocations that arrive at or after `now`.
     *
     * Implementations are free to:
     *  - Use `now` as the arrival time for all invocations in this batch, or
     *  - Use it as a lower bound and set arrival_time >= now.
     */
    virtual std::vector<Invocation> next_batch(TimePoint now) = 0;

    /// Return true if the workload has more invocations to generate.
    virtual bool has_more() const noexcept = 0;
};

/**
 * A simple synthetic workload:
 *
 *  - Has M tenants and one function per tenant.
 *  - At each call to next_batch(now), generates up to `max_batch_size_`
 *    invocations, uniformly sampling tenants.
 *  - Stops after generating `total_invocations_` in total.
 *
 * This is useful as a smoke-test workload for schedulers.
 * FunctionIds and TenantIds are assumed to match 1:1:
 *   func_id = base_func_id_ + tenant_index
 *   tenant  = base_tenant_id_ + tenant_index
 */
class UniformWorkload : public Workload
{
  public:
    UniformWorkload(std::uint32_t num_tenants, std::uint64_t total_invocations,
                    std::uint32_t max_batch_size, FunctionId base_func_id = 1,
                    TenantId base_tenant_id = 1, std::uint64_t seed = 0,
                    std::uint32_t functions_per_tenant = 1)
        : num_tenants_(num_tenants), total_invocations_(total_invocations),
          max_batch_size_(max_batch_size), base_func_id_(base_func_id),
          base_tenant_id_(base_tenant_id),
          functions_per_tenant_(functions_per_tenant),
          rng_(seed ? seed : std::random_device{}())
    {
    }

    enum class ServiceTimeModel
    {
        FIXED,
        EXPONENTIAL
    };

    void set_service_time_fixed(Duration d) noexcept
    {
        service_model_ = ServiceTimeModel::FIXED;
        service_fixed_ = d;
        default_service_time_ = d; // keep old behavior consistent
    }

    void set_service_time_exponential(Duration mean) noexcept
    {
        service_model_ = ServiceTimeModel::EXPONENTIAL;
        service_mean_ = mean;
        default_service_time_ = mean; // optional: still used if you want
    }

    std::vector<Invocation> next_batch(TimePoint now) override
    {
        (void)now;

        std::vector<Invocation> batch;
        if (!has_more())
            return batch;

        std::uint64_t remaining = total_invocations_ - generated_;
        std::uint32_t to_gen = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(remaining, max_batch_size_));
        if (to_gen == 0)
            return batch;

        batch.reserve(to_gen);

        std::uniform_int_distribution<std::uint32_t> tenant_dist(
            0, num_tenants_ - 1);
        std::uniform_int_distribution<std::uint32_t> func_dist(
            0, functions_per_tenant_ - 1);

        for (std::uint32_t i = 0; i < to_gen; ++i)
        {
            auto t_idx = tenant_dist(rng_);
            TenantId tenant = base_tenant_id_ + t_idx;

            auto f_idx = func_dist(rng_);
            FunctionId func =
                base_func_id_ +
                static_cast<FunctionId>(t_idx * functions_per_tenant_ + f_idx);

            InvocationId id = next_invocation_id_++;

            Duration svc = sample_service_time_();
            batch.emplace_back(id, func, tenant,
                               /*user*/ tenant,
                               /*arrival_time*/ current_time_, svc);

            ++generated_;
            ++current_time_; // simple monotonic timestamp
            if (generated_ >= total_invocations_)
                break;
        }

        return batch;
    }

    bool has_more() const noexcept override
    {
        return generated_ < total_invocations_;
    }

    void set_default_service_time(Duration d) noexcept
    {
        default_service_time_ = d;
    }

  private:
    std::uint32_t num_tenants_;
    std::uint64_t total_invocations_;
    std::uint32_t max_batch_size_;
    FunctionId base_func_id_;
    TenantId base_tenant_id_;
    std::uint32_t functions_per_tenant_;

    std::uint64_t generated_ = 0;
    InvocationId next_invocation_id_ = 1;
    TimePoint current_time_ = 0.0;
    Duration default_service_time_ = 5.0;

    std::mt19937_64 rng_;

    ServiceTimeModel service_model_ = ServiceTimeModel::FIXED;
    Duration service_fixed_ = 5.0;
    Duration service_mean_ = 5.0;

    Duration sample_service_time_()
    {
        if (service_model_ == ServiceTimeModel::FIXED)
        {
            return service_fixed_;
        }
        // EXPONENTIAL with mean = service_mean_
        // sample: -mean * ln(1-u), u in (0,1)
        std::uniform_real_distribution<double> uni(0.0, 1.0);
        double u = uni(rng_);
        if (u <= 0.0)
            u = 1e-12; // avoid log(0)
        return -service_mean_ * std::log(1.0 - u);
    }
};

} // namespace kumo
