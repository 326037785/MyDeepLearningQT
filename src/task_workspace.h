#pragma once

#include "project_manager.h"
#include "task_workspace_service.h"

#include <QStringList>
#include <QWidget>

#include <array>
#include <functional>
#include <memory>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTabWidget;

namespace studio {

class ClassificationAnnotationWidget;
class DatasetSelectionView;
class DatasetSplitView;
class InferenceWorkspace;
class ResultsWorkspace;
class SegmentationAnnotationWidget;
class TrainingWorkspace;
class ProjectModelBrowser;
struct TrainingLaunchRequest;
struct InferenceLaunchRequest;

class TaskWorkspace final : public QWidget {
public:
    enum class Step { Data = 0, Annotation, Split, Training, Inference, Results };

    explicit TaskWorkspace(QWidget* parent = nullptr);
    void bind(WorkflowGraph* graph, int node_id);
    void refresh_models();
    void clear();
    void refresh();
    void activate_step(Step step);
    Step active_step() const { return active_step_; }
    void set_runtime_busy(bool busy);

    void training_started();
    void update_training_progress(const jocelin::train::TrainingProgress& progress);
    void finish_training(const jocelin::training::TrainingResult& result,
                         const QString& error);
    void inference_started();
    void update_inference_progress(size_t done, size_t total, const QString& image);
    void finish_inference(const QString& error);

    std::function<void()> back_requested;
    std::function<void()> changed;
    std::function<bool(int, DatasetConfig, int, const TaskDatasetBinding*)> dataset_replace_requested;
    std::function<void(int node_id)> dataset_requested;
    std::function<void(int node_id, const TrainingLaunchRequest&)> train_requested;
    std::function<void()> stop_training_requested;
    std::function<void()> pause_training_requested;
    std::function<void(const QString&)> run_details_requested;
    std::function<void(int node_id)> import_model_requested;
    std::function<void(int node_id, const QString&)> export_model_requested;
    std::function<void(int node_id, const QStringList&)> delete_models_requested;
    std::function<void(int node_id, const ModelWeightSelection&)> delete_model_weights_requested;
    std::function<void(int node_id, const QString&, const QString&)> delete_model_weight_requested;
    std::function<void(int node_id)> clear_models_requested;
    std::function<void(int node_id, const InferenceLaunchRequest&)> infer_requested;
    std::function<void(int node_id, const QString&, const QString&, jocelin::config::WeightType)> convert_weight_requested;
    std::function<void(const QString&)> status;

private:
    QWidget* build_annotation_page();
    void refresh_annotation();
    void refresh_inference_result();
    void refresh_step_availability();
    void domain_changed();

    WorkflowGraph* graph_ = nullptr;
    int node_id_ = 0;
    std::unique_ptr<TaskWorkspaceService> service_;
    QLabel* identity_ = nullptr;
    QLabel* context_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    DatasetSelectionView* data_ = nullptr;
    SegmentationAnnotationWidget* segmentation_ = nullptr;
    ClassificationAnnotationWidget* classification_ = nullptr;
    DatasetSplitView* split_ = nullptr;
    TrainingWorkspace* training_ = nullptr;
    InferenceWorkspace* inference_ = nullptr;
    ResultsWorkspace* results_ = nullptr;
    ProjectModelBrowser* annotation_models_ = nullptr;
    QWidget* annotation_models_tab_ = nullptr;
    QLabel* annotation_selection_ = nullptr;
    QTabWidget* segmentation_inspector_ = nullptr;
    QTabWidget* classification_inspector_ = nullptr;
    std::array<QPushButton*, 6> step_buttons_{};
    Step active_step_ = Step::Data;
    bool runtime_busy_ = false;
    bool annotation_binding_dirty_ = true;
};

} // namespace studio
