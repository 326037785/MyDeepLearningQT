#pragma once

#include "modules/workflow/model/workflow.h"
#include <QImage>

namespace studio {

void confirm_empty_segmentation_annotation(TaskSampleBinding& sample);
void refresh_segmentation_annotation(
    const std::vector<AnnotationClass>& classes, TaskSampleBinding& sample);
void import_segmentation_annotation(
    const std::vector<AnnotationClass>& classes, TaskSampleBinding& sample,
    const QString& mask_path);
bool commit_segmentation_mask(
    const std::vector<AnnotationClass>& classes, TaskSampleBinding& sample,
    const QImage& mask);
QImage segmentation_annotation_image(const TaskSampleBinding& sample);

} // namespace studio
