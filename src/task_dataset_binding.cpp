#include "task_dataset_binding.h"

#include <algorithm>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace studio {

namespace {

struct RawSample {
    std::string id;
    std::vector<const DatasetConfig::Item*> items;
};

std::vector<RawSample> group_raw_samples(const DatasetConfig& dataset) {
    std::vector<const DatasetConfig::Item*> valid;
    for (const auto& item : dataset.items)
        if (item.valid) valid.push_back(&item);

    std::vector<RawSample> grouped;
    std::map<std::string, size_t> explicit_groups;
    std::vector<const DatasetConfig::Item*> implicit;
    for (const auto* item : valid) {
        if (item->sample_id.empty()) {
            implicit.push_back(item);
            continue;
        }
        const auto [it, inserted] = explicit_groups.emplace(item->sample_id, grouped.size());
        if (inserted) grouped.push_back({item->sample_id, {}});
        grouped[it->second].items.push_back(item);
    }

    const size_t bucket = static_cast<size_t>(std::max(1, dataset.bucket_capacity));
    for (size_t offset = 0; offset < implicit.size(); offset += bucket) {
        const size_t end = std::min(implicit.size(), offset + bucket);
        std::vector<std::string> paths;
        RawSample sample;
        for (size_t index = offset; index < end; ++index) {
            sample.items.push_back(implicit[index]);
            paths.push_back(implicit[index]->path);
        }
        sample.id = stable_sample_id(paths);
        grouped.push_back(std::move(sample));
    }
    return grouped;
}

bool string_vector_equal(
    const std::vector<std::string>& left,
    const std::vector<std::string>& right)
{
    return left == right;
}

} // namespace

std::string normalized_split(std::string split) {
    if (split == "train" || split == "validation" || split == "test" ||
        split == "unassigned")
        return split;
    throw std::invalid_argument(
        "split must be train, validation, test, or unassigned");
}

