#include "modules/task/ui/filters/task_inference_filter.h"

#include "modules/task/ui/filters/cls_inference_filter.h"
#include "modules/task/ui/filters/sg_inference_filter.h"

namespace studio {
namespace {

// Task composition lives here so the workspace and panel remain Task-independent.
const TaskInferenceFilterSpec filter_specs[] = {
    {"sg", []() -> std::unique_ptr<InferenceFilterContent> {
        return std::make_unique<SgInferenceFilterContent>();
    }},
    {"cls", []() -> std::unique_ptr<InferenceFilterContent> {
        return std::make_unique<ClsInferenceFilterContent>();
    }},
};

} // namespace

const TaskInferenceFilterSpec* find_inference_filter_spec(const std::string& task) {
    for (const TaskInferenceFilterSpec& spec : filter_specs) {
        if (task == spec.task) {
            return &spec;
        }
    }
    return nullptr;
}

} // namespace studio
