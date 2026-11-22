#pragma once

#include <array>
#include <string>

namespace studio {

struct ModelSpecOption {
    const char* task;
    int index;
    const char* scale;
};

inline constexpr std::array<ModelSpecOption, 6> model_spec_options{{
    {"sg", 0, "s"},
    {"sg", 1, "m"},
    {"cls", 1, "s"},
    {"cls", 2, "n"},
    {"od", 0, "n"},
    {"od", 1, "n"},
}};

inline bool supports_model_spec(const std::string& task, int index, const std::string& scale) {
    for (const auto& option : model_spec_options)
        if (task == option.task && index == option.index && scale == option.scale) return true;
    return false;
}

inline const ModelSpecOption* first_model_spec(const std::string& task) {
    for (const auto& option : model_spec_options)
        if (task == option.task) return &option;
    return nullptr;
}

inline const ModelSpecOption* first_model_spec(const std::string& task, int index) {
    for (const auto& option : model_spec_options)
        if (task == option.task && index == option.index) return &option;
    return nullptr;
}

} // namespace studio
