#pragma once

#include "workflow_document.h"
#include "training_run_store.h"

#include <common/model/model_list.h>
#include <inference/inference_service.h>

#include <QString>
#include <QStringList>

#include <cstddef>
#include <utility>
#include <vector>

namespace studio {

struct ProjectMetadata {
    QString project_id;
    QString name;
    QString description;
    QString created_at;
    QString modified_at;
};

struct ProjectWorkflow {
    QString id;
    QString name;
    WorkflowDocument document;
    bool dirty = false;
};

using ProjectModelClass = jocelin::model::ModelClass;
using ProjectModel = jocelin::model::ModelEntry;
using ModelWeightSelection = std::vector<std::pair<QString, QString>>;

// Open-time diagnostics only; ModelList remains owned by its TaskNode.
struct MissingModelAssets {
    QString workflow_id;
    int node_id = 0;
    QString model_id; // Empty when model_list.json itself is missing.
    QStringList weight_ids;
    QStringList paths;
    bool all_weights_missing = false;
};

class ProjectManager {
public:
    ProjectManager();

    void new_project(const QString& name = "Untitled Project");
    std::vector<MissingModelAssets> open_project(const QString& project_file);
    void save_project(const QString& root_directory = {});
    void save_project_as(const QString& name, const QString& parent_directory);

    size_t add_workflow(const QString& name);
    void remove_workflow(size_t index);
    void set_active_index(size_t index);
    void rename_active_workflow(const QString& name);
    void set_project_name(const QString& name);
    void set_project_description(const QString& description);
    void mark_active_dirty();
    QString node_models_directory(int node_id) const;
    void remove_models(int node_id, const QStringList& model_ids = {}, const QString& weight_id = {},
                       const ModelWeightSelection& weights = {});
    // Saves cleared references before removing only these connected Tasks' assets.
    void clear_dataset_dependents(const std::vector<int>& dataset_node_ids);
    QString prepare_inference_directory(int node_id, const QString& model_id, const QString& weight_id) const;
    void discard_inference_directory(int node_id, const QString& model_id, const QString& weight_id, const QString& staging) const;
    void commit_inference_result(int node_id, const QString& model_id, const QString& weight_id,
                                const jocelin::inference::InferenceReport& report, const QString& staging);

    TrainingRunStore& training_runs() { return training_runs_; }
    const ProjectMetadata& metadata() const { return metadata_; }
    const std::vector<ProjectWorkflow>& workflows() const { return workflows_; }
    std::vector<ProjectWorkflow>& workflows() { return workflows_; }
    const ProjectWorkflow& active_workflow() const;
    ProjectWorkflow& active_workflow();
    size_t active_index() const { return active_index_; }
    bool is_dirty() const;
    bool has_storage() const { return !root_directory_.isEmpty(); }
    const QString& root_directory() const { return root_directory_; }
    QString project_file() const;
    size_t external_reference_count() const;

private:
    QString next_workflow_id() const;

    ProjectMetadata metadata_;
    std::vector<ProjectWorkflow> workflows_;
    TrainingRunStore training_runs_;
    QString root_directory_;
    size_t active_index_ = 0;
    bool metadata_dirty_ = false;
};

} // namespace studio
