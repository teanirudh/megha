#pragma once

#include <cstdint>
#include <fstream>
#include <istream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "common/types.hpp"
#include "workload/attack_workload.hpp" // for AttackConfig

namespace kumo
{

/**
 * Minimal experiment configuration.
 *
 * Parsed from a simple key=value config file.
 *
 * Supported keys:
 *   scheduler         = spread | random | helper | openwhisk | openwhisk_warm | pasch
 *   workload          = uniform
 *   num_tenants       = uint
 *   num_workers       = uint
 *   total_invocations = uint64
 *   batch_size        = uint
 *   time_step         = double
 *   seed              = uint64
 *
 *   # optional attacker settings:
 *   attacker.enabled        = 0|1
 *   attacker.tenant         = uint
 *   attacker.function       = uint
 *   attacker.attack_per_victim = double
 */

enum class ServiceTimeModel
{
    FIXED,
    EXPONENTIAL
};

struct ServiceTimeConfig
{
    ServiceTimeModel model = ServiceTimeModel::EXPONENTIAL;
    double mean = 20.0;  // for exponential
    double fixed = 20.0; // for fixed
};

struct WorkerQueueConfig
{
    std::size_t max_queue_len = 100; // bounded FIFO queue
};

struct ExperimentConfig
{
    std::string scheduler_name = "spread";
    std::string workload_type = "uniform";

    std::uint32_t num_tenants = 3;
    std::uint32_t num_workers = 3;
    std::uint64_t total_invocations = 100;
    std::uint32_t batch_size = 10;
    double time_step = 10.0;
    std::uint64_t seed = 123;

    bool attacker_enabled = false;
    AttackConfig attacker_cfg;

    std::string output_csv;    // optional path
    double arrival_rate = 1.0; // Poisson lambda (global)

    // burst workload params
    double burst_base_rate = 0.1;
    double burst_burst_rate = 2.0;
    double burst_period = 60.0;
    double burst_duty_cycle = 0.2; // fraction of period

    // base idle timeout (applied to all functions)
    double idle_timeout = 60.0;
    // sweep parameters
    std::vector<std::string> sweep_schedulers;
    std::vector<std::uint64_t> sweep_seeds;
    std::vector<double> sweep_idle_timeouts;
    std::vector<double> sweep_attack_intensities;
    std::vector<std::string> sweep_attacker_patterns;
    std::vector<std::uint32_t> sweep_num_workers;
    std::vector<std::size_t> sweep_max_queue_lens;

    // multiple functions per tenant
    std::uint32_t functions_per_tenant = 1;

    // pre-warmed containers
    bool prewarm_enabled = false;
    std::uint32_t prewarm_per_function = 0;

    // base worker capacity (homogeneous default)
    double worker_cpu_cores = 4.0;
    std::uint32_t worker_memory_mb = 4096;
    std::uint32_t worker_storage_mb = 200;

    // simple heterogeneity model (two types: small + normal)
    bool hetero_enabled = false;
    double hetero_small_frac = 0.0; // fraction of workers that are "small"
    double hetero_small_cpu_scale = 0.5;
    double hetero_small_mem_scale = 0.5;

    // tracing
    bool trace_enabled = false;
    std::string trace_file;

    // --- NEW for DoS ---
    ServiceTimeConfig service_time;
    WorkerQueueConfig worker_queue;
};

inline ExperimentConfig parse_experiment_config(std::istream &in)
{
    ExperimentConfig cfg;

    std::unordered_map<std::string, std::string> kv;

    std::string line;
    while (std::getline(in, line))
    {
        // trim leading/trailing whitespace
        auto trim = [](std::string s)
        {
            std::size_t start = 0;
            while (start < s.size() &&
                   (s[start] == ' ' || s[start] == '\t' || s[start] == '\r'))
            {
                ++start;
            }
            std::size_t end = s.size();
            while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                                   s[end - 1] == '\r'))
            {
                --end;
            }
            return s.substr(start, end - start);
        };

        line = trim(line);
        if (line.empty())
            continue;
        if (line[0] == '#')
            continue; // comment

        auto pos = line.find('=');
        if (pos == std::string::npos)
            continue;

        std::string key = trim(line.substr(0, pos));
        std::string val = trim(line.substr(pos + 1));

