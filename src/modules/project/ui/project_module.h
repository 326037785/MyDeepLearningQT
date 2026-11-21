#pragma once
#include "modules/project/application/project_manager.h"
#include <QObject>
#include <QCoreApplication>
#include <functional>
class QAction;
class QDockWidget;
class QLabel;
class QLineEdit;
class QMenu;
class QToolBar;
namespace studio {
class MainWindow;
class ProjectHome;
class ProjectModule final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(studio::ProjectModule)
public:
    ProjectModule(ProjectManager& project, MainWindow& shell);
    void mount(QMenu& menu, QToolBar& home_bar, QToolBar& workflow_bar);
    void refresh();
    void updateRecentProjects(const QString& project_file = {});
    void set_editing_enabled(bool enabled);
    void show_home();
    bool can_close();
    QDockWidget* dock() const { return dock_; }
    std::function<bool()> confirm_annotation_transition;
    std::function<void()> before_switch;
    std::function<void()> after_switch;
    std::function<void()> show_workflow;
    std::function<void()> changed;
    std::function<void(const QString&)> log;
    std::function<bool()> busy;
private:
    void create_panel();
    void refreshProjectUi();
    WorkflowGraph& graph();
    std::map<int, QPointF>& positions();
    bool confirmProjectTransition();
    bool saveProject();
    bool saveProjectAs();
    void newProject();
    void newProjectFromTemplate(const QString& task);
    void openProject(const QString& path = {});
    void close_project();
    void delete_project(const QString& path);
    ProjectManager& project_;
    MainWindow& shell_;
    ProjectHome* home_;
    QDockWidget* dock_;
    QLineEdit* project_name_ = nullptr;
    QLineEdit* project_description_ = nullptr;
    QLabel* project_root_ = nullptr;
    QMenu* recent_menu_ = nullptr;
    QAction* save_ = nullptr;
    QAction* save_as_ = nullptr;
    std::vector<QAction*> actions_;
};
} // namespace studio
