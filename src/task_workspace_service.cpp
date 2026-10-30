#include "task_workspace_service.h"
#include <QDateTime>

#include "model_spec_catalog.h"
#include "task_dataset_binding.h"
#include "classification_annotation_state.h"
#include "segmentation_annotation_state.h"
#include "project_storage.h"

#include <common/config/ai_config.h>

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>

namespace studio {

namespace {

bool equal_folded(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) return false;
    for (size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index])))
            return false;
    }
    return true;
}

bool contains_issue(const std::vector<std::string>& issues, const std::string& issue) {
    return std::find(issues.begin(), issues.end(), issue) != issues.end();
}

void add_issue(std::vector<std::string>& issues, const std::string& issue) {
    if (!contains_issue(issues, issue)) issues.push_back(issue);
}

std::vector<std::string> structural_issues(const TaskSampleBinding& sample) {
    std::vector<std::string> result;
    for (const auto& issue : sample.quality_issues) {
        if (issue == "sample does not contain the configured bucket image count" ||
            issue == "source sample is missing from the Dataset")
            result.push_back(issue);
    }
    return result;
}

jocelin::geo::Region training_mask_region(const std::string& path) {
    if (path.empty()) return {};
    const QImage source(QString::fromStdString(path));
    if (source.isNull())
        throw std::runtime_error("cannot read training exclusion mask: " + path);
    const QImage mask = source.convertToFormat(QImage::Format_Grayscale8);
    return jocelin::geo::binary_image_to_region_simd_omp({mask.constBits(),
        mask.height(), mask.width(), static_cast<size_t>(mask.bytesPerLine())});
}

std::string split_stratum(
    const TaskNodeConfig& task,
    const TaskSampleBinding& sample)
{
    if (task.task == "cls")
        return sample.class_id.empty() ? "unannotated" : "class:" + sample.class_id;
    if (sample.annotation_state != "annotated") return "unannotated";
    std::set<int> present;
    for (const auto& region : sample.regions)
        for (const auto& run : region.vRun) present.insert(run.label);
    std::vector<int> values(present.begin(), present.end());
    if (values.empty()) return "empty-target";
    std::string result = "mask:";
    for (const int value : values) result += std::to_string(value) + ",";
    return result;
}

std::array<size_t, 3> desired_split_counts(
    size_t sample_count,
    int train_percent,
    int validation_percent,
    int test_percent)
{
    const std::array<int, 3> percentages{
        train_percent, validation_percent, test_percent};
    std::array<size_t, 3> counts{};
    std::array<double, 3> remainders{};
    size_t assigned = 0;
    for (size_t index = 0; index < counts.size(); ++index) {
        const double exact =
            static_cast<double>(sample_count) * percentages[index] / 100.0;
        counts[index] = static_cast<size_t>(std::floor(exact));
        remainders[index] = exact - counts[index];
        assigned += counts[index];
    }
    while (assigned < sample_count) {
        const auto best = static_cast<size_t>(std::distance(
            remainders.begin(),
            std::max_element(remainders.begin(), remainders.end())));
        ++counts[best];
        remainders[best] = -1.0;
        ++assigned;
    }
    if (sample_count >= 2 && train_percent > 0 && validation_percent > 0 &&
        (counts[0] == 0 || counts[1] == 0)) {
        const size_t missing = counts[0] == 0 ? 0 : 1;
        const size_t donor = counts[0] > counts[1] ? 0 : 1;
        if (counts[donor] > 1) {
            --counts[donor];
            ++counts[missing];
        }
    }
    return counts;
}


} // namespace

TaskWorkspaceService::TaskWorkspaceService(WorkflowGraph& graph, int node_id, const QString& node_directory)
    : graph_(&graph), node_id_(node_id), node_directory_(node_directory)
{
    if (graph.nodes().count(node_id) == 0 ||
        graph.node(node_id).kind != NodeKind::TaskConfig)
        throw std::runtime_error("TaskWorkspaceService requires a Task node");
    if (!node_directory_.isEmpty() && (!QFileInfo(node_directory_).isAbsolute() ||
        QFileInfo(node_directory_).fileName() != QString::number(node_id)))
        throw std::runtime_error("model persistence requires the owning Node directory");
}

int TaskWorkspaceService::source_node_id() const {
    return binding().source_node_id;
}

TaskNodeConfig& TaskWorkspaceService::task() {
    return std::get<TaskNodeConfig>(graph_->node(node_id_).config);
}

const TaskNodeConfig& TaskWorkspaceService::task() const {
    return std::get<TaskNodeConfig>(graph_->node(node_id_).config);
}

const std::vector<jocelin::model::ModelEntry>& TaskWorkspaceService::models() const {
    return task().model_list.entries();
}

jocelin::model::ModelEntry* TaskWorkspaceService::find_model(const QString& id) {
    return task().model_list.find(id.toStdString());
}

const jocelin::model::ModelEntry* TaskWorkspaceService::find_model(const QString& id) const {
    return task().model_list.find(id.toStdString());
}

