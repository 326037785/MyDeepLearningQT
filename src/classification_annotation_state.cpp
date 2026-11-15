#include "classification_annotation_state.h"
#include "workflow.h"

#include <algorithm>

namespace studio {
namespace {

bool valid_class(const std::vector<AnnotationClass>& classes, const TaskSampleBinding& sample) {
    return std::any_of(classes.begin(), classes.end(), [&](const auto& value) {
        return !value.id.empty() && value.id == sample.class_id;
    });
}

} // namespace

void refresh_classification_annotation(const std::vector<AnnotationClass>& classes, TaskSampleBinding& sample) {
    sample.annotation_state = valid_class(classes, sample) ? "annotated" : "unannotated";
}

std::vector<const AnnotationClass*> classification_classes(
    const std::vector<AnnotationClass>& classes)
{
    std::vector<const AnnotationClass*> result;
    for (const AnnotationClass& value : classes) {
        if (!value.id.empty()) {
            result.push_back(&value);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto* left, const auto* right) {
        return left->id < right->id;
    });
    return result;
}

} // namespace studio
