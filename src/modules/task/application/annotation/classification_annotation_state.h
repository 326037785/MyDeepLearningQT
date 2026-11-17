#pragma once

#include "modules/workflow/model/workflow.h"

namespace studio {

void refresh_classification_annotation(
    const std::vector<AnnotationClass>& classes, TaskSampleBinding& sample);

// Current class collection in Classification backend order. UI ordering and
// segmentation pixel values do not participate in this mapping.
std::vector<const AnnotationClass*> classification_classes(
    const std::vector<AnnotationClass>& classes);

} // namespace studio
