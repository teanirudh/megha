// tests/test_scheduler_registry.cpp
#include <iostream>
#include "scheduler/registry.hpp"
#include "core/engine.hpp"
#include "model/platform_state.hpp"
#include "model/app.hpp"

using namespace kumo;

int main() {
    PlatformState ps;
    FunctionProfile f;
    f.id = 1; f.tenant = 1; f.owner = 1;
    f.resources = { .cpu_cores = 0.5, .memory_mb = 128, .storage_mb = 5 };
    ps.add_function(f);

    ResourceConfig cap{ .cpu_cores = 2.0, .memory_mb = 1024, .storage_mb = 50 };
    ps.add_worker(cap);

    auto& reg = SchedulerRegistry::instance();
    auto sched = reg.create("spread", /*seed*/ 123);

    Engine engine(std::move(ps), std::move(sched));

    engine.enqueue_invocation(Invocation(1, f.id, f.tenant, f.owner, 0.0, 10.0));
    engine.run();

    std::cout << "Scheduler via registry ran OK\n";
    return 0;
}
