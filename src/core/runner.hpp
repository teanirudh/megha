#pragma once

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "../config.hpp"
#include "../model/app.hpp"
#include "../model/platform_state.hpp"
#include "../scheduler/registry.hpp"
#include "../workload/attack_workload.hpp"
#include "../workload/poisson_workload.hpp"
#include "../workload/workload.hpp"
#include "engine.hpp"
#include "logger.hpp"
#include "scenario.hpp"

namespace megha
{

class ExperimentRunner
{
  public:
    static void run(const ExperimentConfig &cfg)
    {
        std::cout << "\n" << std::string(80, '=') << "\n";

        std::cout << "\nexperiment:\n";
        {
            std::ostringstream o;
            for (std::size_t i = 0; i < cfg.sweep_schedulers.size(); ++i)
                o << (i ? "," : "") << cfg.sweep_schedulers[i];
            print_config_kv("schedulers", o.str());
        }
        {
            std::ostringstream o;
            for (std::size_t i = 0; i < cfg.sweep_seeds.size(); ++i)
                o << (i ? "," : "") << cfg.sweep_seeds[i];
            print_config_kv("seeds", o.str());
        }

        std::cout << "\nplatform:\n";
        print_config_kv("num_tenants", cfg.num_tenants);
        print_config_kv("num_workers", cfg.num_workers);
        print_config_kv("total_invocations", cfg.total_invocations);
        print_config_kv("functions_per_tenant", cfg.functions_per_tenant);
        print_config_kv("batch_size", cfg.batch_size);

        std::cout << "\nworkload:\n";
        print_config_kv("workload", cfg.workload_type);
        print_config_kv("mean_service_time", cfg.mean_service_time);
        print_config_kv("arrival_rate", cfg.arrival_rate);
        print_config_kv("idle_timeout", cfg.idle_timeout);
        print_config_kv("time_step", cfg.time_step);

        std::cout << "\nattacker:\n";
        print_config_kv("attacker.enabled", cfg.attacker_enabled);
        print_config_kv("attacker.tenant", cfg.attacker_cfg.attacker_tenant);
        print_config_kv("attacker.function",
                        cfg.attacker_cfg.attacker_function);
        print_config_kv("attacker.intensity", cfg.attacker_cfg.intensity);

        std::cout << "\nworker:\n";
        print_config_kv("worker.cpu_cores", cfg.worker_cpu_cores);
        print_config_kv("worker.memory_mb", cfg.worker_memory_mb);
        print_config_kv("worker.storage_mb", cfg.worker_storage_mb);
        print_config_kv("worker.queue_size", cfg.worker_queue_size);

        const bool has_sweep =
            !cfg.sweep_schedulers.empty() || !cfg.sweep_seeds.empty();
        std::vector<std::string> scheds =
            cfg.sweep_schedulers.empty()
                ? std::vector<std::string>{cfg.scheduler_name}
                : cfg.sweep_schedulers;
        std::vector<std::uint64_t> seeds =
            cfg.sweep_seeds.empty() ? std::vector<std::uint64_t>{cfg.seed}
                                    : cfg.sweep_seeds;

        if (!cfg.trace_file.empty())
            TraceLogger::init(cfg.trace_file);

        auto start = std::chrono::steady_clock::now();
        if (!has_sweep)
        {
            log_trace_run_header(cfg);
            run_single(cfg);
        }
        else
        {
            for (const auto &s : scheds)
            {
                for (auto sd : seeds)
                {
                    ExperimentConfig c = cfg;
                    c.scheduler_name = s;
                    c.seed = sd;
                    std::cout << "\n" << std::string(80, '-') << "\n\n";
                    log_trace_run_header(c);
                    run_single(c);
                }
            }
        }
        TraceLogger::shutdown();

        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;

        std::cout << "\n" << std::string(80, '-') << "\n\n";
        std::cout << "elapsed_time: " << elapsed_seconds.count() << "\n";
        std::cout << "\n" << std::string(80, '=') << "\n\n";
    }

  private:
    template <typename T>
    static void print_config_kv(const char *key, const T &val)
    {
        constexpr int kKeyW = 24;
        std::cout << "  " << std::left << std::setw(kKeyW) << key << "= " << val
                  << '\n';
    }

    static void log_trace_run_header(const ExperimentConfig &cfg)
    {
        if (!TraceLogger::enabled())
        {
            return;
        }
        constexpr std::size_t kLine = 80;
        TraceLogger::log(std::string(kLine, '='));
        TraceLogger::log("[seed-", cfg.seed, "] ",
                         "scheduler: ", cfg.scheduler_name);
        TraceLogger::log(std::string(kLine, '='));
    }

