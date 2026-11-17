#pragma once

#include <common/model/model_list.h>
#include <QWidget>

#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace studio {

class ClassificationCanvas;
class ImageResultCanvas;
class ProjectModelBrowser;
class TaskWorkspaceService;

struct InferenceResultSet;

class ResultsWorkspace final : public QWidget {
public:
    explicit ResultsWorkspace(QWidget* parent = nullptr);
    void set_service(TaskWorkspaceService* service);
    void set_models(const std::vector<jocelin::model::ModelEntry>& models);
    void refresh();
    void export_results();

private:
    const InferenceResultSet* selected_result_set() const;
    void refresh_selected_result();
    void refresh_sample_detail();

    TaskWorkspaceService* service_ = nullptr;
    std::vector<jocelin::model::ModelEntry> models_;
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
