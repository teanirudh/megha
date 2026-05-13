#pragma once
#include "common/types.hpp"
#include <cstdint>

namespace kumo
{

/**
 * Types of events the simulator can handle.
 */
enum class EventType : std::uint8_t
{
    InvocationStart, // An invocation arrives (we choose placement & start delay)
    InvocationExecute, // Invocation actually begins executing inside a container
    InvocationComplete, // Invocation execution completes
    ContainerCool,      // idle timeout reached
};

/**
 * A single scheduled event in the simulation.
 *
 * Events are ordered by `time` in a min-heap (earliest first).
 */
struct Event
{
    TimePoint time = 0.0;
    EventType type = EventType::InvocationStart;

    // Which invocation this event refers to.
    InvocationId invocation_id = 0;

    // Optional fields (used depending on event type).
    FunctionId function_id = 0;
    WorkerId worker_id = 0;

    // Convenience constructors.
    static Event invocation_start(TimePoint t, InvocationId inv_id,
                                  FunctionId func_id)
    {
        Event e;
        e.time = t;
        e.type = EventType::InvocationStart;
        e.invocation_id = inv_id;
        e.function_id = func_id;
        return e;
    }

    static Event invocation_execute(TimePoint t, InvocationId inv_id,
                                    FunctionId func_id, WorkerId wid)
    {
        Event e;
        e.time = t;
        e.type = EventType::InvocationExecute;
        e.invocation_id = inv_id;
        e.function_id = func_id;
        e.worker_id = wid;
        return e;
    }

    static Event invocation_complete(TimePoint t, InvocationId inv_id,
                                     FunctionId func_id, WorkerId wid)
    {
        Event e;
        e.time = t;
        e.type = EventType::InvocationComplete;
        e.invocation_id = inv_id;
        e.function_id = func_id;
        e.worker_id = wid;
        return e;
    }

    static Event container_cool(TimePoint t, FunctionId func_id, WorkerId wid)
    {
        Event e;
        e.time = t;
        e.type = EventType::ContainerCool;
        e.function_id = func_id;
        e.worker_id = wid;
        return e;
    }
};

/**
 * Comparator for using Event with std::priority_queue as a min-heap.
 */
struct EventTimeGreater
{
    bool operator()(const Event &lhs, const Event &rhs) const noexcept
    {
        return lhs.time > rhs.time;
    }
};

} // namespace kumo
