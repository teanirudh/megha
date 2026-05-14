#pragma once

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "../types.hpp"
#include "workload.hpp"

namespace megha
{

/**
 * PoissonWorkload:
 *
 * Generates invocations according to a homogeneous Poisson process.
 *
 * Parameters:
 *  - num_tenants: number of tenants (one function per tenant)
 *  - total_invocations: hard cap on generated invocations
 *  - max_batch_size: max number of invocations per next_batch() call
 *  - arrival_rate: arrival rate (lambda) for the *global* process
 *  - base_func_id, base_tenant_id: IDs for mapping tenants/functions
 *
 * Behavior:
 *  - Maintains an internal next_arrival_time_.
 *  - Each next_batch(now) will:
 *      * If next_arrival_time_ < now, reset it to now (no past arrivals).
 *      * Generate inter-arrival times ~ Exp(arrival_rate_) until:
 *          - we reach max_batch_size, or
 *          - we hit total_invocations_.
 *      * Each generated arrival_time >= now.
 */
class PoissonWorkload : public Workload
{
  public:
    PoissonWorkload(std::uint32_t num_tenants, std::uint64_t total_invocations,
                    std::uint32_t max_batch_size, double arrival_rate,
                    FunctionId base_func_id = 1, TenantId base_tenant_id = 1,
                    std::uint64_t seed = 0,
                    std::uint32_t functions_per_tenant = 1)
        : num_tenants_(num_tenants), total_invocations_(total_invocations),
          max_batch_size_(max_batch_size), arrival_rate_(arrival_rate),
          base_func_id_(base_func_id), base_tenant_id_(base_tenant_id),
          functions_per_tenant_(functions_per_tenant),
          rng_(seed ? seed : std::random_device{}()),
          exp_dist_(arrival_rate_ > 0.0 ? arrival_rate_ : 1.0)
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
        std::vector<Invocation> batch;

        if (!has_more())
        {
            return batch;
        }

        // Ensure we never generate arrivals in the past (relative to engine).
        if (next_arrival_time_ < now)
        {
            next_arrival_time_ = now;
        }

        std::uint64_t remaining = total_invocations_ - generated_;
        std::uint32_t to_gen = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(remaining, max_batch_size_));

        if (to_gen == 0)
            return batch;

        batch.reserve(to_gen);

        std::uniform_int_distribution<std::uint32_t> tenant_dist(
            0, num_tenants_ - 1);

        for (std::uint32_t i = 0; i < to_gen; ++i)
        {
            // Draw inter-arrival interval; protect against degenerate rate.
            double delta = (arrival_rate_ > 0.0) ? exp_dist_(rng_) : 0.0;
            next_arrival_time_ += delta;

            auto idx = tenant_dist(rng_);
            TenantId tenant = base_tenant_id_ + idx;

            std::uniform_int_distribution<std::uint32_t> func_dist(
                0, functions_per_tenant_ - 1);
            auto f_idx = func_dist(rng_);
            FunctionId func =
                base_func_id_ +
                static_cast<FunctionId>(idx * functions_per_tenant_ + f_idx);

            InvocationId inv_id = next_invocation_id_++;

            Duration svc = sample_service_time_();
            batch.emplace_back(inv_id, func, tenant,
                               /*arrival_time*/ next_arrival_time_, svc);
            generated_ += 1;
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
    double arrival_rate_;
    std::uint32_t functions_per_tenant_;

    FunctionId base_func_id_;
    TenantId base_tenant_id_;

    std::uint64_t generated_ = 0;
    InvocationId next_invocation_id_ = 1;

    TimePoint next_arrival_time_ = 0.0;
    Duration default_service_time_ = 5.0;

    std::mt19937_64 rng_;
    std::exponential_distribution<double> exp_dist_;

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

} // namespace megha
