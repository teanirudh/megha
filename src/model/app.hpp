#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../types.hpp"

namespace megha
{

/**
 * Per-cycle hardware counter rates for simulated telemetry.
 */
struct MicroarchProfile
{
    double llc_loads_per_cycle = 0.0;
    double llc_stores_per_cycle = 0.0;
    double llc_load_misses_per_cycle = 0.0;
    double llc_store_misses_per_cycle = 0.0;
    double instructions_per_cycle = 1.0;
};

/**
 * Resource requirements for a single function.
 *
 * These are logical units from the simulator's perspective.
 * You can interpret them however you want when configuring
 * workers (e.g., CPU cores, GB of RAM, etc.).
 */
struct ResourceConfig
{
    double cpu_cores = 0;
    std::size_t memory_mb = 0;
    std::size_t storage_mb = 0;
};

/**
 * Static profile of a serverless function.
 *
 * This captures what your old App class stored as "per-app"
 * properties: resources, packages, and some timing knobs.
 * At runtime, *invocations* will reference this via FunctionId.
 */
struct FunctionProfile
{
    FunctionId id = 0;
    TenantId tenant = 0;
    std::string name;

    ResourceConfig resources;

    std::vector<std::string> packages;
    std::string label;

    Duration cold_start_time = 200.0;
    Duration warm_start_time = 5.0;
    Duration idle_timeout = 30000.0;

    std::size_t max_concurrency_per_container = 1;

    MicroarchProfile microarch{
        .llc_loads_per_cycle = 0.03,
        .llc_stores_per_cycle = 0.01,
        .llc_load_misses_per_cycle = 0.001,
        .llc_store_misses_per_cycle = 0.0005,
        .instructions_per_cycle = 1.0,
    };
};

/**
 * A single invocation of a function.
 *
 * This is what gets scheduled by the scheduler. It is intentionally
 * lightweight: it holds IDs and timing information, while the
 * detailed function metadata lives in FunctionProfile and is
 * accessed via PlatformState.
 */
class Invocation
{
  public:
    Invocation() = default;

    Invocation(InvocationId id, FunctionId func, TenantId tenant,
               TimePoint arrival, Duration service_time, std::string label = {})
        : id_{id}, function_id_{func}, tenant_id_{tenant},
          arrival_time_{arrival}, service_time_{service_time},
          label_{std::move(label)}
    {
    }

    InvocationId id() const noexcept { return id_; }
    FunctionId function_id() const noexcept { return function_id_; }
    TenantId tenant_id() const noexcept { return tenant_id_; }

    TimePoint arrival_time() const noexcept { return arrival_time_; }
    Duration service_time() const noexcept { return service_time_; }

    const std::string &label() const noexcept { return label_; }

    // For convenience when you want to mutate scheduling-related fields
    // (e.g., if the engine wants to adjust service_time for some reason).
    void set_service_time(Duration d) noexcept { service_time_ = d; }

  private:
    InvocationId id_ = 0;
    FunctionId function_id_ = 0;
    TenantId tenant_id_ = 0;

    TimePoint arrival_time_ = 0.0;
    Duration service_time_ = 0.0;

    std::string label_;
};

} // namespace megha
