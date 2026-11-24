#pragma once

#include "modules/task/application/model_weight_selection.h"
#include "modules/task/application/task_launch_requests.h"

#include <common/model/model_list.h>
#include <runtime/device_service.h>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace studio {

class ClassificationCanvas;
class ImageResultCanvas;
class InferenceFilterPanel;
class OdInferenceFilterContent;
class ProjectModelBrowser;
class TaskWorkspaceService;

class InferenceWorkspace final : public QWidget {
public:
    explicit InferenceWorkspace(QWidget* parent = nullptr);
    void set_service(TaskWorkspaceService* service);
    void set_models(const std::vector<jocelin::model::ModelEntry>& models);
    void synchronize_result_selection();
    void refresh();
    void request_start();
    void set_busy(bool busy);
    void begin();
    void update_progress(size_t done, size_t total, const QString& image);
    void finish(const QString& error);
    std::function<void(const InferenceLaunchRequest&)> start_requested;
    std::function<void(const QString&, const QString&, jocelin::config::WeightType)> convert_weight_requested;
    std::function<void()> import_model_requested;
    std::function<void(const QString&)> export_model_requested;
    std::function<void(const QStringList&)> delete_models_requested;
    std::function<void(const ModelWeightSelection&)> delete_model_weights_requested;
    std::function<void(const QString&, const QString&)> delete_model_weight_requested;
    std::function<void()> clear_models_requested;
    std::function<void()> changed;

private:
    void commit_selection_scope();
    void commit_result_selection();
    void refresh_canvas();
    void refresh_device_types();
    void refresh_devices();
    void commit_device();
    const jocelin::runtime::ComputeDevice* selected_device() const;
    const jocelin::model::ModelWeight* selected_weight(const ModelWeightSelection& checked) const;
    void update_command_presentation();
    TaskWorkspaceService* service_ = nullptr;
    std::vector<jocelin::model::ModelEntry> models_;
    ProjectModelBrowser* model_browser_ = nullptr;
    InferenceFilterPanel* filter_panel_ = nullptr;
    OdInferenceFilterContent* od_filter_ = nullptr; // Owned by the filter panel.
    QListWidget* images_ = nullptr;
    QComboBox* sample_split_filter_ = nullptr;
    QLineEdit* sample_search_ = nullptr;
    QStackedWidget* canvas_stack_ = nullptr;
    ImageResultCanvas* segmentation_canvas_ = nullptr;
    ClassificationCanvas* classification_canvas_ = nullptr;
    QCheckBox* overlay_layer_ = nullptr;
    QComboBox* device_type_ = nullptr;
    QComboBox* device_ = nullptr;
    QLabel* checked_target_ = nullptr;
    QSpinBox* batch_size_ = nullptr;
    QLabel* batch_label_ = nullptr;
    QPushButton* start_ = nullptr;
    QLabel* state_ = nullptr;
    QLabel* current_image_ = nullptr;
    QLabel* scope_summary_ = nullptr;
    QProgressBar* progress_ = nullptr;
    bool rebuilding_ = false;
    bool busy_ = false;
    bool cpu_only_weight_ = false;
    std::vector<jocelin::runtime::ComputeDevice> available_devices_;
};

} // namespace studio
