#include <iostream>

#include "common/types.hpp"
#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/random_scheduler.hpp"
#include "scheduler/spread_scheduler.hpp"

int main() {
    using namespace kumo;

    // 1. Create a function profile (like your old App "type")
    FunctionProfile f;
    f.id        = 42;
    f.tenant    = 10;
    f.owner     = 99;
    f.name      = "test-func";
    f.resources = ResourceConfig{ .cpu_cores = 0.5, .memory_mb = 128, .storage_mb = 10 };

    PlatformState state;

    // Register the function with the platform.
    state.add_function(f);

    // 2. Add a few workers with enough capacity.
    ResourceConfig workerCap;
    workerCap.cpu_cores  = 2.0;
    workerCap.memory_mb  = 1024;
    workerCap.storage_mb = 100;

    state.add_worker(workerCap); // worker 0
    state.add_worker(workerCap); // worker 1
    state.add_worker(workerCap); // worker 2

    // 3. Create a fake invocation of that function.
    Invocation inv(
        /*invocation id*/ 1,
        /*function id*/   f.id,
        /*tenant*/        f.tenant,
        /*user*/          f.owner,
        /*arrival time*/  0.0,
        /*service time*/  100.0
    );

    // 4. Use RandomScheduler to choose a worker.
    RandomScheduler sched(/*seed*/ 12345);

    auto decision = sched.schedule(inv, state);

    if (decision.success) {
        std::cout << "Scheduled on worker: " << decision.worker_id << "\n";
        return 0;
    } else {
        std::cerr << "Scheduling failed: " << decision.reason << "\n";
        return 1;
    }
}
