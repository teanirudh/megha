// src/model/app.hpp
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "common/types.hpp"

namespace kumo
{

/**
 * Resource requirements for a single function.
 *
 * These are logical units from the simulator's perspective.
 * You can interpret them however you want when configuring
 * workers (e.g., CPU cores, GB of RAM, etc.).
 */
struct ResourceConfig
{
    double cpu_cores = 0.0;     // e.g., 0.1, 1.0, 2.0 ...
    std::size_t memory_mb = 0;  // in megabytes
    std::size_t storage_mb = 0; // in megabytes
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
    UserId owner = 0; // optional (may be same as tenant)
    std::string name; // human-readable, e.g., "image-resize"

    ResourceConfig resources;

    // Packages / labels are a direct generalization of your old "reqPackages"
    // and "label" fields. Schedulers (like PASch) can use this to do
    // package- or label-aware placement.
    std::vector<std::string> packages;
    std::string label; // optional free-form label

    // Timing characteristics (all in the same time unit as TimePoint/Duration).
    Duration cold_start_time = 200.0; // ms? configurable later
    Duration warm_start_time = 5.0;
    Duration idle_timeout = 30000.0; // 30 seconds until a container cools down

    // Optional concurrency hint (how many concurrent invocations per container).
    std::size_t max_concurrency_per_container = 1;
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

    Invocation(InvocationId id, FunctionId func, TenantId tenant, UserId user,
               TimePoint arrival, Duration service_time, std::string label = {})
        : id_{id}, function_id_{func}, tenant_id_{tenant}, user_id_{user},
          arrival_time_{arrival}, service_time_{service_time},
          label_{std::move(label)}
    {
    }

    InvocationId id() const noexcept { return id_; }
    FunctionId function_id() const noexcept { return function_id_; }
    TenantId tenant_id() const noexcept { return tenant_id_; }
    UserId user_id() const noexcept { return user_id_; }

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
    UserId user_id_ = 0;

    TimePoint arrival_time_ = 0.0;
    Duration service_time_ = 0.0;

    std::string label_;
};

} // namespace kumo
