#pragma once

#include <iostream>
#include <memory>

#include "config/experiment_config.hpp"
#include "model/platform_state.hpp"
#include "model/app.hpp"
#include "scheduler/registry.hpp"
#include "workload/workload.hpp"
#include "workload/attack_workload.hpp"
#include "core/engine.hpp"
#include "core/scenario.hpp"
#include "workload/poisson_workload.hpp"
#include "workload/burst_workload.hpp"
#include "core/trace_logger.hpp"


namespace kumo {

class ExperimentRunner {
public:
    static void run(const ExperimentConfig& cfg) {
        // record elapsed time
        bool has_sweep =
            !cfg.sweep_schedulers.empty() ||
            !cfg.sweep_seeds.empty() ||
            !cfg.sweep_idle_timeouts.empty() ||
            !cfg.sweep_attack_intensities.empty() ||
            !cfg.sweep_attacker_patterns.empty() ||
            !cfg.sweep_num_workers.empty() ||
            !cfg.sweep_max_queue_lens.empty();
        if (!has_sweep) {
            // record elapsed time
            auto start = std::chrono::steady_clock::now();
            run_single(cfg);
            auto end =  std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed_seconds = end - start;
            std::cout << "Elapsed time: " << elapsed_seconds.count() << "s\n";
            return;
        }

        // Construct sweep domains
        std::vector<std::string> scheds =
            cfg.sweep_schedulers.empty()
                ? std::vector<std::string>{cfg.scheduler_name}
                : cfg.sweep_schedulers;

        std::vector<std::uint64_t> seeds =
            cfg.sweep_seeds.empty()
                ? std::vector<std::uint64_t>{cfg.seed}
                : cfg.sweep_seeds;

        std::vector<double> timeouts =
            cfg.sweep_idle_timeouts.empty()
                ? std::vector<double>{cfg.idle_timeout}
                : cfg.sweep_idle_timeouts;

        std::vector<double> attack_intensities =
            cfg.sweep_attack_intensities.empty()
                ? std::vector<double>{ cfg.attacker_enabled ? cfg.attacker_cfg.attack_per_victim : 0.0 }
                : cfg.sweep_attack_intensities;

        std::vector<std::string> attacker_patterns =
            cfg.sweep_attacker_patterns.empty()
                ? std::vector<std::string>{ cfg.attacker_enabled ? cfg.attacker_cfg.pattern : "poisson" }
                : cfg.sweep_attacker_patterns;

        std::vector<std::uint32_t> num_workers =
            cfg.sweep_num_workers.empty()
                ? std::vector<std::uint32_t>{cfg.num_workers}
                : cfg.sweep_num_workers;

        std::vector<std::size_t> max_queue_lens =
            cfg.sweep_max_queue_lens.empty()
                ? std::vector<std::size_t>{cfg.worker_queue.max_queue_len}
                : cfg.sweep_max_queue_lens;

        std::cout << "Running sweep:\n";
        std::cout << "  schedulers: ";
        for (auto& s : scheds) std::cout << s << " ";
        std::cout << "\n  seeds: ";
        for (auto s : seeds) std::cout << s << " ";
        std::cout << "\n  idle_timeouts: ";
        for (auto t : timeouts) std::cout << t << " ";
        std::cout << "\n  attack_intensities: ";
        for (auto ai : attack_intensities) std::cout << ai << " ";
        std::cout << "\n  attacker_patterns: ";
        for (auto ap : attacker_patterns) std::cout << ap << " ";
        std::cout << "\n  num_workers: ";
        for (auto nw : num_workers) std::cout << nw << " ";
        std::cout << "\n max_queue_lens: ";
        for (auto mql : max_queue_lens) std::cout << mql << " ";
        std::cout << "\n";

        auto start = std::chrono::steady_clock::now();
        for (const auto& s : scheds) {
            for (auto sd : seeds) {
                for (auto to : timeouts) {
                    for (auto ai : attack_intensities) {
                        for (const auto& ap : attacker_patterns) {
                            for (auto nw : num_workers) {
                                for (auto mql : max_queue_lens) {
                                    ExperimentConfig c = cfg;
                                    c.scheduler_name = s;
                                    c.seed           = sd;
                                    c.idle_timeout   = to;
                                    c.num_workers    = nw;
                                    c.worker_queue.max_queue_len = mql;
                                    if (c.attacker_enabled) {
                                        c.attacker_cfg.attack_per_victim = ai;
                                        c.attacker_cfg.total_invocations = cfg.total_invocations;
                                        c.attacker_cfg.pattern = ap;
                                    }

                                    std::cout << "\n--- Run: scheduler=" << s
                                            << " seed=" << sd
                                            << " idle_timeout=" << to
                                            << " attack_intensity=" << ai
                                            << " attacker_pattern=" << ap
                                            << " num_workers=" << nw 
                                            << " max_queue_len=" << mql
                                            << " ---\n";
                                    run_single(c);
                                }
                            }
                        }
                    }
                }
            }
        }
        auto end =  std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "Elapsed time: " << elapsed_seconds.count() << "s\n";
    }
    private:
    static void run_single(const ExperimentConfig& cfg) {
        // 0. Tracing init (per run; we allow same path, append run info)
        if (cfg.trace_enabled) {
            std::string path = cfg.trace_file;
            if (path.empty()) {
                path = "results/kumo_trace.log";
            }
            // NOTE: for simplicity, we re-init each run which truncates.
            // If you want append-per-run, you can extend TraceLogger to support append mode.
            TraceLogger::init(path);
            TraceLogger::log("=== New run ===");
            TraceLogger::log("scheduler=", cfg.scheduler_name,
                             " workload=", cfg.workload_type,
                             " seed=", cfg.seed,
                             " idle_timeout=", cfg.idle_timeout);
        }
        // 1. Build platform
        PlatformState ps;

        // victim functions
        ResourceConfig func_res{ .cpu_cores = 0.2, .memory_mb = 64, .storage_mb = 5 };

        // Keep track of all "normal" (non-attacker) function IDs
        std::vector<FunctionId> function_ids;

        for (std::uint32_t t = 0; t < cfg.num_tenants; ++t) {
            TenantId tenant_id = static_cast<TenantId>(1 + t);
            for (std::uint32_t k = 0; k < cfg.functions_per_tenant; ++k) {
                FunctionId fid = static_cast<FunctionId>(
                    1 + t * cfg.functions_per_tenant + k
                );

                FunctionProfile f;
                f.id        = fid;
                f.tenant    = tenant_id;
                f.owner     = tenant_id;
                f.name      = "func-" + std::to_string(f.id);
                f.resources = func_res;
                f.cold_start_time = 10.0;
                f.warm_start_time = 1.0;
                f.idle_timeout    = cfg.idle_timeout;
                ps.add_function(f);

                function_ids.push_back(fid);
            }
        }

        // attacker function (if enabled)
        if (cfg.attacker_enabled) {
            FunctionProfile f_att;
            f_att.id        = cfg.attacker_cfg.attacker_function;
            f_att.tenant    = cfg.attacker_cfg.attacker_tenant;
            f_att.owner     = cfg.attacker_cfg.attacker_user;
            f_att.name      = "attacker-func";
            f_att.resources = func_res;
            f_att.cold_start_time = 10.0;
            f_att.warm_start_time = 1.0;
            f_att.idle_timeout    = cfg.idle_timeout;
            ps.add_function(f_att);
        }

        // workers: homogeneous by default, optional simple heterogeneity
        std::uint32_t num_workers = cfg.num_workers;
        if (num_workers == 0) {
            throw std::runtime_error("num_workers must be > 0");
        }

        // Base capacity
        ResourceConfig base_cap{
            .cpu_cores  = cfg.worker_cpu_cores,
            .memory_mb  = cfg.worker_memory_mb,
            .storage_mb = cfg.worker_storage_mb
        };

        // Determine how many "small" workers (if hetero enabled)
        std::uint32_t num_small = 0;
        if (cfg.hetero_enabled) {
            double frac = cfg.hetero_small_frac;
            if (frac < 0.0) frac = 0.0;
            if (frac > 1.0) frac = 1.0;
            num_small = static_cast<std::uint32_t>(std::round(frac * num_workers));
        }

        for (std::uint32_t i = 0; i < num_workers; ++i) {
            ResourceConfig cap = base_cap;

            if (cfg.hetero_enabled && i < num_small) {
                // First num_small workers are "small"
                cap.cpu_cores = base_cap.cpu_cores * cfg.hetero_small_cpu_scale;
                cap.memory_mb = static_cast<std::uint32_t>(
                    base_cap.memory_mb * cfg.hetero_small_mem_scale
                );
                // Storage kept the same for now; easy to add scale if you want
            }
            ps.add_worker(cap);
        }

        // Debug print (optional): worker capacities
        std::cout << "Worker capacities:\n";
        for (std::uint32_t i = 0; i < num_workers; ++i) {
            const auto& wv = ps.worker_view(static_cast<WorkerId>(i));
            std::cout << "  worker " << i
                        << ": cpu=" << wv.capacity.cpu_cores
                        << " mem=" << wv.capacity.memory_mb
                        << "MB storage=" << wv.capacity.storage_mb
                        << "MB\n";
        }

        // 1.5 Pre-warm containers if enabled
        if (cfg.prewarm_enabled && cfg.prewarm_per_function > 0 && cfg.num_workers > 0) {
            std::cout << "Pre-warming " << cfg.prewarm_per_function
                        << " containers per function across "
                        << cfg.num_workers << " workers\n";

            std::uint32_t num_workers = cfg.num_workers;
            std::size_t func_idx = 0;

            for (FunctionId fid : function_ids) {
                for (std::uint32_t j = 0; j < cfg.prewarm_per_function; ++j) {
                    WorkerId wid = static_cast<WorkerId>(
                        (func_idx + j) % num_workers
                    );
                    auto& w = ps.mutable_worker(wid);

                    Container c;
                    c.function_id = fid;
                    c.worker_id   = wid;
                    c.busy        = false;
                    c.last_used   = 0.0;
                    c.lifetime_invocations = 0;

                    w.containers.push_back(c);

                    // Mark function as "warm-capable" on this worker
                    if (std::find(w.warm_functions.begin(),
                                    w.warm_functions.end(),
                                    fid) == w.warm_functions.end()) {
                        w.warm_functions.push_back(fid);
                    }
                }
                ++func_idx;
            }
        }

        // 2. Scheduler
        auto& reg = SchedulerRegistry::instance();
        if (!reg.has(cfg.scheduler_name)) {
            throw std::runtime_error("Unknown scheduler: " + cfg.scheduler_name);
        }
        auto sched = reg.create(cfg.scheduler_name, cfg.seed);

        Engine engine(std::move(ps), std::move(sched));
        // NEW
        engine.set_max_queue_len(cfg.worker_queue.max_queue_len);

        // 3. Workload
        std::unique_ptr<Workload> workload;

        if (cfg.workload_type == "uniform") {
            auto base = std::make_unique<UniformWorkload>(
                /*num_tenants=*/cfg.num_tenants,
                /*total_invocations=*/cfg.total_invocations,
                /*max_batch_size=*/cfg.batch_size,
                /*base_func_id=*/1,
                /*base_tenant_id=*/1,
                /*seed=*/cfg.seed + 1,
                /*functions_per_tenant=*/cfg.functions_per_tenant
            );
            // Use configured service time (still “default”, per-invocation distribution can come later)
            if (cfg.service_time.model == ServiceTimeModel::FIXED) {
                base->set_service_time_fixed(cfg.service_time.fixed);
            } else { // EXPONENTIAL
                base->set_service_time_exponential(cfg.service_time.mean);
            }


            if (cfg.attacker_enabled) {
                AttackConfig acfg = cfg.attacker_cfg;
                if (acfg.victims.empty()) acfg.victims = {1};
                auto attack_wl = std::make_unique<AttackWorkload>(
                    std::move(base), acfg, cfg.seed + 2);
                workload = std::move(attack_wl);
            } else {
                workload = std::move(base);
            }
        } else if (cfg.workload_type == "poisson") {
            auto base = std::make_unique<PoissonWorkload>(
                /*num_tenants=*/cfg.num_tenants,
                /*total_invocations=*/cfg.total_invocations,
                /*max_batch_size=*/cfg.batch_size,
                /*arrival_rate=*/cfg.arrival_rate,
                /*base_func_id=*/1,
                /*base_tenant_id=*/1,
                /*seed=*/cfg.seed + 1,
                /*functions_per_tenant=*/cfg.functions_per_tenant
            );
            // Use configured service time (still “default”, per-invocation distribution can come later)
            if (cfg.service_time.model == ServiceTimeModel::FIXED) {
                base->set_service_time_fixed(cfg.service_time.fixed);
            } else { // EXPONENTIAL
                base->set_service_time_exponential(cfg.service_time.mean);
            }


            if (cfg.attacker_enabled) {
                AttackConfig acfg = cfg.attacker_cfg;
                if (acfg.victims.empty()) acfg.victims = {1};
                auto attack_wl = std::make_unique<AttackWorkload>(
                    std::move(base), acfg, cfg.seed + 2);
                workload = std::move(attack_wl);
            } else {
                workload = std::move(base);
            }
        } else if (cfg.workload_type == "burst") {
            auto base = std::make_unique<BurstWorkload>(
                /*num_tenants=*/cfg.num_tenants,
                /*total_invocations=*/cfg.total_invocations,
                /*max_batch_size=*/cfg.batch_size,
                /*base_rate=*/cfg.burst_base_rate,
                /*burst_rate=*/cfg.burst_burst_rate,
                /*burst_period=*/cfg.burst_period,
                /*burst_duty_cycle=*/cfg.burst_duty_cycle,
                /*base_func_id=*/1,
                /*base_tenant_id=*/1,
                /*seed=*/cfg.seed + 1,
                /*functions_per_tenant=*/cfg.functions_per_tenant
            );
            // Use configured service time (still “default”, per-invocation distribution can come later)
            if (cfg.service_time.model == ServiceTimeModel::FIXED) {
                base->set_service_time_fixed(cfg.service_time.fixed);
            } else { // EXPONENTIAL
                base->set_service_time_exponential(cfg.service_time.mean);
            }


            if (cfg.attacker_enabled) {
                AttackConfig acfg = cfg.attacker_cfg;
                if (acfg.victims.empty()) acfg.victims = {1};
                auto attack_wl = std::make_unique<AttackWorkload>(
                    std::move(base), acfg, cfg.seed + 2);
                workload = std::move(attack_wl);
            } else {
                workload = std::move(base);
            }
        } else {
            throw std::runtime_error("Unsupported workload type: " + cfg.workload_type);
        }

        // 4. Scenario
        SingleWorkloadScenario scenario(
            std::move(engine),
            std::move(workload),
            /*time_step=*/cfg.time_step
        );

        scenario.run();

        // 5. Summary
        const auto& eng = scenario.engine();
        const auto& m   = eng.metrics();

        std::cout << "=== Experiment Summary ===\n";
        std::cout << "Scheduler: " << cfg.scheduler_name << "\n";
        std::cout << "Workload:  " << cfg.workload_type
                    << (cfg.attacker_enabled ? " + attacker" : "") << "\n";
        std::cout << "Sim time:  " << eng.now() << "\n";
        std::cout << "Total arrivals: " << m.arrivals_total() << "\n";
        std::cout << "Total drops: " << m.drops_total() << "\n";

        std::cout << "\nPer-tenant arrivals:\n";
        for (auto& kv : m.tenant_invocations()) {
            std::cout << "  tenant " << kv.first << " -> " << kv.second << "\n";
        }

        std::cout << "\nPer-tenant drops:\n";
        for (auto& kv : m.tenant_invocations()) {
            std::cout << "  tenant " << kv.first << " -> " << m.drops_for_tenant(kv.first) << "\n";
        }

        // for (auto& kv : eng.max_q_seen_) {
        //     std::cout << "worker " << kv.first << " max_q=" << kv.second << "\n";
        //     }

        // --- Optional CSV output ---
        if (!cfg.output_csv.empty()) {
            bool write_header = false;

            {
                std::ifstream check(cfg.output_csv);
                write_header = !check.good(); // header if file doesn't exist
            }

            std::ofstream out(cfg.output_csv, std::ios::app);
            if (!out) {
                throw std::runtime_error("Cannot open CSV file: " + cfg.output_csv);
            }

            if (write_header) {
                out << "scheduler,workload,attacker,"
                    << "num_tenants,num_workers,total_arrivals,"
                    << "seed,idle_timeout,prewarm_per_function,"
                    << "attack_intensity,attacker_pattern,"     // NEW
                    << "sim_time,total_dropps,"
                    << "cold_count,warm_count,"
                    << "coloc_count,time_to_first_coloc,"
                    << "attacker_tenant,attacker_arrivals,"
                    << "attacker_drops,attacker_drop_rate,"
                    << "victim_tenant,"
                    << "victim_arrivals,victim_drops,victim_drop_rate,"
                    << "victim_mean_latency,victim_p95_latency,"
                    << "service_time_model,service_time_mean,service_time_fixed,"
                    << "worker_queue_len\n";
            }

            TenantId victim_tenant = 1;
            TenantId attacker_tenant = cfg.attacker_cfg.attacker_tenant;
            std::uint64_t cold = 0;
            std::uint64_t warm = 0;

            auto total_arrivals_all = m.arrivals_total();

            auto victim_arr = m.arrivals_for_tenant(victim_tenant);
            auto victim_drop = m.drops_for_tenant(victim_tenant);
            double victim_mean_lat = m.mean_latency_for_tenant(victim_tenant);
            double victim_p95_lat  = m.p95_latency_for_tenant(victim_tenant);
            for (auto& kv : m.tenant_invocations()) {
                cold += m.cold_starts_for(kv.first);
                warm += m.warm_starts_for(kv.first);
            }

            std::uint64_t coloc_va = m.colocation_count(victim_tenant, attacker_tenant);

            // time-to-first-co-location (if any), else -1
            double ttf_coloc = m.first_colocation_time(victim_tenant, attacker_tenant);

            double victim_drop_rate = (victim_arr == 0) ? 0.0 : static_cast<double>(victim_drop) / victim_arr;

            auto attacker_arr = m.arrivals_for_tenant(attacker_tenant);
            auto attacker_drop = m.drops_for_tenant(attacker_tenant);
            double attacker_drop_rate = attacker_arr ? (double)attacker_drop / attacker_arr : 0.0;

            out << cfg.scheduler_name << ","
                << cfg.workload_type << ","
                << (cfg.attacker_enabled ? 1 : 0) << ","
                << cfg.num_tenants << ","
                << cfg.num_workers << ","
                << total_arrivals_all << ","
                << cfg.seed << ","
                << cfg.idle_timeout << ","
                << cfg.prewarm_per_function << ","
                << cfg.attacker_cfg.attack_per_victim << ","   // attack_intensity
                << cfg.attacker_cfg.pattern << "," // attacker_pattern
                << eng.now() << ","
                << eng.num_failed_invocations() << ","
                << cold << ","
                << warm << ","
                << coloc_va << ","
                << ttf_coloc << ","
                << attacker_tenant << ","
                << attacker_arr << ","
                << attacker_drop << ","
                << attacker_drop_rate << ","
                << victim_tenant << ","
                << victim_arr << ","
                << victim_drop << ","
                << victim_drop_rate << ","
                << victim_mean_lat << ","
                << victim_p95_lat << ","
                << (cfg.service_time.model == ServiceTimeModel::FIXED ? "fixed" : "exp") << ","
                << cfg.service_time.mean << ","
                << cfg.service_time.fixed << ","
                << cfg.worker_queue.max_queue_len << "\n";
        }

        if (cfg.trace_enabled) {
            TraceLogger::log("=== Run complete: sim_time=", eng.now(),
                             " dropped=", eng.num_failed_invocations(), " ===");
            TraceLogger::shutdown();
        }
    }
};

} // namespace kumo