jocelin::model::ModelEntry TaskWorkspaceService::add_model(jocelin::model::ModelEntry model) {
    if (model.task != jocelin::config::tool_type_from_string(task().task))
        throw std::runtime_error("model task does not match the owning TaskNode");
    if (model.created_at.empty())
        model.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    return task().model_list.add(std::move(model));
}

bool TaskWorkspaceService::remove_model(const QString& id) {
    if (!find_model(id)) return false;
    if (!node_directory_.isEmpty()) {
        if (id.contains('/') || id.contains('\\') || id.contains(':') ||
            ProjectStorage::validated_relative_path(id, "model") != id)
            throw std::runtime_error("model id must be one directory component");
        const auto directory = QDir(node_directory_).filePath("inference/" + id);
        if (QDir(directory).exists() && !QDir(directory).removeRecursively())
            throw std::runtime_error("cannot remove the model's Node-owned inference directory");
    }
    if (!task().model_list.remove(id.toStdString())) return false;
    if (task().runtime.training_model_id == id.toStdString())
        task().runtime.training_model_id.clear();
    auto& results = task().runtime.results;
    results.erase(std::remove_if(results.begin(), results.end(),
        [&id](const auto& result) { return result.model_id == id.toStdString(); }), results.end());
    if (selected_model_id_ == id) select_inference_result({}, {});
    return true;
}

void TaskWorkspaceService::clear_models() {
    if (!node_directory_.isEmpty()) {
        const auto directory = QDir(node_directory_).filePath("inference");
        if (QDir(directory).exists() && !QDir(directory).removeRecursively())
            throw std::runtime_error("cannot clear the TaskNode inference directory");
    }
    task().model_list.clear();
    task().runtime.training_model_id.clear();
    task().runtime.results.clear();
    select_inference_result({}, {});
}

const InferenceResultSet* TaskWorkspaceService::inference_result(const QString& model_id, const QString& weight_id) const {
    const auto* model = find_model(model_id);
    if (!model || !jocelin::model::find_weight(*model, weight_id.toStdString())) return nullptr;
    for (const auto& result : task().runtime.results)
        if (result.model_id == model_id.toStdString() && result.weight_id == weight_id.toStdString())
            return &result;
    return nullptr;
}

void TaskWorkspaceService::select_inference_result(const QString& model_id, const QString& weight_id) {
    if (selected_model_id_ == model_id && selected_weight_id_ == weight_id) return;
    selected_model_id_ = model_id;
    selected_weight_id_ = weight_id;
    if (inference_selection_changed) inference_selection_changed();
}

void TaskWorkspaceService::select_inference_model(const QString& model_id) {
    const auto* model = find_model(model_id);
    if (!model) { select_inference_result({}, {}); return; }
    const auto* weight = jocelin::model::find_weight(*model, selected_weight_id_.toStdString());
    if (!weight && !model->weights.empty()) weight = &model->weights.front();
    select_inference_result(model_id, weight ? QString::fromStdString(weight->id) : QString{});
}

const InferenceResultSet* TaskWorkspaceService::selected_inference_result() const {
    return inference_result(selected_model_id_, selected_weight_id_);
}

TaskDatasetBinding& TaskWorkspaceService::binding() {
    return task().dataset_binding;
}

const TaskDatasetBinding& TaskWorkspaceService::binding() const {
    return task().dataset_binding;
}

const DatasetConfig& TaskWorkspaceService::source_dataset() const {
    const int source_node_id = this->source_node_id();
    if (source_node_id == 0 || graph_->nodes().count(source_node_id) == 0 ||
        graph_->node(source_node_id).kind != NodeKind::DatasetInput)
        throw std::runtime_error("connect a Dataset node to this Task");
    return std::get<DatasetConfig>(graph_->node(source_node_id).config);
}

bool TaskWorkspaceService::synchronize(std::vector<std::string>* notes) {
    auto result = synchronize_task_dataset_binding(*graph_, node_id_);
    if (source_node_id() == 0) {
        if (notes) *notes = result.notes;
        return result.changed;
    }
    const bool has_new_auto_samples = std::any_of(
        binding().samples.begin(), binding().samples.end(),
        [](const TaskSampleBinding& sample) {
            return sample.selected && !sample.split_manual &&
                   sample.split == "unassigned";
        });
    if (has_new_auto_samples) {
        const auto settings = binding().split_settings;
        assign_reproducible_split(
            settings.train_percent, settings.validation_percent,
            settings.test_percent, settings.seed);
        result.changed = true;
    }
    refresh_quality();
    if (notes) *notes = result.notes;
    return result.changed;
}

