#pragma once

#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "../types.hpp"
#include "app.hpp"
#include "container.hpp"

namespace megha
{

/**
 * A read-mostly view of a worker ("invoker") in the platform.
 *
 * Schedulers will typically only read this via PlatformState.
 * The engine will update it as invocations start/finish.
 */
struct WorkerView
{
    WorkerId id = 0;

    ResourceConfig capacity; // total resources on this worker
    ResourceConfig used;     // current usage

    // For security / scheduling decisions:
    std::vector<FunctionId>
        warm_functions; // functions with at least one warm container
    std::vector<Container> containers;
    std::vector<TenantId> tenants_present; // tenants currently running here

    std::size_t active_invocations = 0; // total in-flight invocations
};

/**
 * PlatformState owns the workers and function profiles and exposes
 * a read-only view for schedulers. The simulation engine will be the
 * only component that mutates this in practice.
 *
 * For now, it's a simple in-memory container with inline methods.
 * We can later move heavy logic into a .cpp if needed.
 */
class PlatformState
{
  public:
    PlatformState() = default;

    //
    // ---- Worker management (engine-facing) ----
    //

    /**
     * Add a new worker with the given capacity and return its WorkerId.
     * WorkerIds are assigned sequentially starting from 0.
     */
    WorkerId add_worker(const ResourceConfig &capacity)
    {
        WorkerId id = static_cast<WorkerId>(workers_.size());
        WorkerView w;
        w.id = id;
        w.capacity = capacity;
        w.used = ResourceConfig{};
        workers_.push_back(std::move(w));
        return id;
    }

    /**
     * Direct mutable access for the engine. Schedulers should not
     * call this; they should use const references from worker_view().
     */
    WorkerView &mutable_worker(WorkerId id)
    {
        auto idx = static_cast<std::size_t>(id);
        if (idx >= workers_.size())
        {
            throw std::out_of_range("invalid WorkerId in mutable_worker");
        }
        return workers_[idx];
    }

    //
    // ---- Function management (engine-facing) ----
    //

    void add_function(FunctionProfile profile)
    {
        FunctionId id = profile.id;
        functions_[id] = std::move(profile);
    }

    //
    // ---- Queries (scheduler-facing) ----
    //

    // List of all workers' IDs.
    std::vector<WorkerId> workers() const
    {
        std::vector<WorkerId> ids;
        ids.reserve(workers_.size());
        for (const auto &w : workers_)
        {
            ids.push_back(w.id);
        }
        return ids;
    }

    // More detailed access to a worker's state.
    const WorkerView &worker_view(WorkerId id) const
    {
        auto idx = static_cast<std::size_t>(id);
        if (idx >= workers_.size())
        {
            throw std::out_of_range("invalid WorkerId in worker_view");
        }
        return workers_[idx];
    }

    // Number of workers.
    std::size_t num_workers() const noexcept { return workers_.size(); }

    // Lookup a function profile by FunctionId; returns nullptr if not found.
    const FunctionProfile *get_function(FunctionId id) const noexcept
    {
        auto it = functions_.find(id);
        if (it == functions_.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    /**
     * Convenience: check if the worker has enough *remaining* capacity
     * to host one more invocation of this function (ignoring warm/cold).
     */
    bool can_host(WorkerId wid, const FunctionProfile &func) const
    {
        const auto &w = worker_view(wid);
        // simple resource check
        double avail_cpu = w.capacity.cpu_cores - w.used.cpu_cores;
        auto avail_mem = w.capacity.memory_mb - w.used.memory_mb;
        auto avail_stor = w.capacity.storage_mb - w.used.storage_mb;

        return (avail_cpu >= func.resources.cpu_cores) &&
               (avail_mem >= func.resources.memory_mb) &&
               (avail_stor >= func.resources.storage_mb);
    }

  private:
    std::vector<WorkerView> workers_;
    std::unordered_map<FunctionId, FunctionProfile> functions_;
};

} // namespace megha