        kv[key] = val;
    }

    auto get_str = [&](const std::string &key,
                       const std::string &def) -> std::string
    {
        auto it = kv.find(key);
        if (it == kv.end())
            return def;
        return it->second;
    };
    auto get_u32 = [&](const std::string &key,
                       std::uint32_t def) -> std::uint32_t
    {
        auto it = kv.find(key);
        if (it == kv.end())
            return def;
        return static_cast<std::uint32_t>(std::stoul(it->second));
    };
    auto get_u64 = [&](const std::string &key,
                       std::uint64_t def) -> std::uint64_t
    {
        auto it = kv.find(key);
        if (it == kv.end())
            return def;
        return static_cast<std::uint64_t>(std::stoull(it->second));
    };
    auto get_f64 = [&](const std::string &key, double def) -> double
    {
        auto it = kv.find(key);
        if (it == kv.end())
            return def;
        return std::stod(it->second);
    };
    auto get_bool = [&](const std::string &key, bool def) -> bool
    {
        auto it = kv.find(key);
        if (it == kv.end())
            return def;
        const auto &v = it->second;
        return (v == "1" || v == "true" || v == "TRUE" || v == "yes");
    };

    auto parse_list_str =
        [&](const std::string &key) -> std::vector<std::string>
    {
        std::vector<std::string> out;
        auto it = kv.find(key);
        if (it == kv.end())
            return out;
        std::string s = it->second;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::string t = item;
            // reuse trim from earlier
            auto trim = [](std::string s)
            {
                std::size_t start = 0;
                while (
                    start < s.size() &&
                    (s[start] == ' ' || s[start] == '\t' || s[start] == '\r'))
                {
                    ++start;
                }
                std::size_t end = s.size();
                while (end > start &&
                       (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                        s[end - 1] == '\r'))
                {
                    --end;
                }
                return s.substr(start, end - start);
            };
            t = trim(t);
            if (!t.empty())
                out.push_back(t);
        }
        return out;
    };

    auto parse_list_size_t =
        [&](const std::string &key) -> std::vector<std::size_t>
    {
        std::vector<std::size_t> out;
        auto it = kv.find(key);
        if (it == kv.end())
            return out;
        std::stringstream ss(it->second);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::string t = item;
            // same trim as above (you can factor it out)
            auto trim = [](std::string s)
            {
                std::size_t start = 0;
                while (
                    start < s.size() &&
                    (s[start] == ' ' || s[start] == '\t' || s[start] == '\r'))
                {
                    ++start;
                }
                std::size_t end = s.size();
                while (end > start &&
                       (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                        s[end - 1] == '\r'))
                {
                    --end;
                }
                return s.substr(start, end - start);
            };
            t = trim(t);
            if (t.empty())
                continue;
            out.push_back(static_cast<std::size_t>(std::stoull(t)));
        }
        return out;
    };

    auto parse_list_u32 =
        [&](const std::string &key) -> std::vector<std::uint32_t>
    {
        std::vector<std::uint32_t> out;
        auto it = kv.find(key);
        if (it == kv.end())
            return out;
        std::stringstream ss(it->second);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::string t = item;
            // same trim as above (you can factor it out)
            auto trim = [](std::string s)
            {
                std::size_t start = 0;
                while (
                    start < s.size() &&
                    (s[start] == ' ' || s[start] == '\t' || s[start] == '\r'))
                {
                    ++start;
                }
                std::size_t end = s.size();
                while (end > start &&
                       (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                        s[end - 1] == '\r'))
                {
                    --end;
                }
                return s.substr(start, end - start);
            };
            t = trim(t);
            if (t.empty())
                continue;
            out.push_back(static_cast<std::uint32_t>(std::stoul(t)));
        }
        return out;
    };

    auto parse_list_u64 =
        [&](const std::string &key) -> std::vector<std::uint64_t>
    {
        std::vector<std::uint64_t> out;
        auto it = kv.find(key);
        if (it == kv.end())
            return out;
        std::stringstream ss(it->second);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::string t = item;
            // same trim as above (you can factor it out)
            auto trim = [](std::string s)
            {
                std::size_t start = 0;
                while (
                    start < s.size() &&
                    (s[start] == ' ' || s[start] == '\t' || s[start] == '\r'))
                {
                    ++start;
                }
                std::size_t end = s.size();
                while (end > start &&
                       (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                        s[end - 1] == '\r'))
                {
                    --end;
                }
                return s.substr(start, end - start);
            };
            t = trim(t);
            if (t.empty())
                continue;
            out.push_back(static_cast<std::uint64_t>(std::stoull(t)));
        }
        return out;
    };

    auto parse_list_f64 = [&](const std::string &key) -> std::vector<double>
    {
        std::vector<double> out;
        auto it = kv.find(key);
        if (it == kv.end())
            return out;
        std::stringstream ss(it->second);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            std::string t = item;
            auto trim = [](std::string s)
            {
                std::size_t start = 0;
                while (
                    start < s.size() &&
                    (s[start] == ' ' || s[start] == '\t' || s[start] == '\r'))
                {
                    ++start;
                }
                std::size_t end = s.size();
                while (end > start &&
                       (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                        s[end - 1] == '\r'))
                {
                    --end;
                }
                return s.substr(start, end - start);
            };
            t = trim(t);
            if (t.empty())
                continue;
            out.push_back(std::stod(t));
        }
        return out;
    };

    cfg.scheduler_name = get_str("scheduler", cfg.scheduler_name);
    cfg.workload_type = get_str("workload", cfg.workload_type);
    cfg.num_tenants = get_u32("num_tenants", cfg.num_tenants);
    cfg.num_workers = get_u32("num_workers", cfg.num_workers);
    cfg.total_invocations = get_u64("total_invocations", cfg.total_invocations);
    cfg.batch_size = get_u32("batch_size", cfg.batch_size);
    cfg.time_step = get_f64("time_step", cfg.time_step);
    cfg.seed = get_u64("seed", cfg.seed);
    cfg.output_csv = get_str("output_csv", "");
    cfg.arrival_rate = get_f64("arrival_rate", 1.0);
    cfg.burst_base_rate = get_f64("burst.base_rate", cfg.burst_base_rate);
    cfg.burst_burst_rate = get_f64("burst.burst_rate", cfg.burst_burst_rate);
    cfg.burst_period = get_f64("burst.period", cfg.burst_period);
    cfg.burst_duty_cycle = get_f64("burst.duty_cycle", cfg.burst_duty_cycle);
    cfg.idle_timeout = get_f64("idle_timeout", cfg.idle_timeout);
    cfg.functions_per_tenant =
        get_u32("functions_per_tenant", cfg.functions_per_tenant);
    cfg.prewarm_enabled = get_bool("prewarm.enabled", false);
    cfg.prewarm_per_function =
        get_u32("prewarm.per_function", cfg.prewarm_per_function);
    cfg.worker_cpu_cores = get_f64("worker.cpu_cores", cfg.worker_cpu_cores);
    cfg.worker_memory_mb = get_u32("worker.memory_mb", cfg.worker_memory_mb);
    cfg.worker_storage_mb = get_u32("worker.storage_mb", cfg.worker_storage_mb);

    cfg.hetero_enabled = get_bool("hetero.enabled", false);
    cfg.hetero_small_frac = get_f64("hetero.small_frac", cfg.hetero_small_frac);
    cfg.hetero_small_cpu_scale =
        get_f64("hetero.small_cpu_scale", cfg.hetero_small_cpu_scale);
    cfg.hetero_small_mem_scale =
        get_f64("hetero.small_mem_scale", cfg.hetero_small_mem_scale);

    cfg.trace_enabled = get_bool("trace.enabled", false);
    cfg.trace_file = get_str("trace.file", "");

    cfg.sweep_schedulers = parse_list_str("sweep.schedulers");
    cfg.sweep_seeds = parse_list_u64("sweep.seeds");
    cfg.sweep_idle_timeouts = parse_list_f64("sweep.idle_timeouts");
    cfg.sweep_attack_intensities = parse_list_f64("sweep.attack_intensities");
    cfg.sweep_attacker_patterns = parse_list_str("sweep.attacker_patterns");
    cfg.sweep_num_workers = parse_list_u32("sweep.num_workers");
    cfg.sweep_max_queue_lens = parse_list_size_t("sweep.max_queue_lens");

    cfg.attacker_enabled = get_bool("attacker.enabled", false);
    if (cfg.attacker_enabled)
    {
        cfg.attacker_cfg.attacker_tenant =
            static_cast<TenantId>(get_u32("attacker.tenant", 999));
        cfg.attacker_cfg.attacker_function =
            static_cast<FunctionId>(get_u32("attacker.function", 100));
        cfg.attacker_cfg.attack_per_victim =
            get_f64("attacker.attack_per_victim", 1.0);
        cfg.attacker_cfg.attacker_user = cfg.attacker_cfg.attacker_tenant;
        // Victim set is not specified here yet; we’ll assume tenant 1 as victim
        cfg.attacker_cfg.victims = {1};
    }

    cfg.service_time.model =
        get_str("service_time.model", "exponential") == "fixed"
            ? ServiceTimeModel::FIXED
            : ServiceTimeModel::EXPONENTIAL;
    cfg.service_time.mean = get_f64("service_time.mean", 20.0);
    cfg.service_time.fixed = get_f64("service_time.fixed", 20.0);
    cfg.worker_queue.max_queue_len = get_u32("worker.max_queue_len", 100);

    return cfg;
}

inline ExperimentConfig load_experiment_config(const std::string &path)
{
    std::ifstream in(path);
    if (!in)
    {
        throw std::runtime_error("Cannot open config file: " + path);
    }
    return parse_experiment_config(in);
}

} // namespace kumo
