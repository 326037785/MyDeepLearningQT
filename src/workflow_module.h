#pragma once
#include "project_manager.h"
#include <QObject>
#include <QCoreApplication>
#include <functional>
class QAction; class QDockWidget; class QGraphicsView; class QLabel; class QMenu;
class QResizeEvent; class QTabBar; class QToolBar;
namespace studio {
class ActivityLog; class MainWindow; class NodeWorkspace; class WorkflowScene; class PropertiesEditor;
class WorkflowModule final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(studio::WorkflowModule)
public:
    WorkflowModule(ProjectManager& project, MainWindow& shell, ActivityLog& log);
    ~WorkflowModule() override;
    void mount(QToolBar& toolbar, QMenu& edit_menu, QMenu& view_menu);
    void bindActiveWorkflow();
    void unbindWorkflowViews();
    void showWorkflowWorkspace();
    void openNodeWorkspace(int node_id);
    void refreshProjectUi();
    void refreshValidation(bool report_to_log = false);
    void setEditingEnabled(bool enabled);
    void adapt_to_resize(QResizeEvent* event);
    NodeWorkspace& node_workspace() const { return *node_workspace_; }
    QDockWidget* library_dock() const { return library_dock_; }
    QDockWidget* property_dock() const { return property_dock_; }
    std::function<void()> project_changed;
    std::function<void(int)> request_training;
    std::function<bool()> busy;
private:
    void build_views();
    void build_workspace();
    void build_library();
    void build_properties();
    WorkflowGraph& graph();
    std::map<int, QPointF>& positions();
    QString connectedDatasetDirectory(int node_id);
    bool before_nodes_removed(const std::vector<int>& nodes);
    void nodes_removed();
    void fit();
    void addWorkflow();
    bool activateWorkflow(int index);
    void closeWorkflow(int index);
    void renameWorkflow(int index);
    void addNode(NodeKind kind, const QString& task_module = {});
    void clearWorkflow();
    void loadExampleWorkflow();
    void deleteSelected();
    bool confirmDatasetChange(const std::vector<int>& sources, bool importing_annotations = false);
    bool replaceDataset(int source, DatasetConfig dataset, int owner, const TaskDatasetBinding* imported);
    void showProperties();
    void markWorkflowChanged();
    void runWorkflow();
    ProjectManager& project_;
    MainWindow& shell_;
    ActivityLog& log_;
    WorkflowScene* scene_ = nullptr;
    QGraphicsView* view_ = nullptr;
    QWidget* workflow_workspace_ = nullptr;
    NodeWorkspace* node_workspace_ = nullptr;
    QDockWidget* library_dock_ = nullptr;
    QDockWidget* property_dock_ = nullptr;
    PropertiesEditor* property_editor_ = nullptr;
    QTabBar* workflow_tabs_ = nullptr;
    QAction* run_action_ = nullptr;
    QAction* delete_action_ = nullptr;
    std::vector<QAction*> editing_actions_;
    std::vector<ValidationIssue> issues_;
};
} // namespace studio
