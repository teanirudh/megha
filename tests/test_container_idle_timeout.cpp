#include <iostream>

#include "common/types.hpp"
#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/random_scheduler.hpp"
#include "core/engine.hpp"

using namespace kumo;

int main() {
    std::cout << "=== Idle Timeout Test ===\n";

    PlatformState ps;

    FunctionProfile f;
    f.id = 1;
    f.tenant = 1;
    f.owner = 1;
    f.name = "T";
    f.resources = { .cpu_cores=0.1, .memory_mb=64, .storage_mb=1 };
    f.cold_start_time = 10.0;
    f.warm_start_time = 2.0;
    f.idle_timeout    = 15.0; // key parameter
    ps.add_function(f);

    ps.add_worker({ .cpu_cores=4, .memory_mb=4096, .storage_mb=100 });

    auto sched = SchedulerPtr(new RandomScheduler(123));
    Engine eng(std::move(ps), std::move(sched));

    // First invocation — cold
    eng.enqueue_invocation(Invocation(1, f.id, 1, 1, 0.0, 5.0));

    // Second arrives AFTER idle timeout → expect another cold start
    eng.enqueue_invocation(Invocation(2, f.id, 1, 1, 40.0, 5.0));

    eng.run();

    const auto& m = eng.metrics();
    std::cout << "Cold starts = " << m.cold_starts_for(f.id) << "\n";
    std::cout << "Warm starts = " << m.warm_starts_for(f.id) << "\n";

    const auto& w = eng.platform().worker_view(0);
    std::cout << "Containers alive after test: " << w.containers.size() << "\n";

    // Expected:
    // cold=2, warm=0  (two separate cold starts)
    bool ok = true;
    if (m.cold_starts_for(f.id) != 2) { ok=false; }
    if (m.warm_starts_for(f.id) != 0) { ok=false; }

    if (ok) std::cout << "[IDLE TIMEOUT TEST PASS]\n";
    else    std::cout << "[IDLE TIMEOUT TEST FAIL]\n";

    return ok ? 0 : 1;
}
