#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

class OptionalHooks {
public:
    using Operation = std::function<int()>;
    using Warning = std::function<void(const std::string &)>;

    void AddIfResolved(const char *name, const void *target, Operation create, Operation enable)
    {
        if (target)
            m_Steps.push_back({name, std::move(create), std::move(enable), false});
    }

    void Install(Warning warn)
    {
        for (auto &step : m_Steps) {
            if (step.create() == 0)
                step.created = true;
            else
                warn(std::string("Warning: optional hook ") + step.name +
                     " create failed; its VR behavior is disabled.");
        }
        for (auto &step : m_Steps) {
            if (step.created && step.enable() != 0)
                warn(std::string("Warning: optional hook ") + step.name +
                     " enable failed; its VR behavior is disabled.");
        }
    }

private:
    struct Step {
        const char *name;
        Operation create;
        Operation enable;
        bool created;
    };
    // Keep successfully created trampolines, even when enabling fails.
    std::vector<Step> m_Steps;
};
