#pragma once

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "common/types.hpp"
#include "workload/workload.hpp"

namespace kumo
{

/**
 * BurstWorkload:
 *
 * Time-varying Poisson process:
 *   - base_rate outside bursts
 *   - burst_rate during bursts
 *
 * Bursts repeat every `burst_period` units of simulated time,
 * and each burst lasts for `burst_duty_cycle * burst_period`.
 *
 * Example:
 *   base_rate=0.1, burst_rate=2.0, period=60, duty_cycle=0.2
 *   => 12 seconds of high load every minute.
 */
class BurstWorkload : public Workload
{
  public:
    BurstWorkload(std::uint32_t num_tenants, std::uint64_t total_invocations,
                  std::uint32_t max_batch_size, double base_rate,
                  double burst_rate, double burst_period,
                  double burst_duty_cycle, FunctionId base_func_id = 1,
                  TenantId base_tenant_id = 1, std::uint64_t seed = 0,
                  std::uint32_t functions_per_tenant = 1)
        : num_tenants_(num_tenants), total_invocations_(total_invocations),
          max_batch_size_(max_batch_size), base_rate_(base_rate),
          burst_rate_(burst_rate), burst_period_(burst_period),
          burst_duty_cycle_(burst_duty_cycle), base_func_id_(base_func_id),
          base_tenant_id_(base_tenant_id),
          functions_per_tenant_(functions_per_tenant),
          rng_(seed ? seed : std::random_device{}()), exp01_(1.0)
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
            return batch;

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
            double rate = current_rate(next_arrival_time_);
            if (rate <= 0.0)
            {
                // If both base_rate and burst_rate are zero, we can't proceed.
                break;
            }

            double delta = exp01_(rng_) / rate; // Exp(rate) via Exp(1)/rate
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
                               /*user*/ tenant,
                               /*arrival_time*/ next_arrival_time_, svc);

            ++generated_;
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
    double current_rate(TimePoint t) const noexcept
    {
        if (burst_period_ <= 0.0)
        {
            // Degenerate: just use base_rate.
            return base_rate_;
        }
        double phase = std::fmod(t, burst_period_);
        if (phase < 0.0)
            phase += burst_period_;

        double burst_duration = burst_duty_cycle_ * burst_period_;
        if (burst_duration < 0.0)
            burst_duration = 0.0;
        if (burst_duration > burst_period_)
            burst_duration = burst_period_;

        if (phase < burst_duration)
        {
            return burst_rate_;
        }
        return base_rate_;
    }

  private:
    std::uint32_t num_tenants_;
    std::uint64_t total_invocations_;
    std::uint32_t max_batch_size_;
    std::uint32_t functions_per_tenant_;

    double base_rate_;
    double burst_rate_;
    double burst_period_;
    double burst_duty_cycle_;

    FunctionId base_func_id_;
    TenantId base_tenant_id_;

    std::uint64_t generated_ = 0;
    InvocationId next_invocation_id_ = 1;
    TimePoint next_arrival_time_ = 0.0;
    Duration default_service_time_ = 5.0;

    std::mt19937_64 rng_;
    std::exponential_distribution<double> exp01_;

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