void TaskWorkspaceService::replace_bound_dataset(
    DatasetConfig dataset,
    TaskDatasetBinding imported_binding)
{
    const int source_node_id = this->source_node_id();
    if (source_node_id == 0 || graph_->nodes().count(source_node_id) == 0 ||
        graph_->node(source_node_id).kind != NodeKind::DatasetInput)
        throw std::runtime_error("connect a Dataset node to this Task");
    // The persistence owner clears disk references/files before invoking this
    // in-memory command. All currently connected Tasks receive a fresh binding.
    for (const auto& edge : graph_->connections())
        if (edge.from_node_id == source_node_id && graph_->node(edge.to_node_id).kind == NodeKind::TaskConfig)
            clear_task_dataset_state(std::get<TaskNodeConfig>(graph_->node(edge.to_node_id).config));
    std::get<DatasetConfig>(
        graph_->node(source_node_id).config) = std::move(dataset);
    // Import explicitly supplies new annotations/classes; Task settings survive.
    binding().classes = std::move(imported_binding.classes);
    binding().samples = std::move(imported_binding.samples);
    for (const auto& edge : graph_->connections())
        if (edge.from_node_id == source_node_id && graph_->node(edge.to_node_id).kind == NodeKind::TaskConfig)
            TaskWorkspaceService(*graph_, edge.to_node_id).synchronize();
}

TaskDatasetStats TaskWorkspaceService::dataset_stats() const {
    TaskDatasetStats stats;
    stats.bucket_capacity = std::max(1, binding().bucket_capacity);
    stats.samples = static_cast<int>(binding().samples.size());
    for (const auto& sample : binding().samples) {
        stats.images += static_cast<int>(sample.image_paths.size());
        stats.selected += sample.selected ? 1 : 0;
        const bool valid = !sample.image_paths.empty() &&
            std::all_of(sample.image_paths.begin(), sample.image_paths.end(),
                        [](const std::string& path) {
                            return QFileInfo(QString::fromStdString(path)).isFile();
                        });
        stats.valid += valid ? 1 : 0;
        stats.invalid += valid ? 0 : 1;
        const bool annotated =
            sample.annotation_state == "annotated";
        stats.annotated += annotated ? 1 : 0;
        stats.unannotated += annotated ? 0 : 1;
        stats.quality_issues += sample.quality_issues.empty() ? 0 : 1;
    }
    return stats;
}

TaskSplitStats TaskWorkspaceService::split_stats() const {
    TaskSplitStats stats;
    for (const auto& sample : binding().samples) {
        if (!sample.selected) continue;
        const std::string split = normalized_split(sample.split);
        if (split == "train") ++stats.train;
        else if (split == "validation") ++stats.validation;
        else if (split == "test") ++stats.test;
        else ++stats.unassigned;
    }
    if (stats.validation == 0 && stats.train > 0) {
        stats.derived_validation = stats.train == 1
            ? 1 : std::max(1, static_cast<int>(
                std::lround(stats.train * 0.20)));
        // Derived validation preserves explicit train assignments. The
        // selected samples are therefore shared with validation.
        stats.shared_validation = stats.derived_validation;
    }
    return stats;
}

void TaskWorkspaceService::refresh_quality() {
    // Detached bindings retain their last verified task targets until rebind.
    if (source_node_id() == 0) return;
    for (auto& sample : binding().samples) {
        sample.quality_issues = structural_issues(sample);
        for (const auto& image_path : sample.image_paths) {
            QImageReader reader(QString::fromStdString(image_path));
            reader.setDecideFormatFromContent(true);
            if (!QFileInfo(QString::fromStdString(image_path)).isFile() || !reader.canRead())
                add_issue(sample.quality_issues, "source image is missing or unreadable");
        }
        if (task().task == "cls")
            refresh_classification_annotation(binding().classes, sample);
        else if (task().task == "sg")
            refresh_segmentation_annotation(binding().classes, sample);
    }
}

void TaskWorkspaceService::set_segmentation_sample_empty(const std::string& sample_id) {
    if (task().task != "sg")
        throw std::runtime_error("empty targets are not supported by this Task");
    confirm_empty_segmentation_annotation(require_sample(sample_id));
    refresh_quality();
}

TaskSampleBinding& TaskWorkspaceService::require_sample(const std::string& sample_id) {
    auto found = std::find_if(binding().samples.begin(), binding().samples.end(),
                              [&sample_id](const TaskSampleBinding& value) {
                                  return value.sample_id == sample_id;
                              });
    if (found == binding().samples.end()) throw std::runtime_error("Task sample does not exist");
    return *found;
}

AnnotationClass& TaskWorkspaceService::require_class(const std::string& class_id) {
    auto found = std::find_if(binding().classes.begin(), binding().classes.end(),
                              [&class_id](const AnnotationClass& value) {
                                  return value.id == class_id;
                              });
    if (found == binding().classes.end()) throw std::runtime_error("Annotation class does not exist");
    return *found;
}

void TaskWorkspaceService::set_sample_selected(const std::string& sample_id, bool selected) {
    auto& sample = require_sample(sample_id);
    if (sample.selected == selected) return;
    sample.selected = selected;
}

void TaskWorkspaceService::set_sample_split(const std::string& sample_id, const std::string& split) {
    auto& sample = require_sample(sample_id);
    const auto value = normalized_split(split);
    if (sample.split == value && sample.split_manual) return;
    sample.split = value;
    sample.split_manual = true;
}

