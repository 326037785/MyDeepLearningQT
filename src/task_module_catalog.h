#pragma once

#include "model_spec_catalog.h"

#include <array>
#include <string>

namespace studio {

struct TaskModuleDefinition {
    const char* id;
    const char* display_name;
    const char* description;
    bool available;
    const char* unavailable_reason;
};

inline constexpr std::array<TaskModuleDefinition, 2> task_modules{{
    {"sg", "Semantic Segmentation", "Train and run pixel-level semantic segmentation models.", true, ""},
    {"cls", "Classification", "Train and run image classification models.", true, ""},
}};

inline const TaskModuleDefinition* find_task_module(const std::string& id) {
    for (const auto& module : task_modules)
        if (id == module.id) return &module;
    return nullptr;
}

} // namespace studio
