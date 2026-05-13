#include <iostream>

#include "common/types.hpp"
#include "core/engine.hpp"
#include "core/scenario.hpp"
#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/registry.hpp"
#include "workload/workload.hpp"

using namespace kumo;

int main()
{
    std::cout << "=== Scenario + UniformWorkload + SpreadScheduler ===\n";

    //
    // 1. Build platform: 3 tenants, 3 workers
    //
    const std::uint32_t num_tenants = 3;

    PlatformState ps;
    ResourceConfig func_res{
        .cpu_cores = 0.25, .memory_mb = 64, .storage_mb = 5};

    for (std::uint32_t i = 0; i < num_tenants; ++i)
    {
        FunctionProfile f;
        f.id = 1 + i;
        f.tenant = 1 + i;
        f.owner = 1 + i;
        f.name = "func-" + std::to_string(f.id);
        f.resources = func_res;
        ps.add_function(f);
    }

    ResourceConfig cap{.cpu_cores = 4.0, .memory_mb = 4096, .storage_mb = 200};
    ps.add_worker(cap); // 0
    ps.add_worker(cap); // 1
    ps.add_worker(cap); // 2

    //
    // 2. Create scheduler via registry
    //
    auto &reg = SchedulerRegistry::instance();
    auto sched = reg.create("spread", /*seed*/ 123);

    Engine engine(std::move(ps), std::move(sched));

    //
    // 3. Create workload and scenario
    //
    auto workload = std::make_unique<UniformWorkload>(
        /*num_tenants=*/num_tenants,
        /*total_invocations=*/30,
        /*max_batch_size=*/5,
        /*base_func_id=*/1,
        /*base_tenant_id=*/1,
        /*seed=*/42);
    workload->set_default_service_time(20.0);

    SingleWorkloadScenario scenario(std::move(engine), std::move(workload),
                                    /*time_step=*/10.0);

    //
    // 4. Run scenario
    //
    scenario.run();

    //
    // 5. Inspect results
    //
    const auto &eng = scenario.engine();
    const auto &ps_final = eng.platform();
    const auto &m = eng.metrics();

    std::cout << "\n=== Results ===\n";
    std::cout << "Sim time: " << eng.now() << "\n";
    std::cout << "Failed invocations: " << eng.num_failed_invocations() << "\n";

    std::cout << "\nWorker tenant presence:\n";
    for (WorkerId wid : ps_final.workers())
    {
        const auto &w = ps_final.worker_view(wid);
        std::cout << "  Worker " << wid << ": tenants = { ";
        for (auto t : w.tenants_present)
            std::cout << t << " ";
        std::cout << "} active=" << w.active_invocations
                  << " cpu_used=" << w.used.cpu_cores << "\n";
    }

    std::cout << "\nPer-tenant invocation counts:\n";
    for (auto &kv : m.tenant_invocations())
    {
        std::cout << "  tenant " << kv.first << " -> " << kv.second << "\n";
    }

    std::cout << "\nCo-location counts:\n";
    for (auto &kv : m.colocations())
    {
        auto p = kv.first;
        std::cout << "  (" << p.first << "," << p.second << ") -> " << kv.second
                  << "\n";
    }

    std::cout << "\n[SCENARIO TEST COMPLETED]\n";
    return 0;
}
