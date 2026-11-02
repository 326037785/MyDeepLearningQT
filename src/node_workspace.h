#pragma once

#include "project_manager.h"
#include "workflow.h"

#include <inference/inference_service.h>
#include <training/training_service.h>

#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QVBoxLayout;

namespace studio {

class TaskWorkspace;
struct TrainingLaunchRequest;
struct InferenceLaunchRequest;

// Hosts either the raw Dataset workspace or the complete six-step Task
// workspace. Task-specific state and commands remain inside TaskWorkspace.
class NodeWorkspace final : public QWidget {
public:
    explicit NodeWorkspace(QWidget* parent = nullptr);

    void edit_node(
        WorkflowGraph* graph,
        int node_id,
        const QString& dataset_directory);
    void refresh_models();
    void clear_node();
    void show_inference();
    void set_runtime_busy(bool busy);
    void training_started(int node_id);
    void update_training_progress(
        int node_id,
        const jocelin::train::TrainingProgress& progress);
    void finish_training(
        int node_id,
        const jocelin::training::TrainingResult& result,
        const QString& error);
    void inference_started(int node_id);
    void update_inference_progress(
        int node_id,
        size_t done,
        size_t total,
        const QString& image);
    void finish_inference(int node_id, const QString& error);

    std::function<void()> back_requested;
    std::function<void()> changed;
    std::function<bool(int, DatasetConfig, int, const TaskDatasetBinding*)> dataset_replace_requested;
    std::function<void(int node_id, const TrainingLaunchRequest&)> training_launch_requested;
    std::function<void(int node_id, const InferenceLaunchRequest&)> infer_requested;
    std::function<void()> stop_requested;
    std::function<void()> pause_requested;
    std::function<void(const QString&)> run_details_requested;
    std::function<void(int node_id)> import_model_requested;
    std::function<void(int node_id, const QString&)> export_model_requested;
    std::function<void(int node_id, const QStringList&)> delete_models_requested;
    std::function<void(int node_id, const ModelWeightSelection&)> delete_model_weights_requested;
    std::function<void(int node_id, const QString&, const QString&)> delete_model_weight_requested;
    std::function<void(int node_id)> clear_models_requested;
    std::function<void(int node_id)> node_requested;
    std::function<void(const QString&)> status;

private:
    void rebuild();
    QWidget* dataset_page();
    QWidget* task_page();
    bool has_current_node(NodeKind kind) const;

    WorkflowGraph* graph_ = nullptr;
    int node_id_ = 0;
    QString dataset_directory_;
    QLabel* title_ = nullptr;
    QWidget* header_ = nullptr;
    QVBoxLayout* content_ = nullptr;
    QWidget* page_ = nullptr;
    TaskWorkspace* task_workspace_ = nullptr;
    bool runtime_busy_ = false;
};

} // namespace studio
