#pragma once

#include "workflow.h"

#include <string>
#include <vector>

namespace studio {

struct BindingSyncResult {
    bool changed = false;
    std::vector<std::string> notes;
};

std::string normalized_split(std::string split);
std::string stable_sample_id(const std::vector<std::string>& image_paths);
std::string default_class_color(int mask_value);

const AnnotationClass* find_annotation_class(
    const TaskDatasetBinding& binding,
    const std::string& class_id);
const AnnotationClass* find_annotation_class_by_mask(
    const TaskDatasetBinding& binding,
    int mask_value);

// Synchronizes the Task-side binding with the one connected raw Dataset.
// Existing Task annotations, selection, splits, and stable class identities
// remain authoritative while raw sample membership follows the Dataset.
BindingSyncResult synchronize_task_dataset_binding(
    WorkflowGraph& graph,
    int task_node_id);

// Refreshes every Task connected to a Dataset after its raw inventory changes.
// The UI uses this command instead of walking graph edges itself.
bool synchronize_task_bindings_for_dataset(
    WorkflowGraph& graph,
    int dataset_node_id);

} // namespace studio