void TaskWorkspaceService::restore_automatic_split(const std::string& sample_id) {
    auto& sample = require_sample(sample_id);
    sample.split_manual = false;
    const auto settings = binding().split_settings;
    assign_reproducible_split(
        settings.train_percent, settings.validation_percent,
        settings.test_percent, settings.seed);
}

void TaskWorkspaceService::assign_reproducible_split(
    int train_percent,
    int validation_percent,
    int test_percent,
    std::uint32_t seed,
    bool replace_manual_overrides)
{
    if (train_percent < 0 || validation_percent < 0 || test_percent < 0 ||
        train_percent + validation_percent + test_percent != 100)
        throw std::runtime_error("split percentages must be non-negative and total 100");

    auto& settings = binding().split_settings;
    settings.train_percent = train_percent;
    settings.validation_percent = validation_percent;
    settings.test_percent = test_percent;
    settings.seed = seed;
    settings.strategy = "stratified";

    std::array<size_t, 3> manual_counts{};
    size_t selected_count = 0;
    std::map<std::string, std::vector<size_t>> strata;
    for (size_t index = 0; index < binding().samples.size(); ++index) {
        auto& sample = binding().samples[index];
        if (!sample.selected) continue;
        ++selected_count;
        if (replace_manual_overrides) sample.split_manual = false;
        if (sample.split_manual && sample.split != "unassigned") {
            if (sample.split == "train") ++manual_counts[0];
            else if (sample.split == "validation") ++manual_counts[1];
            else if (sample.split == "test") ++manual_counts[2];
            continue;
        }
        sample.split_manual = false;
        strata[split_stratum(task(), sample)].push_back(index);
    }

    std::mt19937 generator(seed);
    for (auto& [name, indices] : strata) {
        std::seed_seq stratum_seed{
            seed,
            static_cast<std::uint32_t>(std::hash<std::string>{}(name)),
            static_cast<std::uint32_t>(indices.size())};
        std::mt19937 stratum_generator(stratum_seed);
        std::shuffle(indices.begin(), indices.end(), stratum_generator);
    }
    std::vector<size_t> automatic;
    for (size_t offset = 0;; ++offset) {
        bool appended = false;
        for (const auto& [name, indices] : strata) {
            if (offset >= indices.size()) continue;
            automatic.push_back(indices[offset]);
            appended = true;
        }
        if (!appended) break;
    }
    if (!automatic.empty())
        std::rotate(
            automatic.begin(),
            automatic.begin() + generator() % automatic.size(),
            automatic.end());

    const auto desired = desired_split_counts(
        selected_count, train_percent, validation_percent, test_percent);
    std::array<size_t, 3> remaining{};
    size_t remaining_total = 0;
    for (size_t index = 0; index < remaining.size(); ++index) {
        remaining[index] =
            desired[index] > manual_counts[index]
                ? desired[index] - manual_counts[index] : 0;
        remaining_total += remaining[index];
    }
    while (remaining_total < automatic.size()) {
        size_t best = 0;
        for (size_t index = 1; index < desired.size(); ++index) {
            const double best_fill = desired[best] == 0
                ? std::numeric_limits<double>::infinity()
                : static_cast<double>(manual_counts[best] + remaining[best]) /
                      desired[best];
            const double candidate_fill = desired[index] == 0
                ? std::numeric_limits<double>::infinity()
                : static_cast<double>(manual_counts[index] + remaining[index]) /
                      desired[index];
            if (candidate_fill < best_fill) best = index;
        }
        ++remaining[best];
        ++remaining_total;
    }
    while (remaining_total > automatic.size()) {
        const auto best = static_cast<size_t>(std::distance(
            remaining.begin(), std::max_element(remaining.begin(), remaining.end())));
        if (remaining[best] == 0) break;
        --remaining[best];
        --remaining_total;
    }

    static const std::array<const char*, 3> names{
        "train", "validation", "test"};
    size_t cursor = 0;
    for (size_t split_index = 0; split_index < remaining.size(); ++split_index)
        for (size_t count = 0; count < remaining[split_index] &&
                               cursor < automatic.size(); ++count)
            binding().samples[automatic[cursor++]].split = names[split_index];
}

void TaskWorkspaceService::set_segmentation_mask_for_image(
    const std::string& image_path,
    const QImage& mask)
{
    const QString absolute = QFileInfo(QString::fromStdString(image_path)).absoluteFilePath();
    for (auto& sample : binding().samples) {
        const bool owns_image = std::any_of(
            sample.image_paths.begin(), sample.image_paths.end(),
            [&absolute](const std::string& candidate) {
                return QFileInfo(QString::fromStdString(candidate)).absoluteFilePath() == absolute;
            });
        if (!owns_image) continue;
        commit_segmentation_mask(binding().classes, sample, mask);
        refresh_quality();
        return;
    }
    throw std::runtime_error("saved mask does not belong to the Task binding");
}