std::string stable_sample_id(const std::vector<std::string>& image_paths) {
    constexpr std::uint64_t offset = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t hash = offset;
    for (const auto& path : image_paths) {
        for (const unsigned char value : path) {
            hash ^= value;
            hash *= prime;
        }
        hash ^= 0xffu;
        hash *= prime;
    }
    std::ostringstream stream;
    stream << "sample-" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

std::string default_class_color(int mask_value) {
    static const char* colors[] = {
        "#1F8A9E", "#D18432", "#458C63", "#C34655",
        "#5B66AD", "#B55E96", "#76942D", "#2F78C4",
        "#9B6BD3", "#D45D91", "#7A8B8E", "#D27A3F"};
    if (mask_value <= 0) return "#000000";
    return colors[static_cast<size_t>(mask_value - 1) %
                  (sizeof(colors) / sizeof(colors[0]))];
}

const AnnotationClass* find_annotation_class(
    const TaskDatasetBinding& binding,
    const std::string& class_id)
{
    const auto found = std::find_if(
        binding.classes.begin(), binding.classes.end(),
        [&class_id](const AnnotationClass& value) { return value.id == class_id; });
    return found == binding.classes.end() ? nullptr : &*found;
}

const AnnotationClass* find_annotation_class_by_mask(
    const TaskDatasetBinding& binding,
    int mask_value)
{
    const auto found = std::find_if(
        binding.classes.begin(), binding.classes.end(),
        [mask_value](const AnnotationClass& value) {
            return value.mask_value == mask_value;
        });
    return found == binding.classes.end() ? nullptr : &*found;
}

BindingSyncResult synchronize_task_dataset_binding(
    WorkflowGraph& graph,
    int task_node_id)
{
    if (graph.nodes().count(task_node_id) == 0 ||
        graph.node(task_node_id).kind != NodeKind::TaskConfig)
        throw std::runtime_error("TaskDatasetBinding requires a Task node");

    int dataset_node_id = 0;
    for (const auto& edge : graph.connections()) {
        if (edge.to_node != task_node_id || edge.to_port != 0) continue;
        if (graph.node(edge.from_node).kind != NodeKind::DatasetInput) continue;
        if (dataset_node_id != 0)
            throw std::runtime_error("a Task can bind only one Dataset");
        dataset_node_id = edge.from_node;
    }

    auto& task = std::get<TaskNodeConfig>(graph.node(task_node_id).config);
    auto& binding = task.dataset_binding;
    BindingSyncResult outcome;
    if (binding.source_dataset_node_id != dataset_node_id) {
        binding.source_dataset_node_id = dataset_node_id;
        outcome.changed = true;
    }
    if (dataset_node_id == 0) {
        outcome.notes.push_back("Task has no connected Dataset");
        return outcome;
    }

    const auto& dataset = std::get<DatasetConfig>(graph.node(dataset_node_id).config);
    std::map<std::string, TaskSampleBinding> existing;
    std::vector<std::string> existing_order;
    for (auto& sample : binding.samples) {
        existing_order.push_back(sample.sample_id);
        existing.emplace(sample.sample_id, std::move(sample));
    }

    std::vector<TaskSampleBinding> synchronized;
    std::set<std::string> seen;
    const auto raw_samples = group_raw_samples(dataset);
    synchronized.reserve(raw_samples.size() + existing.size());
    for (const auto& raw : raw_samples) {
        TaskSampleBinding sample;
        const auto old = existing.find(raw.id);
        if (old != existing.end()) sample = std::move(old->second);
        else {
            sample.sample_id = raw.id;
            sample.selected = true;
            outcome.changed = true;
        }

        std::vector<std::string> paths;
        for (const auto* item : raw.items) {
            paths.push_back(item->path);
        }
        if (!string_vector_equal(sample.image_paths, paths)) {
            sample.image_paths = std::move(paths);
            outcome.changed = true;
        }

        const auto normalized = normalized_split(sample.split);
        if (normalized != sample.split) {
            sample.split = normalized;
            outcome.changed = true;
        }

        std::vector<std::string> generated_issues;
        if (raw.items.size() != static_cast<size_t>(std::max(1, dataset.bucket_capacity)))
            generated_issues.push_back("sample does not contain the configured bucket image count");
        if (!sample.mask_path.empty() || !sample.class_id.empty())
            sample.annotation_state = "annotated";
        else
            sample.annotation_state = "unannotated";
        if (sample.quality_issues != generated_issues) {
            sample.quality_issues = std::move(generated_issues);
            outcome.changed = true;
        }

        seen.insert(raw.id);
        synchronized.push_back(std::move(sample));
    }

    // Preserve orphaned task metadata rather than silently discarding
    // annotations after a raw-data reimport. It remains visible as a quality
    // issue and is excluded from training until the source returns.
    for (const auto& id : existing_order) {
        if (seen.count(id) != 0) continue;
        auto found = existing.find(id);
        if (found == existing.end()) continue;
        auto orphan = std::move(found->second);
        orphan.selected = false;
        if (std::find(orphan.quality_issues.begin(), orphan.quality_issues.end(),
                      "source sample is missing from the Dataset") ==
            orphan.quality_issues.end())
            orphan.quality_issues.push_back("source sample is missing from the Dataset");
        synchronized.push_back(std::move(orphan));
        outcome.changed = true;
    }

    binding.samples = std::move(synchronized);
    return outcome;
}

bool synchronize_task_bindings_for_dataset(
    WorkflowGraph& graph,
    int dataset_node_id)
{
    if (graph.nodes().count(dataset_node_id) == 0 ||
        graph.node(dataset_node_id).kind != NodeKind::DatasetInput)
        throw std::runtime_error(
            "binding refresh requires a Dataset node");
    bool changed = false;
    std::vector<int> task_ids;
    for (const auto& edge : graph.connections()) {
        if (edge.from_node != dataset_node_id) continue;
        if (graph.node(edge.to_node).kind == NodeKind::TaskConfig)
            task_ids.push_back(edge.to_node);
    }
    for (const int task_id : task_ids)
        changed =
            synchronize_task_dataset_binding(graph, task_id).changed ||
            changed;
    return changed;
}

} // namespace studio
