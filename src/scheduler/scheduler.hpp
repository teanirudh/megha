#pragma once

#include <memory>
#include <string>

#include "../types.hpp"

namespace megha
{

// Forward declarations to avoid circular dependencies.
// These will be defined in other headers (model / core).
class PlatformState;
class Invocation;

/**
 * Result of a scheduling decision.
 *
 * For now we keep it simple: either we choose a worker, or we fail.
 * Later we can extend this with more details (e.g., reasons, hints
 * for scaling, etc.).
 */
struct SchedulingDecision
{
    bool success = false;
    WorkerId worker_id = 0;
    std::string reason; // optional human-readable explanation

    static SchedulingDecision ok(WorkerId wid)
    {
        SchedulingDecision d;
        d.success = true;
        d.worker_id = wid;
        return d;
    }

    static SchedulingDecision fail(std::string why = {})
    {
        SchedulingDecision d;
        d.success = false;
        d.reason = std::move(why);
        return d;
    }
};

/**
 * Abstract interface for all megha schedulers.
 *
 * Schedulers are *stateless* with respect to the request queue:
 * the simulation engine (or "driver") owns the queue of pending
 * invocations and calls `schedule()` one invocation at a time.
 *
 * The scheduler is allowed to maintain internal state (e.g.,
 * per-function warm-set statistics, frequency maps, etc.) as
 * member variables; it just doesn't own the global queue.
 */
class Scheduler
{
  public:
    virtual ~Scheduler() = default;

    // A short human-readable name, e.g. "random", "helper".
    virtual std::string name() const = 0;

    /**
     * Choose a worker for a given invocation.
     *
     * @param inv    The invocation to place (contains tenant, function, etc.).
     * @param state  Read-only view of the current platform state, including
     *               workers, warm containers, and resource usage.
     *
     * @return       A SchedulingDecision: either success + worker_id,
     *               or failure (no suitable worker).
     */
    virtual SchedulingDecision schedule(const Invocation &inv,
                                        const PlatformState &state) = 0;
};

/**
 * Convenience alias for owning scheduler instances.
 *
 * You can later use factories/registries that return SchedulerPtr.
 */
using SchedulerPtr = std::unique_ptr<Scheduler>;

} // namespace megha
