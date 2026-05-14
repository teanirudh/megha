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
        constexpr int kKeyW = 24;
        auto ckv = [kKeyW](const char *key, const auto &val)
        {
            std::cout << "  " << std::left << std::setw(kKeyW) << key << "= "
                      << val << '\n';
        };

        std::cout << "\n" << std::string(80, '=') << "\n";

        std::cout << "\nconfiguration:\n";
        ckv("workload", cfg.workload_type);
        ckv("num_tenants", cfg.num_tenants);
        ckv("num_workers", cfg.num_workers);
        ckv("total_invocations", cfg.total_invocations);
        ckv("functions_per_tenant", cfg.functions_per_tenant);
        ckv("batch_size", cfg.batch_size);

        std::cout << "\nworkers:\n";
        ckv("worker.cpu_cores", cfg.worker_cpu_cores);
        ckv("worker.memory_mb", cfg.worker_memory_mb);
        ckv("worker.storage_mb", cfg.worker_storage_mb);

        std::cout << "\nattacker:\n";
        ckv("attacker.enabled", cfg.attacker_enabled);
        ckv("attacker.tenant", cfg.attacker_cfg.attacker_tenant);
        ckv("attacker.function", cfg.attacker_cfg.attacker_function);
        ckv("attacker.pattern", cfg.attacker_cfg.pattern);

        std::cout << "\nsweep:\n";
        {
            std::ostringstream o;
            for (std::size_t i = 0; i < cfg.sweep_schedulers.size(); ++i)
                o << (i ? "," : "") << cfg.sweep_schedulers[i];
            ckv("sweep.schedulers", o.str());
        }
        {
            std::ostringstream o;
            for (std::size_t i = 0; i < cfg.sweep_seeds.size(); ++i)
                o << (i ? "," : "") << cfg.sweep_seeds[i];
            ckv("sweep.seeds", o.str());
        }

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
        constexpr int kKeyW = 24;
        auto ckv = [kKeyW](const char *key, const auto &val)
        {
            std::cout << "  " << std::left << std::setw(kKeyW) << key << "= "
                      << val << '\n';
        };

        std::cout << "[seed-" << cfg.seed << "] " << "\n";

        auto &reg = SchedulerRegistry::instance();
        if (!reg.has(cfg.scheduler_name))
            throw std::runtime_error("unknown_scheduler: " +
                                     cfg.scheduler_name);

        auto sched = reg.create(cfg.scheduler_name, cfg.seed);

        Engine engine(std::move(ps), std::move(sched));
        engine.set_max_queue_len(cfg.worker_queue.max_queue_len);

        // 3. Workload
        std::unique_ptr<Workload> workload;
        {
            auto base = std::make_unique<PoissonWorkload>(
                cfg.num_tenants, cfg.total_invocations, cfg.batch_size,
                cfg.arrival_rate, 1, 1, cfg.seed + 1, cfg.functions_per_tenant);
            if (cfg.service_time.model == ServiceTimeModel::FIXED)
            {
                base->set_service_time_fixed(cfg.service_time.fixed);
            }
            else
            {
                base->set_service_time_exponential(cfg.service_time.mean);
            }

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

        ckv("scheduler", cfg.scheduler_name);
        ckv("simulation_time", eng.now());
        ckv("total_arrivals", m.arrivals_total());
        ckv("total_drops", m.drops_total());

        if (!cfg.results_file.empty())
        {
            bool write_header = false;

            {
                std::ifstream check(cfg.results_file);
                write_header = !check.good(); // header if file doesn't exist
            }

            std::ofstream out(cfg.results_file, std::ios::app);
            if (!out)
            {
                throw std::runtime_error("Cannot open CSV file: " +
                                         cfg.results_file);
            }

            if (write_header)
            {
                out << "seed,num_tenants,num_workers,total_invocations,"
                    << "functions_per_tenant,batch_size,scheduler,workload,"
                    << "attacker.enabled,attacker.tenant,attacker.function,"
                    << "attacker.attack_intensity,attacker.pattern,"
                    << "attacker.victims,attacker.total_invocations,"
                    << "sweep.schedulers,sweep.seeds,"
                    << "arrival_rate,idle_timeout,time_step,"
                    << "worker.cpu_cores,worker.memory_mb,worker.storage_mb,"
                    << "trace_file,results_file,"
                    << "service_time.model,service_time.mean,service_time."
                       "fixed,"
                    << "worker.max_queue_len,"
                    << "simulation_time,total_arrivals,total_drops,cold_count,"
                    << "warm_count,colocation_count,time_to_colocation,"
                    << "victim_tenant,victim_arrivals,victim_drops,"
                    << "victim_drop_rate,attacker_tenant,attacker_arrivals,"
                    << "attacker_drops,attacker_drop_rate,victim_mean_latency,"
                    << "victim_tail_latency\n";
            }

            TenantId victim_tenant = 1;
            TenantId attacker_tenant = cfg.attacker_cfg.attacker_tenant;
            std::uint64_t cold = 0;
            std::uint64_t warm = 0;

            auto total_arrivals_all = m.arrivals_total();

            auto victim_arr = m.arrivals_for_tenant(victim_tenant);
            auto victim_drop = m.drops_for_tenant(victim_tenant);
            double victim_mean_lat = m.mean_latency_for_tenant(victim_tenant);
            double victim_tail_lat = m.tail_latency_for_tenant(victim_tenant);
            for (auto &kv : m.tenant_invocations())
            {
                cold += m.cold_starts_for(kv.first);
                warm += m.warm_starts_for(kv.first);
            }

            std::uint64_t coloc_va =
                m.colocation_count(victim_tenant, attacker_tenant);

            double ttf_coloc =
                m.first_colocation_time(victim_tenant, attacker_tenant);

            double victim_drop_rate =
                (victim_arr == 0)
                    ? 0.0
                    : static_cast<double>(victim_drop) / victim_arr;

            auto attacker_arr = m.arrivals_for_tenant(attacker_tenant);
            auto attacker_drop = m.drops_for_tenant(attacker_tenant);
            double attacker_drop_rate =
                attacker_arr ? (double)attacker_drop / attacker_arr : 0.0;

            const std::string st_model =
                cfg.service_time.model == ServiceTimeModel::FIXED
                    ? "fixed"
                    : "exponential";

            std::ostringstream victims_ss;
            for (std::size_t i = 0; i < cfg.attacker_cfg.victims.size(); ++i)
            {
                if (i > 0)
                    victims_ss << ',';
                victims_ss << cfg.attacker_cfg.victims[i];
            }
            std::ostringstream sweep_sched_ss;
            for (std::size_t i = 0; i < cfg.sweep_schedulers.size(); ++i)
            {
                if (i > 0)
                    sweep_sched_ss << ',';
                sweep_sched_ss << cfg.sweep_schedulers[i];
            }
            std::ostringstream sweep_seeds_ss;
            for (std::size_t i = 0; i < cfg.sweep_seeds.size(); ++i)
            {
                if (i > 0)
                    sweep_seeds_ss << ',';
                sweep_seeds_ss << cfg.sweep_seeds[i];
            }

            out << cfg.seed << "," << cfg.num_tenants << "," << cfg.num_workers
                << "," << cfg.total_invocations << ","
                << cfg.functions_per_tenant << "," << cfg.batch_size << ","
                << cfg.scheduler_name << "," << cfg.workload_type << ","
                << (cfg.attacker_enabled ? 1 : 0) << ","
                << cfg.attacker_cfg.attacker_tenant << ","
                << cfg.attacker_cfg.attacker_function << ","
                << cfg.attacker_cfg.attack_intensity << ","
                << cfg.attacker_cfg.pattern << "," << victims_ss.str() << ","
                << cfg.attacker_cfg.total_invocations << ",\""
                << sweep_sched_ss.str() << "\",\"" << sweep_seeds_ss.str()
                << "\"," << cfg.arrival_rate << "," << cfg.idle_timeout << ","
                << cfg.time_step << "," << cfg.worker_cpu_cores << ","
                << cfg.worker_memory_mb << "," << cfg.worker_storage_mb << ","
                << cfg.trace_file << "," << cfg.results_file << "," << st_model
                << "," << cfg.service_time.mean << "," << cfg.service_time.fixed
                << "," << cfg.worker_queue.max_queue_len << "," << eng.now()
                << "," << total_arrivals_all << "," << m.drops_total() << ","
                << cold << "," << warm << "," << coloc_va << "," << ttf_coloc
                << "," << victim_tenant << "," << victim_arr << ","
                << victim_drop << "," << victim_drop_rate << ","
                << attacker_tenant << "," << attacker_arr << ","
                << attacker_drop << "," << attacker_drop_rate << ","
                << victim_mean_lat << "," << victim_tail_lat << "\n";
        }
    }
};

} // namespace megha
