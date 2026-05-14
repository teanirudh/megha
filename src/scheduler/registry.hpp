#pragma once

#include <stdexcept>
#include <string>

#include "guardian_scheduler.hpp"
#include "helper_scheduler.hpp"
#include "random_scheduler.hpp"
#include "scheduler.hpp"

namespace megha
{

/**
 * SchedulerRegistry: maps string names to scheduler factory functions.
 *
 * Usage:
 *   auto& reg = SchedulerRegistry::instance();
 *   auto sched = reg.create("random", seed_uint64);
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

    // Register a scheduler factory under a given name.
    void register_scheduler(const std::string &name, Factory factory)
    {
        factories_[name] = std::move(factory);
    }

    // Create a scheduler by name.
    // Throws std::invalid_argument on unknown name.
    SchedulerPtr create(const std::string &name, std::uint64_t seed) const
    {
        auto it = factories_.find(name);
        if (it == factories_.end())
        {
            throw std::invalid_argument("unknown scheduler: " + name);
        }
        return it->second(seed);
    }

    // Check if a scheduler name is known.
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

        register_scheduler("helper", [](std::uint64_t seed)
                           { return SchedulerPtr(new HelperScheduler(seed)); });

        register_scheduler(
            "guardian", [](std::uint64_t seed)
            { return SchedulerPtr(new GuardianScheduler(seed)); });
    }

    std::unordered_map<std::string, Factory> factories_;
};

} // namespace megha
