#pragma once

#include <cstdint>
#include <optional>

#include "common/types.hpp"

namespace kumo {

struct Container {
    FunctionId function_id = 0;      // Which function this container belongs to
    WorkerId   worker_id   = 0;      // Which worker hosts this container

    bool busy = false;               // Is it currently running an invocation?
    TimePoint last_used = 0.0;       // Last time it became idle

    // Optional: track how many invocations have passed through it.
    std::uint64_t lifetime_invocations = 0;

    // Simple helper API
    bool is_idle() const noexcept { return !busy; }
};

} // namespace kumo
