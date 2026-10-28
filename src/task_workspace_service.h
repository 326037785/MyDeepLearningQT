#pragma once

#include "workflow.h"

#include <checkpoint/checkpoint_catalog.h>
#include <common/model/model_list.h>
#include <inference/inference_service.h>
#include <training/training_service.h>

#include <QString>
#include <QImage>

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
    TaskWorkspaceService(WorkflowGraph& graph, int node_id, const QString& node_directory = {});

    int node_id() const { return node_id_; }
    int source_node_id() const;
    TaskNodeConfig& task();
    const TaskNodeConfig& task() const;
    const std::vector<jocelin::model::ModelEntry>& models() const;
    jocelin::model::ModelEntry* find_model(const QString& id);
    const jocelin::model::ModelEntry* find_model(const QString& id) const;
    jocelin::model::ModelEntry add_model(jocelin::model::ModelEntry model);
    bool remove_model(const QString& id);
    void clear_models();
    const InferenceResultSet* inference_result(const QString& model_id, const QString& weight_id) const;
    void select_inference_result(const QString& model_id, const QString& weight_id);
    void select_inference_model(const QString& model_id);
    std::function<void()> inference_selection_changed;
    const QString& selected_model_id() const { return selected_model_id_; }
    const QString& selected_weight_id() const { return selected_weight_id_; }
    const InferenceResultSet* selected_inference_result() const;
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
    void set_segmentation_mask_for_image(const std::string& image_path,
                                         const QImage& mask);
    void set_segmentation_sample_empty(const std::string& sample_id);
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
        jocelin::config::WeightSource weight_source = {},
        const jocelin::model::ModelEntry* resume_model = nullptr) const;
    jocelin::inference::InferenceRequest build_inference_request(
        const jocelin::config::Device& device,
        const jocelin::model::ModelEntry& model,
        const jocelin::model::ModelWeight& weight,
        const std::string& output_directory = {}) const;

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
    int node_id_ = 0;
    QString selected_model_id_;
    QString selected_weight_id_;
    QString node_directory_;
};

} // namespace studio
