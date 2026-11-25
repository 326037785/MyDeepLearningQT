#pragma once

#include "modules/task/application/model_weight_selection.h"
#include "modules/task/application/task_launch_requests.h"

#include <common/model/model_list.h>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QSplitter;

namespace jocelin::train {
struct TrainingProgress;
}

namespace jocelin::training {
struct TrainingResult;
}

namespace studio {

class LossChart;
class ProjectModelBrowser;
class TaskWorkspaceService;

class TrainingWorkspace final : public QWidget {
public:
    explicit TrainingWorkspace(QWidget* parent = nullptr);
    void set_service(TaskWorkspaceService* service);
    void set_models(const std::vector<jocelin::model::ModelEntry>& models);
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
    std::function<void(const ModelWeightSelection&)> delete_model_weights_requested;
    std::function<void(const QString&, const QString&)> delete_model_weight_requested;
    std::function<void()> clear_models_requested;
    std::function<void()> changed;

private:
    void commit_model_spec();
    void populate_model_options();
    void populate_models();
    const jocelin::model::ModelEntry* controlled_model() const;
    QString training_source_mode() const;
    void update_model_compatibility();
    void update_advanced_presentation();
    void update_training_content_height();
    TaskWorkspaceService* service_ = nullptr;
    bool training_busy_ = false;
    QComboBox* model_index_ = nullptr;
    QComboBox* model_scale_ = nullptr;
    QSpinBox* image_width_ = nullptr;
    QSpinBox* image_height_ = nullptr;
    QLabel* image_size_separator_ = nullptr;
    QComboBox* image_size_preset_ = nullptr;
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
    std::vector<jocelin::model::ModelEntry> models_;
    QString model_compatibility_severity_;
    bool rebuilding_ = false;
};

} // namespace studio
