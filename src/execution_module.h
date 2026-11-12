#pragma once
#include "project_manager.h"
#include "training_job.h"
#include "inference_job.h"
#include "node_workspace.h"
#include <QCoreApplication>
#include <QFutureWatcher>
#include <functional>
#include <utility>
namespace studio {
class ExecutionModule final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(studio::ExecutionModule)
public:
    ExecutionModule(ProjectManager& project, NodeWorkspace& workspace, QWidget* parent);
    bool is_running() const;
    void start_training(int node_id);
    void start_training(int node_id, const TrainingLaunchRequest& launch);
    void start_inference(int node_id, const InferenceLaunchRequest& launch);
    void start_weight_conversion(int node_id, const QString& model_id, const QString& weight_id,
                                 jocelin::config::WeightType target = jocelin::config::WeightType::Onnx);
    void stop();
    void pause();
    std::function<void(const QString&)> log;
    std::function<void()> show_log;
    std::function<void(const QString&, int)> status;
    std::function<void(bool)> editing_changed;
    std::function<void(const QString&, const char*)> runtime_status;
    std::function<void()> changed;
private:
    WorkflowGraph& graph();
    void message(const QString& text, int timeout = 0);
    void training_progress(const jocelin::train::TrainingProgress& progress);
    void training_log(const QString& message);
    void training_warning(const QString& message);
    void training_completed(const jocelin::training::TrainingResult& result, const QString& error);
    void inference_progress(size_t done, size_t total, const QString& image);
    void inference_completed(const InferenceOutcome& outcome);
    void weight_conversion_completed();
    ProjectManager& project_;
    NodeWorkspace& workspace_;
    QWidget* dialog_parent_;
    TrainingJob training_job_;
    InferenceJob inference_job_;
    QFutureWatcher<std::pair<jocelin::model::ModelWeight, QString>> conversion_job_;
    int active_conversion_node_id_ = 0;
    QString active_conversion_model_id_;
    int active_training_node_id_ = 0;
    QString active_training_model_id_;
    QString active_training_source_mode_;
    QString active_training_parent_model_id_;
    QString active_training_source_weight_id_;
    QString active_training_run_id_;
    int active_training_run_number_ = 0;
    QString active_training_run_directory_;
    QString active_training_started_at_;
    QString active_training_log_path_;
    int active_inference_node_id_ = 0;
    QString active_inference_model_id_;
    QString active_inference_weight_id_;
    QString active_inference_staging_;
    std::string active_inference_previous_status_;
};
} // namespace studio
