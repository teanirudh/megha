#include <iostream>

#include "common/types.hpp"
#include "model/app.hpp"
#include "model/container.hpp"
#include "model/platform_state.hpp"

using namespace kumo;

int main()
{
    std::cout << "=== Container + PlatformState Sanity Test ===\n";

    PlatformState ps;

    // Define a simple function profile (not really used yet, but good to check).
    FunctionProfile f;
    f.id = 1;
    f.tenant = 1;
    f.owner = 1;
    f.name = "test-func";
    f.resources =
        ResourceConfig{.cpu_cores = 0.5, .memory_mb = 128, .storage_mb = 10};
    f.cold_start_time = 100.0;
    f.warm_start_time = 10.0;
    f.idle_timeout = 1000.0;

    ps.add_function(f);

    // Add one worker
    ResourceConfig cap{.cpu_cores = 2.0, .memory_mb = 1024, .storage_mb = 100};
    WorkerId w0 = ps.add_worker(cap);

    // Attach a container by hand to that worker
    auto &wv = ps.mutable_worker(w0);

    Container c;
    c.function_id = f.id;
    c.worker_id = w0;
    c.busy = false;
    c.last_used = 0.0;
    c.lifetime_invocations = 0;

    wv.containers.push_back(c);

    std::cout << "Worker " << w0 << " capacity cpu=" << wv.capacity.cpu_cores
              << ", mem=" << wv.capacity.memory_mb << "\n";
    std::cout << "Worker " << w0 << " containers: " << wv.containers.size()
              << "\n";
    if (!wv.containers.empty())
    {
        const auto &cc = wv.containers.front();
        std::cout << "  container[0]: func=" << cc.function_id
                  << ", worker=" << cc.worker_id << ", busy=" << cc.busy
                  << ", last_used=" << cc.last_used << "\n";
    }

    std::cout << "[CONTAINER TEST COMPLETED]\n";
    return 0;
}