    static void run_single(const ExperimentConfig &cfg)
    {
        // 1. Build platform
        PlatformState ps;

        // Victim functions
        ResourceConfig func_res{
            .cpu_cores = 0.2, .memory_mb = 64, .storage_mb = 5};

        for (std::uint32_t t = 0; t < cfg.num_tenants; ++t)
        {
            TenantId tenant_id = static_cast<TenantId>(1 + t);
            for (std::uint32_t k = 0; k < cfg.functions_per_tenant; ++k)
            {
                FunctionId fid = static_cast<FunctionId>(
                    1 + t * cfg.functions_per_tenant + k);

                FunctionProfile f;
                f.id = fid;
                f.tenant = tenant_id;
                f.name = "func-" + std::to_string(f.id);
                f.resources = func_res;
                f.cold_start_time = 10.0;
                f.warm_start_time = 1.0;
                f.idle_timeout = cfg.idle_timeout;
                ps.add_function(f);
            }
        }

        // Attacker function (if enabled)
        if (cfg.attacker_enabled)
        {
            FunctionProfile f_att;
            f_att.id = cfg.attacker_cfg.attacker_function;
            f_att.tenant = cfg.attacker_cfg.attacker_tenant;
            f_att.name = "attacker-func";
            f_att.resources = func_res;
            f_att.cold_start_time = 10.0;
            f_att.warm_start_time = 1.0;
            f_att.idle_timeout = cfg.idle_timeout;
            f_att.microarch = MicroarchProfile{
                .llc_loads_per_cycle = 0.08,
                .llc_stores_per_cycle = 0.002,
                .llc_load_misses_per_cycle = 0.05,
                .llc_store_misses_per_cycle = 0.00001,
                .instructions_per_cycle = 0.333,
            };
            ps.add_function(f_att);
        }

        // Workers: homogeneous capacity
        std::uint32_t num_workers = cfg.num_workers;
        if (num_workers == 0)
        {
            throw std::runtime_error("num_workers must be > 0");
        }

        ResourceConfig base_cap{.cpu_cores = cfg.worker_cpu_cores,
                                .memory_mb = cfg.worker_memory_mb,
                                .storage_mb = cfg.worker_storage_mb};

        for (std::uint32_t i = 0; i < num_workers; ++i)
        {
            ps.add_worker(base_cap);
        }

        // 2. Scheduler
        std::cout << "[seed-" << cfg.seed << "] " << "\n";

        auto &reg = SchedulerRegistry::instance();
        if (!reg.has(cfg.scheduler_name))
            throw std::runtime_error("unknown_scheduler: " +
                                     cfg.scheduler_name);

        auto sched = reg.create(cfg.scheduler_name, cfg.seed);

        Engine engine(std::move(ps), std::move(sched));
        engine.set_max_queue_len(cfg.worker_queue_size);

        // 3. Workload
        std::unique_ptr<Workload> workload;
        {
            auto base = std::make_unique<PoissonWorkload>(
                cfg.num_tenants, cfg.total_invocations, cfg.batch_size,
                cfg.arrival_rate, 1, 1, cfg.seed + 1, cfg.functions_per_tenant);
            // Baseline service times: exponential with mean_service_time.
            base->set_service_time_exponential(cfg.mean_service_time);

            if (cfg.attacker_enabled)
            {
                AttackConfig acfg = cfg.attacker_cfg;
                if (acfg.victims.empty())
                    acfg.victims = {1};
                workload = std::make_unique<AttackWorkload>(std::move(base),
                                                            acfg, cfg.seed + 2);
            }
            else
            {
                workload = std::move(base);
            }
        }

        // 4. Scenario
        SingleWorkloadScenario scenario(std::move(engine), std::move(workload),
                                        cfg.time_step);

        scenario.run();

        // 5. Summary
        const auto &eng = scenario.engine();
        const auto &m = eng.metrics();

        print_config_kv("scheduler", cfg.scheduler_name);
        print_config_kv("simulation_time", eng.now());
        print_config_kv("total_arrivals", m.arrivals_total());
        print_config_kv("total_drops", m.drops_total());

        if (!cfg.results_file.empty())
        {
            bool write_header = false;
            {
                std::ifstream check(cfg.results_file);
                write_header = !check.good();
            }

            std::ofstream out(cfg.results_file, std::ios::app);
            if (!out)
            {
                throw std::runtime_error("Cannot open CSV file: " +
                                         cfg.results_file);
            }

            if (write_header)
            {
                out << "seed,scheduler,num_tenants,num_workers,"
                    << "mean_service_time,arrival_rate,attacker.enabled,"
                    << "attacker.intensity,simulation_time,total_arrivals,"
                    << "total_drops,benign_cold_count,benign_warm_count,"
                    << "colocation_count,victim_tenant,victim_arrivals,"
                    << "victim_drops,victim_tail_latency\n";
            }

            TenantId victim_tenant = 1;
            if (!cfg.attacker_cfg.victims.empty())
                victim_tenant = cfg.attacker_cfg.victims.front();
            const TenantId attacker_tenant = cfg.attacker_cfg.attacker_tenant;

            const auto total_arrivals_all = m.arrivals_total();

            const auto victim_arr = m.arrivals_for_tenant(victim_tenant);
            const auto victim_drop = m.drops_for_tenant(victim_tenant);
            const double victim_tail_lat =
                m.tail_latency_for_tenant(victim_tenant);

            const FunctionId attacker_fid =
                cfg.attacker_enabled ? cfg.attacker_cfg.attacker_function : 0;
            std::uint64_t benign_cold = 0;
            std::uint64_t benign_warm = 0;
            for (const auto &kv : m.function_cold_starts())
            {
                if (cfg.attacker_enabled && kv.first == attacker_fid)
                    continue;
                benign_cold += kv.second;
            }
            for (const auto &kv : m.function_warm_starts())
            {
                if (cfg.attacker_enabled && kv.first == attacker_fid)
                    continue;
                benign_warm += kv.second;
            }

            const std::uint64_t coloc_va =
                m.colocation_count(victim_tenant, attacker_tenant);

            out << cfg.seed << "," << cfg.scheduler_name << ","
                << cfg.num_tenants << "," << cfg.num_workers << ","
                << cfg.mean_service_time << "," << cfg.arrival_rate << ","
                << (cfg.attacker_enabled ? 1 : 0) << ","
                << cfg.attacker_cfg.intensity << "," << eng.now() << ","
                << total_arrivals_all << "," << m.drops_total() << ","
                << benign_cold << "," << benign_warm << "," << coloc_va << ","
                << victim_tenant << "," << victim_arr << "," << victim_drop
                << "," << victim_tail_lat << "\n";
        }
    }
};

} // namespace megha
