#pragma once

#include "modules/task/application/task_dataset_binding.h"
#include <QString>
#include <functional>

class QWidget;

namespace studio {

std::string choose_annotation_class(QWidget* parent, const std::vector<AnnotationClass>& classes,
    const std::function<std::string(const QString&)>& add_class);

} // namespace studio
