#pragma once

#include "project_manager.h"
#include "inference_job.h"
#include "training_job.h"

#include <QMainWindow>

#include <vector>

class QAction;
class QCloseEvent;
class QComboBox;
class QDockWidget;
class QGraphicsView;
class QKeyEvent;
class QResizeEvent;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QTabWidget;
class QTabBar;
class QTextEdit;
class QStackedWidget;
class QToolBar;

namespace studio {

class PropertiesEditor;
class ProjectHome;
class NodeWorkspace;
class WorkflowScene;
struct TrainingLaunchRequest;
struct InferenceLaunchRequest;

class MainWindow final : public QMainWindow {
public:
    MainWindow();
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void restoreShellState();
    void normalizeDockState();
    WorkflowGraph& graph();
    std::map<int, QPointF>& positions();
    void bindActiveWorkflow();
    void unbindWorkflowViews();
    void showProjectHome();
    void showWorkflowWorkspace();
    void openNodeWorkspace(int node_id);
    QString connectedDatasetDirectory(int node_id);
    void startTraining(int node_id, const TrainingLaunchRequest& launch);
    void startInference(int node_id, const InferenceLaunchRequest& launch);
    void refreshProjectUi();
    void updateWindowTitle();
    void updateRecentProjects(const QString& project_file = {});
    bool confirmProjectTransition();
    bool saveProject();
    bool saveProjectAs();
    void newProject();
    void newProjectFromTemplate(const QString& task);
    void openProject(const QString& path = {});
    void addWorkflow();
    void removeWorkflow();
    void renameWorkflow();
    void addNode(NodeKind kind);
    void clearWorkflow();
    void loadExampleWorkflow();
    void deleteSelected();
    bool confirmDatasetChange(const std::vector<int>& sources, bool importing_annotations = false);
    bool replaceDataset(int source, DatasetConfig dataset, int annotation_owner, const TaskDatasetBinding* imported);
    void showProperties();
    void refreshValidation(bool report_to_log = false);
    void markWorkflowChanged();
    void setEditingEnabled(bool enabled);
    void setRuntimeStatus(const QString& text, const char* state);
    void runWorkflow();

    ProjectManager project_;
    TrainingJob training_job_;
    InferenceJob inference_job_;
    QStackedWidget* workspace_stack_ = nullptr;
    ProjectHome* project_home_ = nullptr;
    QWidget* workflow_workspace_ = nullptr;
    NodeWorkspace* node_workspace_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    WorkflowScene* scene_ = nullptr;
    QGraphicsView* view_ = nullptr;
    QDockWidget* project_dock_ = nullptr;
    QDockWidget* library_dock_ = nullptr;
    QDockWidget* property_dock_ = nullptr;
    QDockWidget* output_dock_ = nullptr;
    PropertiesEditor* property_editor_ = nullptr;
    QLabel* project_root_ = nullptr;
    QLabel* workflow_context_title_ = nullptr;
    QLabel* workflow_context_hint_ = nullptr;
    QTabBar* workflow_tabs_ = nullptr;
    QWidget* canvas_empty_state_ = nullptr;
    QLabel* overview_nodes_ = nullptr;
    QLabel* overview_connections_ = nullptr;
    QLabel* overview_parameters_ = nullptr;
    QLabel* overview_status_ = nullptr;
    QLabel* status_context_ = nullptr;
    QLabel* status_saved_ = nullptr;
    QLabel* status_runtime_ = nullptr;
    QLineEdit* project_name_ = nullptr;
    QLineEdit* project_description_ = nullptr;
    QMenu* recent_menu_ = nullptr;
    QTabWidget* output_tabs_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QTextEdit* result_ = nullptr;
    QAction* run_action_ = nullptr;
    QAction* save_project_action_ = nullptr;
    QAction* save_project_as_action_ = nullptr;
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
    std::vector<QAction*> editing_actions_;
    std::vector<ValidationIssue> issues_;
};

} // namespace studio
