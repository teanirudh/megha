#include <iostream>

#include "common/types.hpp"
#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/random_scheduler.hpp"
#include "core/engine.hpp"

using namespace kumo;

int main() {
    std::cout << "=== Engine Cold/Warm Sanity Test ===\n";

    PlatformState ps;

    // Function with distinct cold vs warm start times
    FunctionProfile f;
    f.id        = 1;
    f.tenant    = 1;
    f.owner     = 1;
    f.name      = "test-func";
    f.resources = ResourceConfig{ .cpu_cores = 0.5, .memory_mb = 128, .storage_mb = 10 };
    f.cold_start_time = 10.0;
    f.warm_start_time = 1.0;
    f.idle_timeout    = 0.0; // not used yet
    ps.add_function(f);

    // Single worker
    ResourceConfig cap{ .cpu_cores = 4.0, .memory_mb = 2048, .storage_mb = 100 };
    ps.add_worker(cap);

    auto sched = SchedulerPtr(new RandomScheduler(123));
    Engine engine(std::move(ps), std::move(sched));

    // Two sequential invocations of the same function
    engine.enqueue_invocation(Invocation(
        /*id*/ 1,
        /*function id*/ f.id,
        /*tenant*/ f.tenant,
        /*user*/ f.owner,
        /*arrival*/ 0.0,
        /*service*/ 5.0
    ));

    engine.enqueue_invocation(Invocation(
        /*id*/ 2,
        /*function id*/ f.id,
        /*tenant*/ f.tenant,
        /*user*/ f.owner,
        /*arrival*/ 20.0,    // arrives after first finishes
        /*service*/ 5.0
    ));

    engine.run();

    const auto& ps_final = engine.platform();
    const auto& w = ps_final.worker_view(0);

    std::cout << "Sim time: " << engine.now() << "\n";
    std::cout << "Worker 0 containers: " << w.containers.size() << "\n";

    if (!w.containers.empty()) {
        const auto& c = w.containers.front();
        std::cout << "  container.func=" << c.function_id
                  << " busy=" << c.busy
                  << " lifetime_invocations=" << c.lifetime_invocations
                  << " last_used=" << c.last_used << "\n";
    }

    std::cout << "Failed invocations: " << engine.num_failed_invocations() << "\n";
    std::cout << "[COLD/WARM TEST COMPLETED]\n";

    return 0;
}
