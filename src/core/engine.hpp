#pragma once

#include <algorithm>
#include <deque>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "common/types.hpp"
#include "core/event.hpp"
#include "core/trace_logger.hpp"
#include "metrics/metrics.hpp"
#include "model/app.hpp"
#include "model/container.hpp"
#include "model/platform_state.hpp"
#include "scheduler/scheduler.hpp"

namespace kumo
{

/**
 * A discrete-event simulation engine with basic cold/warm container modeling.
 *
 * Flow:
 *  - enqueue_invocation() creates an InvocationStart event at arrival_time.
 *  - InvocationStart:
 *      * Scheduler chooses a worker.
 *      * Engine decides warm vs cold based on worker.containers.
 *      * Engine schedules InvocationExecute at now + (warm|cold)_start_time.
 *      * MetricsCollector notified of "start" placement.
 *  - InvocationExecute:
 *      * Engine finds/creates a container on that worker.
 *      * Marks container busy, allocates resources.
 *      * Schedules InvocationComplete at now + service_time.
 *  - InvocationComplete:
 *      * Engine frees resources.
 *      * Marks container idle and updates last_used.
 *      * MetricsCollector notified of completion.
 */
class Engine
{
  public:
    std::unordered_map<WorkerId, std::size_t> max_q_seen_;

    Engine(PlatformState platform, SchedulerPtr scheduler)
        : platform_(std::move(platform)), scheduler_(std::move(scheduler))
    {
        if (!scheduler_)
        {
            throw std::invalid_argument("Engine requires a non-null Scheduler");
        }
    }

    void set_max_queue_len(std::size_t n) { max_queue_len_ = n; }

    /// Current simulation time.
    TimePoint now() const noexcept { return current_time_; }

    /// Access metrics (mutable and const).
    MetricsCollector &metrics() noexcept { return metrics_; }

    const MetricsCollector &metrics() const noexcept { return metrics_; }

    /// Enqueue a single invocation as an event-driven job.
    void enqueue_invocation(const Invocation &inv)
    {
        const auto id = inv.id();
        inv_map_[id] = inv;

        // NEW: record arrival for DoS metrics
        metrics_.on_invocation_arrival(inv);

        events_.push(
            Event::invocation_start(inv.arrival_time(), id, inv.function_id()));
    }

    /// Enqueue a batch of invocations.
    void enqueue_invocations(const std::vector<Invocation> &invs)
    {
        for (const auto &inv : invs)
        {
            enqueue_invocation(inv);
        }
    }

    /// Run the simulation until there are no more events.
    void run()
    {
        while (!events_.empty())
        {
            process_next_event();
        }
    }

    /// Run until next event time > `until`.
    void run_until(TimePoint until)
    {
        while (!events_.empty())
        {
            const auto &ev = events_.top();
            if (ev.time > until)
            {
                break;
            }
            process_next_event();
        }
        if (current_time_ < until)
        {
            current_time_ = until;
        }
    }

    /// Access the platform state (for inspection / metrics).
    const PlatformState &platform() const noexcept { return platform_; }

    /// Number of invocations that failed to schedule or run.
    std::size_t num_failed_invocations() const noexcept
    {
        return failed_invocations_.size();
    }

    /// IDs of invocations that failed.
    const std::vector<InvocationId> &failed_invocations() const noexcept
    {
        return failed_invocations_;
    }

  private:
    void process_next_event()
    {
        if (events_.empty())
            return;

        Event ev = events_.top();
        events_.pop();

        current_time_ = ev.time;

        switch (ev.type)
        {
        case EventType::InvocationStart:
            handle_invocation_start(ev);
            break;
        case EventType::InvocationExecute:
            handle_invocation_execute(ev);
            break;
        case EventType::InvocationComplete:
            handle_invocation_complete(ev);
            break;
        case EventType::ContainerCool:
            handle_container_cool(ev);
            break;
        }
    }