void TaskWorkspaceService::set_class_for_sample(
    const std::string& sample_id,
    const std::string& class_id)
{
    const auto& schema = require_class(class_id);
    if (!schema.active) throw std::runtime_error("cannot assign a retired class");
    auto& sample = require_sample(sample_id);
    sample.class_id = class_id;
    refresh_classification_annotation(binding().classes, sample);
}

std::string TaskWorkspaceService::add_class(
    const std::string& name,
    const std::string& color)
{
    if (name.empty()) throw std::runtime_error("class name cannot be empty");
    if (std::any_of(binding().classes.begin(), binding().classes.end(),
                    [&name](const AnnotationClass& value) {
                        return value.active && equal_folded(value.name, name);
                    }))
        throw std::runtime_error("class name already exists");
    if (task().task == "cls") {
        const std::string id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        binding().classes.push_back({id, 0, name,
            color.empty() ? default_class_color(static_cast<int>(binding().classes.size()) + 1) : color,
            true, false, true});
        return id;
    }
    std::set<int> used;
    std::set<std::string> ids;
    for (const auto& value : binding().classes) {
        used.insert(value.mask_value);
        ids.insert(value.id);
    }
    int mask_value = 1;
    while (mask_value <= 254 && used.count(mask_value) != 0) ++mask_value;
    if (mask_value > 254)
        throw std::runtime_error(
            "training reserves class zero for background and supports at most 254 foreground classes");
    std::string id = "class-" + stable_sample_id({name, std::to_string(mask_value)}).substr(7);
    for (int suffix = 2; ids.count(id) != 0; ++suffix)
        id = "class-" + stable_sample_id({name, std::to_string(mask_value), std::to_string(suffix)}).substr(7);
    binding().classes.push_back(
        {id, mask_value, name, color.empty() ? default_class_color(mask_value) : color,
         true, false, true});
    return id;
}

void TaskWorkspaceService::rename_class(const std::string& class_id, const std::string& name) {
    if (name.empty()) throw std::runtime_error("class name cannot be empty");
    if (std::any_of(binding().classes.begin(), binding().classes.end(),
                    [&class_id, &name](const AnnotationClass& value) {
                        return value.id != class_id && value.active && equal_folded(value.name, name);
                    }))
        throw std::runtime_error("class name already exists");
    auto& value = require_class(class_id);
    value.name = name;
}

void TaskWorkspaceService::retire_class(const std::string& class_id) {
    auto& value = require_class(class_id);
    if (!value.active) return;
    value.active = false;
    value.visible = false;
    value.locked = true;
    refresh_quality();
}

void TaskWorkspaceService::move_class(const std::string& class_id, int direction) {
    auto& classes = binding().classes;
    auto found = std::find_if(classes.begin(), classes.end(),
                              [&class_id](const AnnotationClass& value) {
                                  return value.id == class_id;
                              });
    if (found == classes.end()) throw std::runtime_error("Annotation class does not exist");
    const auto index = std::distance(classes.begin(), found);
    const auto target = index + (direction < 0 ? -1 : 1);
    if (target < 0 || target >= static_cast<decltype(index)>(classes.size())) return;
    std::iter_swap(classes.begin() + index, classes.begin() + target);
}

void TaskWorkspaceService::set_class_visible(const std::string& class_id, bool visible) {
    auto& value = require_class(class_id);
    if (value.visible == visible) return;
    value.visible = visible;
}

void TaskWorkspaceService::set_class_locked(const std::string& class_id, bool locked) {
    auto& value = require_class(class_id);
    if (value.locked == locked) return;
    value.locked = locked;
}

