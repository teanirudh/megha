#include <iostream>
#include <string>

#include "config/experiment_config.hpp"
#include "core/experiment_runner.hpp"

using namespace kumo;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <config-file>\n";
        return 1;
    }

    std::string config_path = argv[1];

    try {
        ExperimentConfig cfg = load_experiment_config(config_path);
        std::cout << "Loaded config from " << config_path << "\n"
                  << "  scheduler=" << cfg.scheduler_name << "\n"
                  << "  workload="  << cfg.workload_type << "\n"
                  << "  tenants="   << cfg.num_tenants << "\n"
                  << "  workers="   << cfg.num_workers << "\n"
                  << "  total_invocations=" << cfg.total_invocations << "\n"
                  << "  batch_size=" << cfg.batch_size << "\n"
                  << "  time_step="  << cfg.time_step << "\n"
                  << "  seed="       << cfg.seed << "\n"
                  << "  attacker.enabled=" << (cfg.attacker_enabled ? 1 : 0) << "\n";

        ExperimentRunner::run(cfg);
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
