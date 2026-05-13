#include <iomanip>
#include <iostream>

#include "common/types.hpp"
#include "core/engine.hpp"
#include "core/scenario.hpp"
#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/registry.hpp"
#include "workload/attack_workload.hpp"
#include "workload/workload.hpp"

using namespace kumo;

void print_metrics(const MetricsCollector &m)
{
    std::cout << "Per-tenant invocation counts:\n";
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
}

//
// Build a minimal platform with:
//   - One victim tenant (1) with func 1
//   - One attacker tenant (999) with func 100
//   - Two workers
//
PlatformState make_platform()
{
    PlatformState ps;

    // victim function
    FunctionProfile f_vic;
    f_vic.id = 1;
    f_vic.tenant = 1;
    f_vic.owner = 1;
    f_vic.name = "victim-func";
    f_vic.resources = {.cpu_cores = 0.3, .memory_mb = 64, .storage_mb = 5};
    ps.add_function(f_vic);

    // attacker function
    FunctionProfile f_att;
    f_att.id = 100;
    f_att.tenant = 999;
    f_att.owner = 999;
    f_att.name = "attacker-func";
    f_att.resources = {.cpu_cores = 0.3, .memory_mb = 64, .storage_mb = 5};
    ps.add_function(f_att);

    // two workers
    ResourceConfig cap{.cpu_cores = 4.0, .memory_mb = 2048, .storage_mb = 200};
    ps.add_worker(cap); // worker 0
    ps.add_worker(cap); // worker 1

    return ps;
}

// Run a scenario using the given scheduler name and return metrics.
MetricsCollector run_attack_scenario(const std::string &scheduler_name)
{
    std::cout << "\n=== Running " << scheduler_name << " ===\n";

    // 1. Baseline victim workload
    auto base = std::make_unique<UniformWorkload>(
        /*num_tenants=*/1,
        /*total_invocations=*/50,
        /*max_batch_size=*/5,
        /*base_func_id=*/1,
        /*base_tenant_id=*/1,
        /*seed=*/1234);
    base->set_default_service_time(20.0);

    // 2. Attack config
    AttackConfig cfg;
    cfg.attacker_tenant = 999;
    cfg.attacker_function = 100;
    cfg.victims = {1};         // attack tenant 1
    cfg.attack_per_victim = 1; // ~1 attack per victim invocation

    // 3. Wrap baseline workload in AttackWorkload
    auto attack_wl =
        std::make_unique<AttackWorkload>(std::move(base), cfg, /*seed=*/777);

    // 4. Build platform
    PlatformState ps = make_platform();

    // 5. Scheduler via registry
    auto &reg = SchedulerRegistry::instance();
    auto sched = reg.create(scheduler_name, /*seed=*/999);

    // 6. Engine + scenario
    Engine engine(std::move(ps), std::move(sched));
    SingleWorkloadScenario scenario(std::move(engine), std::move(attack_wl),
                                    /*time_step=*/10.0);

    scenario.run();

    return scenario.engine().metrics();
}

int main()
{
    // Compare random vs spread
    auto m_random = run_attack_scenario("random");
    auto m_spread = run_attack_scenario("spread");

    std::cout << "\n--- Random Scheduler Metrics ---\n";
    print_metrics(m_random);

    std::cout << "\n--- Spread Scheduler Metrics ---\n";
    print_metrics(m_spread);

    // Core sanity check: random should produce *much higher* co-location
    // between tenant 1 and tenant 999 than spread.
    auto col_random = m_random.colocation_count(1, 999);
    auto col_spread = m_spread.colocation_count(1, 999);

    std::cout << "\nExpected: Random co-location >> Spread co-location\n";
    std::cout << "Random co-location = " << col_random << "\n";
    std::cout << "Spread co-location = " << col_spread << "\n";

    if (col_random > col_spread)
    {
        std::cout << "\n[ATTACK TEST PASS]\n";
        return 0;
    }
    else
    {
        std::cout << "\n[ATTACK TEST FAIL: Spread should reduce co-location]\n";
        return 1;
    }
}