jocelin::training::TrainingRequest TaskWorkspaceService::build_training_request(
    const jocelin::config::Device& device,
    jocelin::config::WeightSource weight_source,
    const jocelin::model::ModelEntry* resume_model) const
{
    const auto& config = task();
    const auto tool_type = jocelin::config::tool_type_from_string(config.task);
    const auto model_scale = jocelin::config::model_scale_from_string(config.model_scale);
    if (!supports_model_spec(config.task, config.model_index, config.model_scale))
        throw std::runtime_error("model spec is not registered for the selected task");
    if (config.training.output_directory.empty())
        throw std::runtime_error("configure a training output directory");

    int maximum_mask_value = 0;
    std::map<std::string, int> classification_labels;
    std::vector<std::string> class_names;
    if (tool_type == jocelin::config::ToolType::Cls) {
        auto ordered_classes = classification_classes(binding().classes);
        if (resume_model) {
            ordered_classes.clear();
            for (const auto& model_class : resume_model->classes) {
                const auto* schema = find_annotation_class(binding(), model_class.id);
                if (!schema || !schema->active)
                    throw std::runtime_error("paused model class schema does not match the current task");
                ordered_classes.push_back(schema);
            }
            if (ordered_classes.size() != classification_classes(binding().classes).size())
                throw std::runtime_error("paused model class schema does not match the current task");
        }
        for (const auto* schema : ordered_classes) {
            class_names.push_back(schema->name);
            classification_labels.emplace(schema->id, static_cast<int>(class_names.size()));
        }
    } else {
        for (const auto& schema : binding().classes) {
            maximum_mask_value = std::max(maximum_mask_value, schema.mask_value);
        }
    }
    if (maximum_mask_value == 0 && classification_labels.empty())
        throw std::runtime_error("add at least one annotation class before training");
    if (tool_type == jocelin::config::ToolType::Sg && maximum_mask_value > 255)
        throw std::runtime_error(
            "segmentation training reserves class zero for background and supports mask values up to 255");

    jocelin::meta::SampleSet samples;
    samples.dataset_root = source_dataset().directory;
    samples.dataset_name =
        QFileInfo(QString::fromStdString(source_dataset().directory)).fileName().toStdString();
    if (samples.dataset_name.empty()) samples.dataset_name = "qt-project-dataset";
    bool has_train = false;
    std::vector<std::string> malformed;
    for (const auto& sample_binding : binding().samples) {
        if (!sample_binding.selected) continue;
        auto verified = sample_binding;
        if (tool_type == jocelin::config::ToolType::Cls)
            refresh_classification_annotation(binding().classes, verified);
        if (verified.annotation_state != "annotated") continue;
        const std::string split = normalized_split(sample_binding.split);
        if (split == "unassigned") continue;
        if (!sample_binding.quality_issues.empty()) {
            malformed.push_back(sample_binding.sample_id);
            continue;
        }
        jocelin::meta::AiMetaData sample;
        sample.dataset_id = samples.dataset_name;
        sample.sample_id = sample_binding.sample_id;
        sample.split = split == "validation"
            ? jocelin::meta::AiMetaData::Split::Validation
            : jocelin::meta::AiMetaData::Split::Train;
        sample.image_paths = sample_binding.image_paths;
        sample.annotation_state = jocelin::meta::AiMetaData::AnnotationState::Annotated;
        auto exclusion = training_mask_region(sample_binding.training_mask_path);
        if (!exclusion.empty()) sample.mask.push_back(std::move(exclusion));
        has_train = has_train || split == "train";
        if (tool_type == jocelin::config::ToolType::Sg) {
            sample.regions = sample_binding.regions;
        } else if (tool_type == jocelin::config::ToolType::Cls) {
            sample.class_label = classification_labels.at(sample_binding.class_id);
        }
        samples.samples.push_back(std::move(sample));
    }
    if (!malformed.empty())
        throw std::runtime_error(
            "selected split contains a malformed sample; first: " +
            malformed.front());
    if (samples.empty()) throw std::runtime_error("selected Task data produced no trainable samples");
    if (!has_train) throw std::runtime_error("assign at least one selected sample to the train split");

    const bool has_explicit_validation = std::any_of(
        samples.samples.begin(), samples.samples.end(),
        [](const jocelin::meta::AiMetaData& sample) {
            return sample.split == jocelin::meta::AiMetaData::Split::Validation;
        });
    if (!has_explicit_validation) {
        std::map<std::string, std::vector<size_t>> validation_strata;
        for (size_t index = 0; index < samples.samples.size(); ++index) {
            if (samples.samples[index].split != jocelin::meta::AiMetaData::Split::Train) continue;
            const auto found = std::find_if(
                binding().samples.begin(), binding().samples.end(),
                [&](const TaskSampleBinding& candidate) {
                    return candidate.sample_id ==
                        samples.samples[index].sample_id;
                });
            const std::string stratum = found == binding().samples.end()
                ? "unknown"
                : split_stratum(task(), *found);
            validation_strata[stratum].push_back(index);
        }
        for (auto& [name, indices] : validation_strata) {
            std::seed_seq stratum_seed{
                binding().split_settings.seed,
                static_cast<std::uint32_t>(
                    std::hash<std::string>{}(name)),
                static_cast<std::uint32_t>(indices.size())};
            std::mt19937 generator(stratum_seed);
            std::shuffle(indices.begin(), indices.end(), generator);
        }
        std::vector<size_t> train_indices;
        for (size_t offset = 0;; ++offset) {
            bool appended = false;
            for (const auto& [name, indices] : validation_strata) {
                if (offset >= indices.size()) continue;
                train_indices.push_back(indices[offset]);
                appended = true;
            }
            if (!appended) break;
        }
        std::mt19937 validation_generator(
            binding().split_settings.seed);
        if (!train_indices.empty())
            std::rotate(
                train_indices.begin(),
                train_indices.begin() +
                    validation_generator() % train_indices.size(),
                train_indices.end());
        const size_t derived_count = train_indices.size() == 1
            ? 1
            : std::max<size_t>(
                1, static_cast<size_t>(
                    std::lround(train_indices.size() * 0.20)));
        for (size_t offset = 0;
             offset < std::min(derived_count, train_indices.size());
             ++offset) {
            auto validation =
                samples.samples[train_indices[offset]];
            validation.split = jocelin::meta::AiMetaData::Split::Validation;
            validation.extra.emplace_back(
                "split_origin", "derived-validation");
            samples.samples.push_back(std::move(validation));
        }
    }

    jocelin::config::AiConfig backend;
    backend.tool_type = tool_type;
    backend.runtime_mode = jocelin::config::RuntimeMode::Train;
    backend.model_index = config.model_index;
    backend.model_scale = model_scale;
    backend.model_embed_dim = 768;
    backend.model_depth = 12;
    backend.model_num_heads = 12;
    backend.batch_size = config.training.batch_size;
    backend.bucket_capacity = std::max(1, source_dataset().bucket_capacity);
    backend.num_classes = tool_type == jocelin::config::ToolType::Cls
        ? static_cast<int>(class_names.size()) : maximum_mask_value + 1;
    backend.num_epochs = config.training.epochs;
    backend.learning_rate = config.training.learning_rate;
    backend.weight_decay = config.training.weight_decay;
    backend.image_width = config.training.image_width;
    backend.image_height = config.training.image_height;
    backend.num_workers = config.training.workers;
    backend.val_batch_size = 0;
    backend.val_num_workers = -1;
    backend.train_drop_last = true;
    backend.optimizer_type = config.training.optimizer;
    backend.momentum = 0.9f;
    backend.adam_beta1 = 0.9f;
    backend.adam_beta2 = 0.999f;
    backend.scheduler_type = config.training.scheduler;
    backend.scheduler_step_size = 30;
    backend.scheduler_gamma = 0.1f;
    backend.scheduler_t_max = config.training.epochs;
    backend.save_interval = 1;
    backend.model_save_dir = config.training.output_directory;
    backend.weight_type = jocelin::config::WeightType::LibTorch;
    backend.device = device;
    backend.augment = config.training.augment;
    backend.normalize = true;
    backend.bgr_to_rgb = true;
    backend.mean[0] = 0.485f;
    backend.mean[1] = 0.456f;
    backend.mean[2] = 0.406f;
    backend.std[0] = 0.229f;
    backend.std[1] = 0.224f;
    backend.std[2] = 0.225f;
    backend.loss_type = jocelin::config::LossType::Auto;
    backend.metric_type = jocelin::config::MetricType::Auto;

    if (tool_type == jocelin::config::ToolType::Sg) {
        class_names.resize(static_cast<size_t>(backend.num_classes));
        class_names.front() = "background";
        for (const auto& schema : binding().classes) {
            if (schema.mask_value <= 0 || schema.mask_value > maximum_mask_value) continue;
            class_names[static_cast<size_t>(schema.mask_value)] =
                schema.active ? schema.name : "[retired] " + schema.name;
        }
        for (size_t index = 0; index < class_names.size(); ++index)
            if (class_names[index].empty())
                class_names[index] = "[reserved] " + std::to_string(index);
    }

    jocelin::training::TrainingRequest request;
    request.config = backend;
    request.samples = std::move(samples);
    request.class_names = std::move(class_names);
    request.weight_source = std::move(weight_source);
    return request;
}

