#pragma once

#include "workflow.h"

#include <checkpoint/checkpoint_catalog.h>
#include <common/model/model_list.h>
#include <inference/inference_service.h>
#include <training/training_service.h>

#include <QString>

#include <cstdint>
#include <string>
#include <vector>

namespace studio {

struct TaskDatasetStats {
    int samples = 0;
    int images = 0;
    int selected = 0;
    int valid = 0;
    int invalid = 0;
    int annotated = 0;
    int unannotated = 0;
    int quality_issues = 0;
    int bucket_capacity = 1;
};

struct TaskSplitStats {
    int train = 0;
    int validation = 0;
    int derived_validation = 0;
    int shared_validation = 0;
    int test = 0;
    int unassigned = 0;
};

// Application service for the single-Task product flow. Views consume this
// typed API instead of traversing edges or reading std::variant values.
class TaskWorkspaceService final {
public:
    TaskWorkspaceService(WorkflowGraph& graph, int task_node_id);

    int task_node_id() const { return task_node_id_; }
    int dataset_node_id() const;
    TaskNodeConfig& task();
    const TaskNodeConfig& task() const;
    TaskDatasetBinding& binding();
    const TaskDatasetBinding& binding() const;
    const DatasetConfig& source_dataset() const;

    bool synchronize(std::vector<std::string>* notes = nullptr);
    void replace_bound_dataset(
        DatasetConfig dataset,
        TaskDatasetBinding binding);
    TaskDatasetStats dataset_stats() const;
    TaskSplitStats split_stats() const;
    void refresh_quality();

    void set_sample_selected(const std::string& sample_id, bool selected);
    void set_sample_split(const std::string& sample_id, const std::string& split);
    void restore_automatic_split(const std::string& sample_id);
    void assign_reproducible_split(int train_percent,
                                   int validation_percent,
                                   int test_percent,
                                   std::uint32_t seed,
                                   bool replace_manual_overrides = false);
    void set_mask_path_for_image(const std::string& image_path,
                                 const std::string& mask_path);
    void set_class_for_sample(const std::string& sample_id,
                              const std::string& class_id);

    std::string add_class(const std::string& name, const std::string& color = {});
    void rename_class(const std::string& class_id, const std::string& name);
    void retire_class(const std::string& class_id);
    void move_class(const std::string& class_id, int direction);
    void set_class_visible(const std::string& class_id, bool visible);
    void set_class_locked(const std::string& class_id, bool locked);

    jocelin::training::TrainingRequest build_training_request(
        const jocelin::config::Device& device,
        jocelin::config::WeightSource weight_source = {}) const;
    jocelin::inference::InferenceRequest build_inference_request(
        const jocelin::config::Device& device,
        const jocelin::model::ModelEntry& model,
        const jocelin::model::ModelWeight& weight) const;

    void record_training_started(const std::string& model_id);
    void record_inference_started();
    void record_inference_result(
        const jocelin::inference::InferenceReport& report,
        const std::string& model_id,
        const std::string& weight_id,
        jocelin::config::WeightType weight_format,
        const std::string& output_directory,
        const std::string& error);

private:
    TaskSampleBinding& require_sample(const std::string& sample_id);
    AnnotationClass& require_class(const std::string& class_id);

    WorkflowGraph* graph_ = nullptr;
    int task_node_id_ = 0;
};

} // namespace studio
