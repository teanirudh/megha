// src/common/types.hpp
#pragma once

#include <cstdint>
#include <string>

namespace kumo
{

/**
 * Basic identifier types used throughout the simulator.
 * Having strong typedefs keeps function signatures clear.
 */
using TenantId = std::uint32_t;
using UserId = std::uint32_t;
using FunctionId = std::uint32_t;   // logical "function" / app type
using InvocationId = std::uint64_t; // individual invocation/request
using WorkerId = std::uint32_t;     // node / invoker
using ContainerId = std::uint64_t;  // warm container instance

/**
 * Simulation time.
 *
 * We keep it as double to support sub-millisecond precision later
 * if needed. For now you can think of it as "milliseconds since
 * the start of the experiment" or "seconds", depending on how we
 * configure the engine.
 */
using TimePoint = double;
using Duration = double;

/**
 * Optional tag for grouping functions into tenants / users.
 * This is mostly convenience for future logging / metrics
 */
struct Identity
{
    TenantId tenant = 0;
    UserId user = 0;
    FunctionId function = 0;
    InvocationId invocation = 0;
};

} // namespace kumo
