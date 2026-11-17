#pragma once

#include <memory>
#include <string>

namespace studio {

class InferenceFilterContent;

struct TaskInferenceFilterSpec {
    const char* task;
    std::unique_ptr<InferenceFilterContent> (*create_content)();
};

const TaskInferenceFilterSpec* find_inference_filter_spec(const std::string& task);

} // namespace studio
