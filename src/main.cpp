#include <iostream>
#include <string>
#include <cstdlib>

#include "common/types.hpp"
#include "model/app.hpp"
#include "model/platform_state.hpp"
#include "scheduler/registry.hpp"
#include "workload/workload.hpp"
#include "core/engine.hpp"
#include "core/scenario.hpp"

using namespace kumo;

namespace {

// Very tiny argument parser: expects flags like --key=value
std::string get_arg(int argc, char** argv, const std::string& key,
                    const std::string& def) {
    const std::string prefix = "--" + key + "=";
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind(prefix, 0) == 0) {
            return s.substr(prefix.size());
        }
    }
    return def;
}

std::uint32_t get_arg_u32(int argc, char** argv, const std::string& key,
                          std::uint32_t def) {
    std::string val = get_arg(argc, argv, key, std::to_string(def));
    return static_cast<std::uint32_t>(std::stoul(val));
}

std::uint64_t get_arg_u64(int argc, char** argv, const std::string& key,
                          std::uint64_t def) {
    std::string val = get_arg(argc, argv, key, std::to_string(def));
    return static_cast<std::uint64_t>(std::stoull(val));
}

double get_arg_f64(int argc, char** argv, const std::string& key,
                   double def) {
    std::string val = get_arg(argc, argv, key, std::to_string(def));
    return std::stod(val);
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --scheduler=name        Scheduler: random, spread, openwhisk,\n"
              << "                          openwhisk_warm, helper, pasch (default: spread)\n"
              << "  --num-tenants=N        Number of tenants (default: 3)\n"
              << "  --num-workers=N        Number of workers (default: 3)\n"
              << "  --total-invocations=N  Total invocations (default: 30)\n"
              << "  --batch-size=N         Max batch size per step (default: 5)\n"
              << "  --time-step=T          Time step per batch (default: 10.0)\n"
              << "  --seed=S               RNG seed (default: 123)\n"
              << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    // Parse arguments
    if (argc > 1 && std::string(argv[1]) == "--help") {
        print_usage(argv[0]);
        return 0;
    }

    std::string scheduler_name = get_arg(argc, argv, "scheduler", "spread");
    std::uint32_t num_tenants  = get_arg_u32(argc, argv, "num-tenants", 3);
    std::uint32_t num_workers  = get_arg_u32(argc, argv, "num-workers", 3);
    std::uint64_t total_inv    = get_arg_u64(argc, argv, "total-invocations", 30);
    std::uint32_t batch_size   = get_arg_u32(argc, argv, "batch-size", 5);
    double time_step           = get_arg_f64(argc, argv, "time-step", 10.0);
    std::uint64_t seed         = get_arg_u64(argc, argv, "seed", 123);

    std::cout << "Kumo simulator\n"
              << "  scheduler: " << scheduler_name << "\n"
              << "  tenants: " << num_tenants << "\n"
              << "  workers: " << num_workers << "\n"
              << "  total invocations: " << total_inv << "\n"
              << "  batch size: " << batch_size << "\n"
              << "  time step: " << time_step << "\n"
              << "  seed: " << seed << "\n\n";

    // 1. Build platform
    PlatformState ps;

    ResourceConfig func_res{ .cpu_cores = 0.25, .memory_mb = 64, .storage_mb = 5 };

    for (std::uint32_t i = 0; i < num_tenants; ++i) {
        FunctionProfile f;
        f.id        = 1 + i;
        f.tenant    = 1 + i;
        f.owner     = 1 + i;
        f.name      = "func-" + std::to_string(f.id);
        f.resources = func_res;
        ps.add_function(f);
    }

    ResourceConfig cap{ .cpu_cores = 4.0, .memory_mb = 4096, .storage_mb = 200 };
    for (std::uint32_t i = 0; i < num_workers; ++i) {
        ps.add_worker(cap);
    }

    // 2. Create scheduler via registry
    auto& reg = SchedulerRegistry::instance();
    if (!reg.has(scheduler_name)) {
        std::cerr << "Unknown scheduler: " << scheduler_name << "\n";
        print_usage(argv[0]);
        return 1;
    }

    auto sched = reg.create(scheduler_name, seed);
    Engine engine(std::move(ps), std::move(sched));

    // 3. Create workload & scenario
    auto workload = std::make_unique<UniformWorkload>(
        /*num_tenants=*/num_tenants,
        /*total_invocations=*/total_inv,
        /*max_batch_size=*/batch_size,
        /*base_func_id=*/1,
        /*base_tenant_id=*/1,
        /*seed=*/seed + 1
    );
    workload->set_default_service_time(20.0);

    SingleWorkloadScenario scenario(
        std::move(engine),
        std::move(workload),
        /*time_step=*/time_step
    );

    // 4. Run scenario
    scenario.run();

    // 5. Print a concise summary
    const auto& eng = scenario.engine();
    const auto& ps_final = eng.platform();
    const auto& m = eng.metrics();

    std::cout << "=== Simulation Summary ===\n";
    std::cout << "Sim time: " << eng.now() << "\n";
    std::cout << "Failed invocations: " << eng.num_failed_invocations() << "\n";

    std::cout << "\nPer-tenant invocation counts:\n";
    for (auto& kv : m.tenant_invocations()) {
        std::cout << "  tenant " << kv.first << " -> " << kv.second << "\n";
    }

    std::cout << "\nWorker tenant presence:\n";
    for (WorkerId wid : ps_final.workers()) {
        const auto& w = ps_final.worker_view(wid);
        std::cout << "  worker " << wid << ": tenants = { ";
        for (auto t : w.tenants_present) std::cout << t << " ";
        std::cout << "}\n";
    }

    std::cout << "\nCo-location counts:\n";
    for (auto& kv : m.colocations()) {
        auto p = kv.first;
        std::cout << "  (" << p.first << "," << p.second << ") -> " << kv.second << "\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}
