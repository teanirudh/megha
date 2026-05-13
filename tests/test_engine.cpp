#include <iostream>

#include "common/types.hpp"
#include "core/engine.hpp"
#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/random_scheduler.hpp"

int main()
{
    using namespace kumo;

    // Function profile
    FunctionProfile f;
    f.id = 1;
    f.tenant = 1;
    f.owner = 1;
    f.name = "hello";
    f.resources =
        ResourceConfig{.cpu_cores = 0.5, .memory_mb = 128, .storage_mb = 10};

    PlatformState ps;
    ps.add_function(f);

    ResourceConfig cap{.cpu_cores = 2.0, .memory_mb = 1024, .storage_mb = 100};
    ps.add_worker(cap); // worker 0
    ps.add_worker(cap); // worker 1

    auto sched = SchedulerPtr(new RandomScheduler(123));
    Engine engine(std::move(ps), std::move(sched));

    // Enqueue 4 invocations at different times
    for (int i = 0; i < 4; ++i)
    {
        engine.enqueue_invocation(Invocation(
            /*invocation id*/ i + 1,
            /*function id*/ f.id,
            /*tenant*/ f.tenant,
            /*user*/ f.owner,
            /*arrival time*/ i * 10.0, // 0, 10, 20, 30
            /*service time*/ 5.0       // each lasts 5 time units
            ));
    }

    engine.run();

    const auto &m = engine.metrics();

    std::cout << "Per-worker invocations:\n";
    for (auto &kv : m.worker_invocations())
    {
        std::cout << "  worker " << kv.first << " -> " << kv.second << "\n";
    }

    std::cout << "Per-tenant invocations:\n";
    for (auto &kv : m.tenant_invocations())
    {
        std::cout << "  tenant " << kv.first << " -> " << kv.second << "\n";
    }

    const auto &platform = engine.platform();
    for (WorkerId wid : platform.workers())
    {
        const auto &w = platform.worker_view(wid);
        std::cout << "Worker " << wid << " used cpu=" << w.used.cpu_cores
                  << " mem=" << w.used.memory_mb
                  << " active=" << w.active_invocations << "\n";
    }

    std::cout << "Sim time now = " << engine.now() << "\n";
    std::cout << "Failed invocations: " << engine.num_failed_invocations()
              << "\n";
}
