#pragma once

#include "workflow_document.h"

#include <common/model/model_list.h>

#include <QString>

#include <cstddef>
#include <vector>

namespace studio {

struct ProjectMetadata {
    QString project_id;
    QString name;
    QString description;
    QString created_at;
    QString modified_at;
    QString root_directory;
    QString derived_from_project_id;
    QString derived_from_project_path;
};

struct ProjectWorkflow {
    QString id;
    QString name;
    WorkflowDocument document;
    bool dirty = false;
};

using ProjectModelClass = jocelin::model::ModelClass;
using ProjectModel = jocelin::model::ModelEntry;

class ProjectManager {
public:
    ProjectManager();

    void new_project(const QString& name = "Untitled Project");
    void open_project(const QString& project_file);
    void save_project(const QString& root_directory = {});
    void save_project_as(const QString& name, const QString& parent_directory);

    size_t add_workflow(const QString& name);
    void remove_workflow(size_t index);
    void set_active_index(size_t index);
    void rename_active_workflow(const QString& name);
    void set_project_name(const QString& name);
    void set_project_description(const QString& description);
    void mark_active_dirty();
    void mark_models_dirty();
    ProjectModel add_model(ProjectModel model);
    bool remove_model(const QString& id);
    void clear_models();
    int allocate_training_run_number();

    const ProjectMetadata& metadata() const { return metadata_; }
    const std::vector<ProjectWorkflow>& workflows() const { return workflows_; }
    std::vector<ProjectWorkflow>& workflows() { return workflows_; }
    const ProjectWorkflow& active_workflow() const;
    ProjectWorkflow& active_workflow();
    size_t active_index() const { return active_index_; }
    const std::vector<ProjectModel>& models() const { return model_list_.entries(); }
    const ProjectModel* find_model(const QString& id) const;
    ProjectModel* find_model(const QString& id);
    bool is_dirty() const;
    bool has_storage() const { return !metadata_.root_directory.isEmpty(); }
    QString project_file() const;
    size_t external_reference_count() const;

private:
    QString next_workflow_id() const;

    ProjectMetadata metadata_;
    std::vector<ProjectWorkflow> workflows_;
    jocelin::model::ModelList model_list_;
    int next_training_run_number_ = 1;
    size_t active_index_ = 0;
    bool metadata_dirty_ = false;
};

} // namespace studio
