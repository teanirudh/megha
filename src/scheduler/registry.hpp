// src/scheduler/registry.hpp
#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "scheduler/helper_scheduler.hpp"
#include "scheduler/openwhisk_scheduler.hpp"
#include "scheduler/openwhisk_warm_scheduler.hpp"
#include "scheduler/pasch_scheduler.hpp"
#include "scheduler/random_scheduler.hpp"
#include "scheduler/scheduler.hpp"
#include "scheduler/spread_scheduler.hpp"

namespace kumo
{

/**
 * SchedulerRegistry: maps string names to scheduler factory functions.
 *
 * Usage:
 *   auto& reg = SchedulerRegistry::instance();
 *   auto sched = reg.create("spread", /*seed* / 123);
 */
class SchedulerRegistry
{
  public:
    using Factory = std::function<SchedulerPtr(std::uint64_t seed)>;

    static SchedulerRegistry &instance()
    {
        static SchedulerRegistry inst;
        return inst;
    }

    /// Register a scheduler factory under a given name.
    void register_scheduler(const std::string &name, Factory factory)
    {
        factories_[name] = std::move(factory);
    }

    /// Create a scheduler by name. Throws std::invalid_argument on unknown name.
    SchedulerPtr create(const std::string &name, std::uint64_t seed) const
    {
        auto it = factories_.find(name);
        if (it == factories_.end())
        {
            throw std::invalid_argument("unknown scheduler: " + name);
        }
        return it->second(seed);
    }

    /// Check if a scheduler name is known.
    bool has(const std::string &name) const noexcept
    {
        return factories_.find(name) != factories_.end();
    }

  private:
    SchedulerRegistry()
    {
        // Built-in schedulers
        register_scheduler("random", [](std::uint64_t seed)
                           { return SchedulerPtr(new RandomScheduler(seed)); });

        register_scheduler("spread", [](std::uint64_t seed)
                           { return SchedulerPtr(new SpreadScheduler(seed)); });

        register_scheduler(
            "openwhisk", [](std::uint64_t seed)
            { return SchedulerPtr(new OpenWhiskScheduler(seed)); });

        register_scheduler(
            "openwhisk_warm", [](std::uint64_t seed)
            { return SchedulerPtr(new OpenWhiskWarmScheduler(seed)); });

        register_scheduler("helper", [](std::uint64_t seed)
                           { return SchedulerPtr(new HelperScheduler(seed)); });

        register_scheduler("pasch", [](std::uint64_t seed)
                           { return SchedulerPtr(new PASchScheduler(seed)); });
    }

    std::unordered_map<std::string, Factory> factories_;
};

} // namespace kumo
