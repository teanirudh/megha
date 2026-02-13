#include <iostream>

#include "common/types.hpp"
#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/spread_scheduler.hpp"
#include "core/engine.hpp"

using namespace kumo;

int main() {
    //
    // Setup platform with 2 workers
    //
    PlatformState ps;

    ResourceConfig cap{ .cpu_cores = 2.0, .memory_mb = 2048, .storage_mb = 50 };
    ps.add_worker(cap); // worker 0
    ps.add_worker(cap); // worker 1

    //
    // Define two functions for two tenants
    //
    FunctionProfile fA;
    fA.id = 1;
    fA.tenant = 10;
    fA.owner  = 10;
    fA.name   = "tenantA-func";
    fA.resources = { .cpu_cores = 0.5, .memory_mb = 128, .storage_mb = 5 };

    FunctionProfile fB;
    fB.id = 2;
    fB.tenant = 20;
    fB.owner  = 20;
    fB.name   = "tenantB-func";
    fB.resources = { .cpu_cores = 0.5, .memory_mb = 128, .storage_mb = 5 };

    ps.add_function(fA);
    ps.add_function(fB);

    //
    // Use SpreadScheduler
    //
    auto sched = SchedulerPtr(new SpreadScheduler(123));
    Engine engine(std::move(ps), std::move(sched));

    //
    // === Phase 1: Fill worker 0 with Tenant A
    //
    for (int i = 0; i < 3; ++i) {
        engine.enqueue_invocation(Invocation(
            /*id*/        1000 + i,
            /*func*/      fA.id,
            /*tenant*/    fA.tenant,
            /*user*/      fA.owner,
            /*arrival*/   0.0,
            /*service*/   50.0
        ));
    }

    engine.run_until(0.1); // process Tenant A placements

    std::cout << "After Phase 1:\n";
    for (WorkerId wid = 0; wid < 2; ++wid) {
        const auto& wv = engine.platform().worker_view(wid);
        std::cout << "  Worker " << wid << " tenants: ";
        for (auto t : wv.tenants_present) std::cout << t << " ";
        std::cout << "\n";
    }

    //
    // === Phase 2: Tenant B arrives
    //
    for (int i = 0; i < 3; ++i) {
        engine.enqueue_invocation(Invocation(
            /*id*/        2000 + i,
            /*func*/      fB.id,
            /*tenant*/    fB.tenant,
            /*user*/      fB.owner,
            /*arrival*/   1.0,
            /*service*/   10.0
        ));
    }

    engine.run();

    std::cout << "\nAfter Phase 2:\n";
    for (WorkerId wid = 0; wid < 2; ++wid) {
        const auto& wv = engine.platform().worker_view(wid);
        std::cout << "  Worker " << wid << " tenants: ";
        for (auto t : wv.tenants_present) std::cout << t << " ";
        std::cout << "\n";
    }

    //
    // Show co-location metrics
    //
    std::cout << "\nCo-location counts:\n";
    for (auto& kv : engine.metrics().colocations()) {
        auto pair = kv.first;
        std::cout << "  (" << pair.first << "," << pair.second
                  << ") -> " << kv.second << "\n";
    }

    return 0;
}