jocelin::inference::InferenceRequest TaskWorkspaceService::build_inference_request(
    const jocelin::config::Device& device,
    const jocelin::model::ModelEntry& model,
    const jocelin::model::ModelWeight& weight, const std::string& output_directory) const
{
    const auto& config = task();
    const auto& output = output_directory.empty() ? config.inference_output_directory : output_directory;
    if (weight.path.empty()) throw std::runtime_error("select a model weight");
    const auto task_type = jocelin::config::tool_type_from_string(config.task);
    if (model.task != task_type)
        throw std::runtime_error("selected checkpoint belongs to a different task repository");
    const std::string model_scale = jocelin::config::to_string(model.model_scale);
    if (!supports_model_spec(config.task, model.model_index, model_scale))
        throw std::runtime_error("checkpoint model spec is not registered for the selected task");
    if (output.empty())
        throw std::runtime_error("configure an inference output directory");

    const std::set<std::string> requested(
        config.inference_images.begin(), config.inference_images.end());
    std::vector<std::string> images;
    std::vector<jocelin::inference::InferenceTarget> targets;
    for (const auto& sample : binding().samples) {
        if (!sample.selected) continue;
        jocelin::inference::InferenceTarget target;
        auto verified = sample;
        if (task_type == jocelin::config::ToolType::Cls)
            refresh_classification_annotation(binding().classes, verified);
        target.available = verified.annotation_state == "annotated";
        if (target.available && task_type == jocelin::config::ToolType::Cls) {
            const auto found = std::find_if(model.classes.begin(), model.classes.end(),
                [&](const auto& value) { return value.id == sample.class_id; });
            target.available = found != model.classes.end();
            if (target.available)
                target.class_label = static_cast<int>(std::distance(model.classes.begin(), found)) + 1;
        } else if (target.available && task_type == jocelin::config::ToolType::Sg) {
            target.regions = sample.regions;
        }
        for (const auto& image : sample.image_paths) {
            const auto name = QFileInfo(QString::fromStdString(image)).fileName().toStdString();
            if (config.inference_use_all_images || requested.count(image) != 0 ||
                requested.count(name) != 0) {
                images.push_back(image);
                targets.push_back(target);
            }
        }
    }
    if (images.empty()) throw std::runtime_error("select at least one valid Task image for inference");

    jocelin::config::AiConfig backend;
    backend.tool_type = model.task;
    backend.runtime_mode = jocelin::config::RuntimeMode::Infer;
    backend.model_index = model.model_index;
    backend.model_scale = model.model_scale;
    backend.model_embed_dim = 768;
    backend.model_depth = 12;
    backend.model_num_heads = 12;
    backend.batch_size = config.inference_batch_size;
    backend.num_classes = static_cast<int>(model.classes.size()) +
        (task_type == jocelin::config::ToolType::Sg ? 1 : 0);
    backend.image_width = model.image_width;
    backend.image_height = model.image_height;
    backend.weight_type = weight.format;
    backend.device = device;
    backend.require_checkpoint_match = true;
    backend.use_checkpoint_config = true;
    backend.allow_partial_checkpoint_load = false;
    backend.normalize = true;
    backend.mean[0] = 0.485f;
    backend.mean[1] = 0.456f;
    backend.mean[2] = 0.406f;
    backend.std[0] = 0.229f;
    backend.std[1] = 0.224f;
    backend.std[2] = 0.225f;
    backend.confidence_threshold = 0.5f;
    backend.min_region_area = 1;
    backend.max_region_area = std::numeric_limits<int64_t>::max();
    backend.aspect_ratio_range = {0.0, std::numeric_limits<double>::max()};
    backend.min_component_confidence = 0.0;
    backend.cls_confidence_threshold = 0.0;
    backend.morphological_refine = true;
    backend.morph_kernel_size = 5;
    backend.morph_iterations = 1;

    jocelin::inference::InferenceRequest request;
    request.config = backend;
    request.weight_path = weight.path;
    request.selected_weight_id = weight.id;
    request.available_weights = model.weights;
    request.image_paths = std::move(images);
    request.targets = std::move(targets);
    request.segmentation_result.save_dir = output;
    request.segmentation_result.save_option =
        jocelin::result::sg::SaveOption::ResultWithHeatmapOverlay;
    request.segmentation_result.save_color_mask = true;
    request.segmentation_result.overlay_alpha = 0.45f;
    request.classification_result.save_dir = output;
    return request;
}