    void handle_invocation_start(const Event &ev)
    {
        auto it = inv_map_.find(ev.invocation_id);

        const Invocation &inv = it->second;
        const FunctionProfile *func = platform_.get_function(inv.function_id());
        // Ask scheduler for placement decision.
        auto decision = scheduler_->schedule(inv, platform_);

        WorkerId wid = decision.worker_id;

        auto &w = platform_.mutable_worker(wid);

        // Maintain worker's "tenants_present".
        if (std::find(w.tenants_present.begin(), w.tenants_present.end(),
                      inv.tenant_id()) == w.tenants_present.end())
        {
            w.tenants_present.push_back(inv.tenant_id());
        }

        // Decide warm vs cold: check for an idle container of this function.
        bool has_idle_container = false;
        for (auto &c : w.containers)
        {
            if (c.function_id == inv.function_id() && c.is_idle())
            {
                has_idle_container = true;
                break;
            }
        }

        // If no container exists at all, create one (cold).
        if (!has_idle_container &&
            !has_container_for_function(w, inv.function_id()))
        {
            Container c;
            c.function_id = inv.function_id();
            c.worker_id = wid;
            c.busy = false;
            c.last_used = current_time_;
            c.lifetime_invocations = 0;
            w.containers.push_back(c);

            // Also track warm_functions: this worker now has a warmable container
            if (std::find(w.warm_functions.begin(), w.warm_functions.end(),
                          inv.function_id()) == w.warm_functions.end())
            {
                w.warm_functions.push_back(inv.function_id());
            }
        }

        // Determine start delay based on warm vs cold.
        Duration start_delay =
            has_idle_container ? func->warm_start_time : func->cold_start_time;

        if (TraceLogger::enabled())
        {
            TraceLogger::log("[place] t=", current_time_, " inv=", inv.id(),
                             " func=", inv.function_id(),
                             " tenant=", inv.tenant_id(), " worker=", wid);
        }

        // Notify metrics collector about placement.
        metrics_.on_invocation_start(inv, wid, platform_, current_time_);

        // notify cold vs warm.
        if (has_idle_container)
        {
            metrics_.on_warm_start(inv, wid, platform_);
            if (TraceLogger::enabled())
            {
                TraceLogger::log("[warm_start] inv=", inv.id(),
                                 " func=", inv.function_id(), " worker=", wid,
                                 " delay=", start_delay);
            }
        }
        else
        {
            metrics_.on_cold_start(inv, wid, platform_);
            if (TraceLogger::enabled())
            {
                TraceLogger::log("[cold_start] inv=", inv.id(),
                                 " func=", inv.function_id(), " worker=", wid,
                                 " delay=", start_delay);
            }
        }

        // Remember which worker is chosen for this invocation.
        inv_worker_[inv.id()] = wid;

        // Schedule the execution event.
        TimePoint exec_time = current_time_ + start_delay;
        events_.push(Event::invocation_execute(exec_time, inv.id(),
                                               inv.function_id(), wid));
    }

    void handle_invocation_execute(const Event &ev)
    {
        auto inv_it = inv_map_.find(ev.invocation_id);

        const Invocation &inv = inv_it->second;
        const FunctionProfile *func = platform_.get_function(inv.function_id());

        WorkerId wid = ev.worker_id;
        auto worker_it = inv_worker_.find(inv.id());
        if (worker_it != inv_worker_.end())
        {
            wid = worker_it->second;
        }

        auto &w = platform_.mutable_worker(wid);

        // NEW: capacity check at execute-time (realistic contention)
        // Do this BEFORE allocating resources or marking containers busy.
        if (!platform_.can_host(wid, *func))
        {
            auto &q = waitq_[wid];

            if (q.size() >= max_queue_len_)
            {
                // hard drop (this is the only real "failed" in DoS model)
                // std::cout << "[drop] t=" << current_time_ << " inv=" << inv.id() << " worker=" << wid << std::endl;
                failed_invocations_.push_back(inv.id());
                metrics_.on_invocation_drop(inv, wid, current_time_);

                inv_map_.erase(inv_it);
                inv_worker_.erase(inv.id());
                return;
            }

            q.push_back(inv.id());
            max_q_seen_[wid] = std::max(max_q_seen_[wid], q.size());

            if (TraceLogger::enabled())
            {
                TraceLogger::log("[queue] t=", current_time_, " inv=", inv.id(),
                                 " worker=", wid, " qlen=", q.size());
            }
            return;
        }

        // At this point, we are actually going to execute now.

        // Find an idle container for this function; if none, create one.
        Container *container = nullptr;
        for (auto &c : w.containers)
        {
            if (c.function_id == inv.function_id() && c.is_idle())
            {
                container = &c;
                break;
            }
        }
        if (!container)
        {
            Container c;
            c.function_id = inv.function_id();
            c.worker_id = wid;
            c.busy = false;
            c.last_used = current_time_;
            c.lifetime_invocations = 0;
            w.containers.push_back(c);
            container = &w.containers.back();

            if (std::find(w.warm_functions.begin(), w.warm_functions.end(),
                          inv.function_id()) == w.warm_functions.end())
            {
                w.warm_functions.push_back(inv.function_id());
            }
        }

        // Mark container busy and increment lifetime invocation count.
        container->busy = true;
        container->lifetime_invocations += 1;

        // Allocate resources for this execution.
        w.used.cpu_cores += func->resources.cpu_cores;
        w.used.memory_mb += func->resources.memory_mb;
        w.used.storage_mb += func->resources.storage_mb;
        w.active_invocations += 1;

        if (TraceLogger::enabled())
        {
            TraceLogger::log(
                "[execute] t=", current_time_, " inv=", inv.id(),
                " func=", inv.function_id(), " worker=", wid,
                " container_lifetime=", container->lifetime_invocations);
        }

        // record actual execution start time for latency
        metrics_.on_invocation_execute(inv, wid, current_time_);

        // Schedule completion after service time.
        TimePoint completion_time = current_time_ + inv.service_time();
        events_.push(Event::invocation_complete(completion_time, inv.id(),
                                                inv.function_id(), wid));
    }

