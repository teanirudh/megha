#pragma once

#include <vector>

#include "../model/app.hpp"
#include "../types.hpp"

namespace megha
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

    // Return true if the workload has more invocations to generate.
    virtual bool has_more() const noexcept = 0;
};

} // namespace megha