void TaskWorkspaceService::record_training_started(const std::string& model_id) {
    if (model_id.empty())
        throw std::runtime_error("training model id cannot be empty");
    task().runtime.training_model_id = model_id;
}

void TaskWorkspaceService::record_inference_started() {
    task().runtime.inference_status = "running";
}

void TaskWorkspaceService::record_inference_result(
    const jocelin::inference::InferenceReport& report,
    const std::string& model_id,
    const std::string& weight_id,
    jocelin::config::WeightType weight_format,
    const std::string& output_directory,
    const std::string& error)
{
    auto& runtime = task().runtime;
    runtime.inference_status = error.empty() ? "completed" : "error";
    if (!error.empty()) return;

    const auto* model = task().model_list.find(model_id);
    const auto* weight = model ? jocelin::model::find_weight(*model, weight_id) : nullptr;
    if (!weight || weight->format != weight_format)
        throw std::runtime_error("inference result must reference this TaskNode's model and weight");
    InferenceResultSet set;
    set.model_id = model_id;
    set.weight_id = weight_id;
    set.weight_format = jocelin::config::to_string(weight_format);
    set.result_directory = output_directory;

    const auto sample_id_for = [this](const std::string& image_path) {
        const auto path = QFileInfo(QString::fromStdString(image_path)).absoluteFilePath();
        for (const auto& sample : binding().samples) {
            if (std::any_of(sample.image_paths.begin(), sample.image_paths.end(), [&path](const auto& input) {
                    return QFileInfo(QString::fromStdString(input)).absoluteFilePath() == path;
                }))
                return sample.sample_id;
        }
        throw std::runtime_error("inference result has no bound source_sample_id");
    };

    if (const auto* cls = std::get_if<jocelin::result::cls::ClsInferenceResult>(&report)) {
        set.aggregate_metrics = cls->metrics;
        for (const auto& record : cls->samples) {
            ResultReference stored;
            stored.source_sample_id = sample_id_for(record.image_path);
            stored.result = record;
            set.samples.push_back(std::move(stored));
        }
    } else {
        const auto& sg = std::get<jocelin::result::sg::SgInferenceResult>(report);
        for (const auto& record : sg.samples) {
            ResultReference stored;
            stored.source_sample_id = sample_id_for(record.image_path);
            stored.result = record;
            set.samples.push_back(std::move(stored));
        }
    }

    runtime.results.erase(
        std::remove_if(
            runtime.results.begin(), runtime.results.end(),
            [&model_id, &weight_id](const InferenceResultSet& existing) {
                return existing.model_id == model_id &&
                    existing.weight_id == weight_id;
            }),
        runtime.results.end());
    runtime.results.push_back(std::move(set));
}

} // namespace studio
