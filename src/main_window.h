#pragma once
#include <QMainWindow>
#include <memory>

class QCloseEvent; class QDockWidget; class QLabel; class QResizeEvent;
class QStackedWidget; class QToolBar;
namespace studio {
class ActivityLog; class ExecutionModule; class ModelActions;
class ProjectManager; class ProjectModule; class WorkflowModule;

// Window layout and feature composition. Business state belongs to the domain objects.
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;
    void mount_page(QWidget* page);
    QDockWidget* mount_dock(const QString& title, const char* id, QWidget* content, Qt::DockWidgetArea area);
    void mount_dock(Qt::DockWidgetArea area, QDockWidget* dock);
    void show_home(QWidget* page);
    void show_workflow(QWidget* page);
    void show_task(QWidget* page);
    bool is_current_page(QWidget* page) const;
    void show_message(const QString& text, int timeout = 0);
    void clear_message();
    void set_context_status(const QString& text);
    void set_saved_status(const QString& text);
    void set_runtime_status(const QString& text, const char* state);
protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    void buildUi();
    void restoreShellState();
    void normalizeDockState();
    void hide_docks();
    std::unique_ptr<ProjectManager> project_;
    std::unique_ptr<ProjectModule> project_module_;
    std::unique_ptr<WorkflowModule> workflow_;
    std::unique_ptr<ExecutionModule> execution_;
    std::unique_ptr<ModelActions> models_;
    ActivityLog* log_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QToolBar* workflow_toolbar_ = nullptr;
    QToolBar* home_toolbar_ = nullptr;
    QDockWidget* project_dock_ = nullptr;
    QDockWidget* library_dock_ = nullptr;
    QDockWidget* property_dock_ = nullptr;
    QList<QDockWidget*> docks_;
    QLabel* context_ = nullptr;
    QLabel* saved_ = nullptr;
    QLabel* runtime_ = nullptr;
};
} // namespace studio
