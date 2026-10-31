#pragma once

#include "project_manager.h"
#include "task_workspace_service.h"

#include <QStringList>
#include <QWidget>

#include <runtime/device_service.h>

#include <functional>

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QSplitter;
class QStackedWidget;
class QDoubleSpinBox;
class QTableWidget;
class QToolButton;

namespace studio {

class ClassificationCanvas;
class ImageResultCanvas;
class LossChart;
class ProjectModelBrowser;
class SampleBrowser;

struct TrainingLaunchRequest {
    QString mode = "new";
    QString model_id;
};

struct InferenceLaunchRequest {
    QString model_id;
    QString weight_id;
    jocelin::config::Device device;
};

class DatasetSelectionView final : public QWidget {
public:
    explicit DatasetSelectionView(QWidget* parent = nullptr);
    void set_service(TaskWorkspaceService* service);
    void refresh();
    std::function<void()> changed;
    std::function<void(int node_id)> open_dataset_requested;
    std::function<bool(int, DatasetConfig, int, const TaskDatasetBinding*)> dataset_replace_requested;

private:
    void apply_filter();
    TaskWorkspaceService* service_ = nullptr;
    QLineEdit* search_ = nullptr;
    QComboBox* filter_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* sample_value_ = nullptr;
    QLabel* image_value_ = nullptr;
    QLabel* selected_value_ = nullptr;
    QLabel* quality_value_ = nullptr;
    QLabel* issue_banner_ = nullptr;
    bool rebuilding_ = false;
};

class DatasetSplitView final : public QWidget {
public:
    explicit DatasetSplitView(QWidget* parent = nullptr);
    void set_service(TaskWorkspaceService* service);
    void refresh();
    void apply_random_split();
    std::function<void()> changed;
    std::function<void(const QString&)> status;

private:
    void refresh_stats();
    TaskWorkspaceService* service_ = nullptr;
    SampleBrowser* thumbnails_ = nullptr;
    QTableWidget* table_ = nullptr;
    QSpinBox* train_percent_ = nullptr;
    QSpinBox* validation_percent_ = nullptr;
    QSpinBox* test_percent_ = nullptr;
    QSpinBox* seed_ = nullptr;
    QLabel* train_value_ = nullptr;
    QLabel* validation_value_ = nullptr;
    QLabel* test_value_ = nullptr;
    QLabel* unassigned_value_ = nullptr;
    QLabel* warning_ = nullptr;
    bool rebuilding_ = false;
};

class TrainingWorkspace final : public QWidget {
public:
    explicit TrainingWorkspace(QWidget* parent = nullptr);
    void set_service(TaskWorkspaceService* service);
    void set_models(const std::vector<ProjectModel>& models);
    void refresh();
    void request_start();
    void set_busy(bool busy);
    void begin();
    void update_progress(const jocelin::train::TrainingProgress& progress);
    void finish(const jocelin::training::TrainingResult& result, const QString& error);
    std::function<void(const TrainingLaunchRequest&)> start_requested;
    std::function<void()> stop_requested;
    std::function<void()> pause_requested;
    std::function<void(const QString&)> run_details_requested;
    std::function<void()> import_model_requested;
    std::function<void(const QString&)> export_model_requested;
    std::function<void(const QStringList&)> delete_models_requested;
    std::function<void(const QString&, const QString&)> delete_model_weight_requested;
    std::function<void()> clear_models_requested;
    std::function<void()> changed;

private:
    void commit_model_spec();
    void populate_model_options();
    void populate_models();
    const ProjectModel* controlled_model() const;
    QString training_source_mode() const;
    void update_model_compatibility();
    void update_advanced_presentation();
    void update_training_content_height();
    TaskWorkspaceService* service_ = nullptr;
    QComboBox* model_index_ = nullptr;
    QComboBox* model_scale_ = nullptr;
    QSpinBox* image_width_ = nullptr;
    QSpinBox* image_height_ = nullptr;
    QSpinBox* batch_size_ = nullptr;
    QSpinBox* epochs_ = nullptr;
    QDoubleSpinBox* learning_rate_ = nullptr;
    QCheckBox* use_gpu_ = nullptr;
    QLineEdit* output_directory_ = nullptr;
    QSpinBox* workers_ = nullptr;
    QDoubleSpinBox* weight_decay_ = nullptr;
    QComboBox* optimizer_ = nullptr;
    QComboBox* scheduler_ = nullptr;
    QPushButton* augmentation_button_ = nullptr;
    QPushButton* advanced_toggle_ = nullptr;
    QFrame* advanced_panel_ = nullptr;
    QScrollArea* workspace_scroll_ = nullptr;
    QSplitter* workspace_splitter_ = nullptr;
    QWidget* settings_panel_ = nullptr;
    QFrame* models_panel_ = nullptr;
    ProjectModelBrowser* model_browser_ = nullptr;
    QLabel* source_status_ = nullptr;
    QPushButton* start_ = nullptr;
    QPushButton* stop_ = nullptr;
    QPushButton* pause_ = nullptr;
    QPushButton* resume_ = nullptr;
    QLabel* state_ = nullptr;
    QLabel* epoch_ = nullptr;
    QLabel* loss_ = nullptr;
    QLabel* metric_ = nullptr;
    QProgressBar* progress_ = nullptr;
    LossChart* chart_ = nullptr;
    QPushButton* run_details_ = nullptr;
    std::vector<ProjectModel> models_;
    QString model_compatibility_severity_;
    bool rebuilding_ = false;
};

class InferenceWorkspace final : public QWidget {
public:
    explicit InferenceWorkspace(QWidget* parent = nullptr);
    void set_service(TaskWorkspaceService* service);
    void set_models(const std::vector<ProjectModel>& models);
    void synchronize_result_selection();
    void refresh();
    void request_start();
    void set_busy(bool busy);
    void begin();
    void update_progress(size_t done, size_t total, const QString& image);
    void finish(const QString& error);
    std::function<void(const InferenceLaunchRequest&)> start_requested;
    std::function<void()> import_model_requested;
    std::function<void(const QString&)> export_model_requested;
    std::function<void(const QStringList&)> delete_models_requested;
    std::function<void(const QString&, const QString&)> delete_model_weight_requested;
    std::function<void()> clear_models_requested;
    std::function<void()> changed;

private:
    void commit_selection_scope();
    void commit_result_selection();
    void refresh_canvas();
    void refresh_device_types();
    void refresh_devices();
    void refresh_weight_types();
    void refresh_weight_branches();
    void commit_device();
    const jocelin::runtime::ComputeDevice* selected_device() const;
    const jocelin::model::ModelWeight* selected_weight() const;
    void update_command_presentation();
    TaskWorkspaceService* service_ = nullptr;
    std::vector<ProjectModel> models_;
    ProjectModelBrowser* model_browser_ = nullptr;
    QListWidget* images_ = nullptr;
    QStackedWidget* canvas_stack_ = nullptr;
    ImageResultCanvas* segmentation_canvas_ = nullptr;
    ClassificationCanvas* classification_canvas_ = nullptr;
    QCheckBox* overlay_layer_ = nullptr;
    QComboBox* device_type_ = nullptr;
    QComboBox* device_ = nullptr;
    QComboBox* weight_type_ = nullptr;
    QComboBox* weight_branch_ = nullptr;
    QSpinBox* batch_size_ = nullptr;
    QPushButton* start_ = nullptr;
    QLabel* state_ = nullptr;
    QLabel* current_image_ = nullptr;
    QLabel* scope_summary_ = nullptr;
    QProgressBar* progress_ = nullptr;
    bool rebuilding_ = false;
    bool busy_ = false;
    std::vector<jocelin::runtime::ComputeDevice> available_devices_;
};

class ResultsWorkspace final : public QWidget {
public:
    explicit ResultsWorkspace(QWidget* parent = nullptr);
    void set_service(TaskWorkspaceService* service);
    void set_models(const std::vector<ProjectModel>& models);
    void refresh();
    void export_results();

private:
    const InferenceResultSet* selected_result_set() const;
    void refresh_selected_result();
    void refresh_sample_detail();

    TaskWorkspaceService* service_ = nullptr;
    std::vector<ProjectModel> models_;
    ProjectModelBrowser* model_browser_ = nullptr;
    QWidget* summary_ = nullptr;
    QLabel* context_ = nullptr;
    QLabel* precision_ = nullptr;
    QLabel* recall_ = nullptr;
    QLabel* f1_ = nullptr;
    QLabel* benchmark_label_ = nullptr;
    QLabel* benchmark_ = nullptr;
    QLabel* state_ = nullptr;
    QLabel* sample_detail_ = nullptr;
    QStackedWidget* canvas_stack_ = nullptr;
    ImageResultCanvas* segmentation_canvas_ = nullptr;
    ClassificationCanvas* classification_canvas_ = nullptr;
    QCheckBox* overlay_layer_ = nullptr;
    QWidget* empty_state_ = nullptr;
    QWidget* empty_filler_ = nullptr;
    QWidget* content_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPushButton* open_folder_ = nullptr;
    QPushButton* export_button_ = nullptr;
    QString selected_model_id_;
    QComboBox* weight_branch_ = nullptr;
};

} // namespace studio
