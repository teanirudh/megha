#pragma once

#include <cstdint>
#include <fstream>
#include <istream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "types.hpp"
#include "workload/attack_workload.hpp"

namespace megha
{

/**
 * Experiment configuration:
 *
 *     seed=<uint64>
 *     num_tenants=<uint32>
 *     num_workers=<uint32>
 *     total_invocations=<uint64>
 *     functions_per_tenant=<uint32>
 *     batch_size=<uint32>
 *
 *     scheduler=<name>
 *     workload=<poisson>
 *
 *     attacker.enabled=<0|1>
 *     attacker.tenant=<uint32>
 *     attacker.function=<uint32>
 *     attacker.attack_intensity=<double>
 *     attacker.pattern=<string>
 *
 *     sweep.schedulers=<a,b,…>
 *     sweep.seeds=<1,2,…>
 *
 *     arrival_rate=<double>
 *     idle_timeout=<double>
 *     time_step=<double>
 *
 *     worker.cpu_cores=<double>
 *     worker.memory_mb=<uint32>
 *     worker.storage_mb=<uint32>
 *     worker.max_queue_len=<uint32>
 *
 *     trace_file=<path>
 *     results_file=<path>
 *
 *     service_time.model=<fixed|exponential>
 *     service_time.mean=<double>
 *     service_time.fixed=<double>
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
    std::uint64_t seed = 0;
    std::uint32_t num_tenants = 8;
    std::uint32_t num_workers = 4;
    std::uint64_t total_invocations = 100;
    std::uint32_t functions_per_tenant = 2;
    std::uint32_t batch_size = 10;

    std::string scheduler_name = "random";
    std::string workload_type = "poisson";

    bool attacker_enabled = false;
    AttackConfig attacker_cfg;

    std::vector<std::string> sweep_schedulers;
    std::vector<std::uint64_t> sweep_seeds;

    double arrival_rate = 1.0;
    double idle_timeout = 60.0;
    double time_step = 10.0;

    double worker_cpu_cores = 4.0;
    std::uint32_t worker_memory_mb = 4096;
    std::uint32_t worker_storage_mb = 200;

    std::string trace_file = "trace.log";
    std::string results_file = "results.csv";

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
            continue;
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

    cfg.seed = get_u64("seed", cfg.seed);
    cfg.num_tenants = get_u32("num_tenants", cfg.num_tenants);
    cfg.num_workers = get_u32("num_workers", cfg.num_workers);
    cfg.total_invocations = get_u64("total_invocations", cfg.total_invocations);
    cfg.functions_per_tenant =
        get_u32("functions_per_tenant", cfg.functions_per_tenant);
    cfg.batch_size = get_u32("batch_size", cfg.batch_size);

    cfg.scheduler_name = get_str("scheduler", cfg.scheduler_name);
    cfg.workload_type = get_str("workload", cfg.workload_type);

    cfg.attacker_enabled = get_bool("attacker.enabled", false);
    if (cfg.attacker_enabled)
    {
        cfg.attacker_cfg.attacker_tenant =
            static_cast<TenantId>(get_u32("attacker.tenant", 999));
        cfg.attacker_cfg.attacker_function =
            static_cast<FunctionId>(get_u32("attacker.function", 100));
        cfg.attacker_cfg.attack_intensity =
            get_f64("attacker.attack_intensity", 1.0);
        cfg.attacker_cfg.pattern =
            get_str("attacker.pattern", cfg.attacker_cfg.pattern);
        cfg.attacker_cfg.victims = {1};
    }

    cfg.sweep_schedulers = parse_list_str("sweep.schedulers");
    cfg.sweep_seeds = parse_list_u64("sweep.seeds");

    cfg.arrival_rate = get_f64("arrival_rate", 1.0);
    cfg.idle_timeout = get_f64("idle_timeout", cfg.idle_timeout);
    cfg.time_step = get_f64("time_step", cfg.time_step);

    cfg.worker_cpu_cores = get_f64("worker.cpu_cores", cfg.worker_cpu_cores);
    cfg.worker_memory_mb = get_u32("worker.memory_mb", cfg.worker_memory_mb);
    cfg.worker_storage_mb = get_u32("worker.storage_mb", cfg.worker_storage_mb);

    cfg.trace_file = get_str("trace_file", cfg.trace_file);
    cfg.results_file = get_str("results_file", cfg.results_file);

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

} // namespace megha
