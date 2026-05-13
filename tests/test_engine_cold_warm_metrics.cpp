#include <iostream>

#include "common/types.hpp"
#include "core/engine.hpp"
#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/random_scheduler.hpp"

using namespace kumo;

int main()
{
    std::cout << "=== Engine Cold/Warm Metrics Test ===\n";

    PlatformState ps;

    // Function with distinct cold vs warm start times
    FunctionProfile f;
    f.id = 1;
    f.tenant = 1;
    f.owner = 1;
    f.name = "test-func";
    f.resources =
        ResourceConfig{.cpu_cores = 0.5, .memory_mb = 128, .storage_mb = 10};
    f.cold_start_time = 10.0;
    f.warm_start_time = 1.0;
    f.idle_timeout = 0.0; // not used yet
    ps.add_function(f);

    // Single worker
    ResourceConfig cap{.cpu_cores = 4.0, .memory_mb = 2048, .storage_mb = 100};
    ps.add_worker(cap);

    auto sched = SchedulerPtr(new RandomScheduler(123));
    Engine engine(std::move(ps), std::move(sched));

    // Two invocations of the same function far enough apart
    engine.enqueue_invocation(Invocation(
        /*id*/ 1,
        /*function id*/ f.id,
        /*tenant*/ f.tenant,
        /*user*/ f.owner,
        /*arrival*/ 0.0,
        /*service*/ 5.0));

    engine.enqueue_invocation(Invocation(
        /*id*/ 2,
        /*function id*/ f.id,
        /*tenant*/ f.tenant,
        /*user*/ f.owner,
        /*arrival*/ 20.0, // arrives after first finishes
        /*service*/ 5.0));

    engine.run();

    const auto &ps_final = engine.platform();
    const auto &w = ps_final.worker_view(0);

    std::cout << "Sim time: " << engine.now() << "\n";
    std::cout << "Worker 0 containers: " << w.containers.size() << "\n";
    if (!w.containers.empty())
    {
        const auto &c = w.containers.front();
        std::cout << "  container.func=" << c.function_id << " busy=" << c.busy
                  << " lifetime_invocations=" << c.lifetime_invocations
                  << " last_used=" << c.last_used << "\n";
    }

    const auto &m = engine.metrics();
    std::uint64_t cold = m.cold_starts_for(f.id);
    std::uint64_t warm = m.warm_starts_for(f.id);

    std::cout << "Cold starts for func " << f.id << " = " << cold << "\n";
    std::cout << "Warm starts for func " << f.id << " = " << warm << "\n";
    std::cout << "Failed invocations: " << engine.num_failed_invocations()
              << "\n";

    bool ok = true;
    if (cold != 1)
    {
        std::cout << "[FAIL] expected 1 cold start\n";
        ok = false;
    }
    if (warm != 1)
    {
        std::cout << "[FAIL] expected 1 warm start\n";
        ok = false;
    }
    if (engine.num_failed_invocations() != 0)
    {
        std::cout << "[FAIL] expected 0 failed invocations\n";
        ok = false;
    }

    if (ok)
    {
        std::cout << "[COLD/WARM METRICS TEST PASS]\n";
        return 0;
    }
    else
    {
        std::cout << "[COLD/WARM METRICS TEST FAIL]\n";
        return 1;
    }
}
