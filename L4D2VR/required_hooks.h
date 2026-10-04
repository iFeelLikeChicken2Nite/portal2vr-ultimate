#pragma once

#include <functional>
#include <string>
#include <vector>

// All required detours are created before the first one is enabled. The caller
// owns MinHook rollback if either stage fails.
class RequiredHooks {
public:
    using Operation = std::function<int()>;

    void Add(const char *name, Operation create, Operation enable)
    {
        m_Steps.push_back({name, std::move(create), std::move(enable)});
    }

    // An unresolved optional symbol has no safe target or original trampoline.
    // A resolved symbol still uses the checked required create/enable stages.
    void AddIfResolved(const char *name, const void *target, Operation create, Operation enable)
    {
        if (target) Add(name, std::move(create), std::move(enable));
    }

    bool CreateAll(std::string &failed)
    {
        for (const auto &step : m_Steps) {
            if (step.create()) {
                failed = step.name;
                return false;
            }
        }
        return true;
    }

    bool EnableAll(std::string &failed)
    {
        for (const auto &step : m_Steps) {
            if (step.enable()) {
                failed = step.name;
                return false;
            }
        }
        return true;
    }

private:
    struct Step {
        const char *name;
        Operation create;
        Operation enable;
    };
    std::vector<Step> m_Steps;
};
