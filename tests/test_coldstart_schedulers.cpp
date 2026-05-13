#include <iostream>
#include <string>
#include <vector>

#include "common/types.hpp"
#include "core/engine.hpp"
#include "core/scenario.hpp"
#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/registry.hpp"
#include "workload/workload.hpp"

using namespace kumo;

PlatformState make_platform(std::uint32_t num_workers)
{
    PlatformState ps;

    // One function, single tenant.
    FunctionProfile f;
    f.id = 1;
    f.tenant = 1;
    f.owner = 1;
    f.name = "coldwarm-func";
    f.resources =
        ResourceConfig{.cpu_cores = 0.2, .memory_mb = 64, .storage_mb = 5};

    // Distinct cold vs warm times.
    f.cold_start_time = 10.0;
    f.warm_start_time = 1.0;
    f.idle_timeout = 0.0; // no timeout yet
    ps.add_function(f);

    ResourceConfig cap{.cpu_cores = 4.0, .memory_mb = 4096, .storage_mb = 200};
    for (std::uint32_t i = 0; i < num_workers; ++i)
    {
        ps.add_worker(cap);
    }

    return ps;
}

void run_for_scheduler(const std::string &name)
{
    std::cout << "\n=== Scheduler: " << name << " ===\n";

    const std::uint32_t num_workers = 4;
    const std::uint64_t total_inv = 200;
    const std::uint32_t batch_size = 10;
    const double time_step = 5.0;
    const std::uint64_t base_seed = 1234;

    PlatformState ps = make_platform(num_workers);

    auto &reg = SchedulerRegistry::instance();
    if (!reg.has(name))
    {
        std::cout << "  [SKIP] unknown scheduler name\n";
        return;
    }

    auto sched = reg.create(name, base_seed);

    Engine engine(std::move(ps), std::move(sched));

    auto workload = std::make_unique<UniformWorkload>(
        /*num_tenants=*/1,
        /*total_invocations=*/total_inv,
        /*max_batch_size=*/batch_size,
        /*base_func_id=*/1,
        /*base_tenant_id=*/1,
        /*seed=*/base_seed + 1);
    workload->set_default_service_time(5.0);

    SingleWorkloadScenario scenario(std::move(engine), std::move(workload),
                                    /*time_step=*/time_step);

    scenario.run();

    const auto &eng = scenario.engine();
    const auto &m = eng.metrics();

    FunctionId fid = 1;
    std::uint64_t cold = m.cold_starts_for(fid);
    std::uint64_t warm = m.warm_starts_for(fid);

    std::cout << "Sim time: " << eng.now() << "\n";
    std::cout << "Total invocations: " << (cold + warm) << "\n";
    std::cout << "Cold starts: " << cold << "\n";
    std::cout << "Warm starts: " << warm << "\n";
    std::cout << "Failed: " << eng.num_failed_invocations() << "\n";
}

int main()
{
    std::cout << "=== Cold/Warm Comparison Across Schedulers ===\n";

    std::vector<std::string> schedulers = {
        "random", "spread", "helper", "openwhisk", "openwhisk_warm", "pasch"};

    for (const auto &name : schedulers)
    {
        run_for_scheduler(name);
    }

    std::cout << "\n[COLD/WARM SCHEDULER TEST COMPLETED]\n";
    return 0;
}
