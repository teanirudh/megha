#pragma once

#include "../types.hpp"
#include "../workload/workload.hpp"
#include "engine.hpp"

namespace megha
{

/**
 * Abstract Scenario:
 *
 * A Scenario wires together:
 *  - an Engine (platform + scheduler + metrics),
 *  - one or more Workloads,
 *  - and a driving policy for simulation time.
 *
 * You can later implement:
 *  - BaselineScenario (no attacker),
 *  - AttackScenario (victim + attacker workload),
 *  - TraceReplayScenario, etc.
 */
class Scenario
{
  public:
    virtual ~Scenario() = default;

    // Run the scenario to completion.
    virtual void run() = 0;

    // Access the underlying engine (for inspection / results).
    virtual Engine &engine() = 0;
    virtual const Engine &engine() const = 0;
};

/**
 * A simple scenario that:
 *  - owns an Engine and a single Workload,
 *  - at each step:
 *      * asks the workload for a batch of invocations (at current time),
 *      * enqueues them,
 *      * advances simulation time by a fixed step,
 *  - and finally drains all remaining events.
 *
 * This is essentially the cleaned-up version of your "uniform submission"
 * pattern from the old Cluster::run code.
 */
class SingleWorkloadScenario : public Scenario
{
  public:
    SingleWorkloadScenario(Engine engine, std::unique_ptr<Workload> workload,
                           Duration time_step)
        : engine_(std::move(engine)), workload_(std::move(workload)),
          time_step_(time_step)
    {
    }

    void run() override
    {
        TimePoint now = engine_.now();

        while (workload_ && workload_->has_more())
        {
            auto batch = workload_->next_batch(now);
            if (!batch.empty())
            {
                engine_.enqueue_invocations(batch);
            }
            engine_.run_until(now + time_step_);
            now += time_step_;
        }

        // Drain any remaining completion events.
        engine_.run();
    }

    Engine &engine() override { return engine_; }

    const Engine &engine() const override { return engine_; }

  private:
    Engine engine_;
    std::unique_ptr<Workload> workload_;
    Duration time_step_;
};

} // namespace megha
