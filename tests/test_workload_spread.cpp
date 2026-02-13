#include <iostream>
#include <iomanip>

#include "common/types.hpp"
#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/spread_scheduler.hpp"
#include "workload/workload.hpp"
#include "core/engine.hpp"

using namespace kumo;

int main() {
    std::cout << "=== Workload + SpreadScheduler End-to-End Test ===\n";

    //
    // 1. Build platform with 3 tenants and 3 workers
    //
    const std::uint32_t num_tenants = 3;

    PlatformState ps;
    ResourceConfig func_res{ .cpu_cores = 0.25, .memory_mb = 64, .storage_mb = 5 };

    // define 1 function per tenant
    for (std::uint32_t i = 0; i < num_tenants; ++i) {
        FunctionProfile f;
        f.id        = 1 + i;
        f.tenant    = 1 + i;
        f.owner     = 1 + i;
        f.name      = "func-" + std::to_string(f.id);
        f.resources = func_res;
        ps.add_function(f);
    }

    // workers
    ResourceConfig cap{ .cpu_cores = 4.0, .memory_mb = 4096, .storage_mb = 200 };
    ps.add_worker(cap);   // worker 0
    ps.add_worker(cap);   // worker 1
    ps.add_worker(cap);   // worker 2

    //
    // 2. Set scheduler + engine
    //
    auto sched = SchedulerPtr(new SpreadScheduler(123));
    Engine engine(std::move(ps), std::move(sched));

    //
    // 3. Uniform workload: 3 tenants, 30 total invocations, 5 per batch
    //
    UniformWorkload workload(/*num_tenants=*/num_tenants,
                             /*total_invocations=*/30,
                             /*max_batch_size=*/5,
                             /*base_func_id=*/1,
                             /*base_tenant_id=*/1,
                             /*seed=*/42);

    workload.set_default_service_time(20.0);

    //
    // 4. Drive simulation over time
    //
    TimePoint now = 0.0;
    Duration step = 10.0;

    while (workload.has_more()) {
        auto batch = workload.next_batch(now);
        engine.enqueue_invocations(batch);
        engine.run_until(now + step);
        now += step;
    }

    engine.run(); // drain all completion events

    //
    // 5. Print results
    //
    std::cout << "\n=== Simulation Results ===\n";
    std::cout << "Simulated time: " << engine.now() << "\n";
    std::cout << "Failed invocations: " << engine.num_failed_invocations() << "\n";

    const auto& ps_final = engine.platform();

    std::cout << "\nWorker tenant presence:\n";
    for (WorkerId wid : ps_final.workers()) {
        const auto& w = ps_final.worker_view(wid);

        std::cout << "  Worker " << wid << ": tenants = { ";
        for (auto t : w.tenants_present) std::cout << t << " ";
        std::cout << "} active=" << w.active_invocations
                  << " cpu_used=" << w.used.cpu_cores << "\n";
    }

    const auto& M = engine.metrics();

    std::cout << "\nPer-tenant invocation counts:\n";
    for (auto& kv : M.tenant_invocations()) {
        std::cout << "  tenant " << kv.first << " -> " << kv.second << "\n";
    }

    std::cout << "\nCo-location counts:\n";
    for (auto& kv : M.colocations()) {
        auto p = kv.first;
        std::cout << "  (" << p.first << "," << p.second << ") -> " << kv.second << "\n";
    }

    std::cout << "\n[TEST COMPLETED]\n";
    return 0;
}
