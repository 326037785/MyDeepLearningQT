#pragma once

#include "modules/task/ui/samples/sample_browser.h"
#include "modules/task/application/task_dataset_binding.h"

namespace studio {

bool annotation_sample_visible(const TaskSampleBinding& sample);
SampleBrowserEntry annotation_sample_entry(const TaskSampleBinding& sample,
                                           int sample_index, int source_index,
                                           const QString& state = {});

} // namespace studio