    void handle_invocation_complete(const Event &ev)
    {
        auto inv_it = inv_map_.find(ev.invocation_id);
        if (inv_it == inv_map_.end())
        {
            return;
        }

        const Invocation &inv = inv_it->second;
        const FunctionProfile *func = platform_.get_function(inv.function_id());
        if (!func)
        {
            inv_map_.erase(inv_it);
            inv_worker_.erase(inv.id());
            return;
        }

        WorkerId wid = ev.worker_id;
        auto worker_it = inv_worker_.find(inv.id());
        if (worker_it != inv_worker_.end())
        {
            wid = worker_it->second;
        }

        auto &w = platform_.mutable_worker(wid);

        // Free resources.
        w.used.cpu_cores -= func->resources.cpu_cores;
        w.used.memory_mb -= func->resources.memory_mb;
        w.used.storage_mb -= func->resources.storage_mb;
        if (w.active_invocations > 0)
        {
            w.active_invocations -= 1;
        }

        // Mark container idle and update last_used.
        for (auto &c : w.containers)
        {
            if (c.function_id == inv.function_id() && c.busy)
            {
                c.busy = false;
                c.last_used = current_time_;

                // If idle_timeout > 0, schedule cool-down event.
                if (func->idle_timeout > 0)
                {
                    TimePoint cool_time = current_time_ + func->idle_timeout;
                    events_.push(
                        Event::container_cool(cool_time, c.function_id, wid));
                }
                break;
            }
        }

        if (TraceLogger::enabled())
        {
            TraceLogger::log("[complete] t=", current_time_, " inv=", inv.id(),
                             " func=", inv.function_id(), " worker=", wid);
        }

        // Notify metrics collector.
        metrics_.on_invocation_complete(inv, wid, platform_);

        // NEW: record end-to-end latency
        metrics_.on_invocation_finish(inv, current_time_);

        // NEW: if queue has work, start next immediately
        auto qit = waitq_.find(wid);

        if (qit != waitq_.end() && !qit->second.empty())
        {
            InvocationId next_id = qit->second.front();
            qit->second.pop_front();

            auto nit = inv_map_.find(next_id);
            if (nit != inv_map_.end())
            {
                const Invocation &next_inv = nit->second;
                // start immediately (no extra cold/warm delay; that was already paid)
                events_.push(Event::invocation_execute(
                    current_time_, next_inv.id(), next_inv.function_id(), wid));
                if (TraceLogger::enabled())
                {
                    TraceLogger::log("[dequeue] t=", current_time_,
                                     " inv=", next_inv.id(), " worker=", wid,
                                     " qlen=", qit->second.size());
                }
            }
        }

        inv_map_.erase(inv_it);
        inv_worker_.erase(inv.id());
    }
    void handle_container_cool(const Event &ev)
    {
        WorkerId wid = ev.worker_id;
        FunctionId fid = ev.function_id;

        auto &w = platform_.mutable_worker(wid);
        bool erased_any = false;

        if (TraceLogger::enabled())
        {
            TraceLogger::log("[cool_event] t=", current_time_, " worker=", wid,
                             " func=", fid);
        }

        // Remove idle containers for this function.
        // If container is busy or has been reused (last_used > timeout start),
        // skip removal.
        w.containers.erase(
            std::remove_if(
                w.containers.begin(), w.containers.end(),
                [&](const Container &c)
                {
                    return (c.function_id == fid && !c.busy &&
                            // ‘stale’ meaning it has remained idle
                            // exactly until this cooling event
                            c.last_used <=
                                ev.time -
                                    platform_.get_function(fid)->idle_timeout);
                }),
            w.containers.end());

        // Optionally: also drop from warm_functions
        auto &wf = w.warm_functions;
        if (std::none_of(w.containers.begin(), w.containers.end(),
                         [&](auto &c) { return c.function_id == fid; }))
        {
            wf.erase(std::remove(wf.begin(), wf.end(), fid), wf.end());
        }
    }

    bool has_container_for_function(const WorkerView &w,
                                    FunctionId func_id) const
    {
        for (const auto &c : w.containers)
        {
            if (c.function_id == func_id)
                return true;
        }
        return false;
    }

  private:
    PlatformState platform_;
    SchedulerPtr scheduler_;

    TimePoint current_time_ = 0.0;

    // All pending/running invocations (by ID).
    std::unordered_map<InvocationId, Invocation> inv_map_;

    // Mapping invocation -> worker running it.
    std::unordered_map<InvocationId, WorkerId> inv_worker_;

    // Event priority queue (earliest time first).
    std::priority_queue<Event, std::vector<Event>, EventTimeGreater> events_;

    std::vector<InvocationId> failed_invocations_;

    // Metrics.
    MetricsCollector metrics_;

    // NEW: per-worker waiting queue and a cap
    std::unordered_map<WorkerId, std::deque<InvocationId>> waitq_;
    std::size_t max_queue_len_ = 100; // default; make configurable later
};

} // namespace kumo
