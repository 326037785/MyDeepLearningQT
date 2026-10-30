#include "main_window.h"
#include "project_manager.h"
#include "project_module.h"
#include "new_project_dialog.h"
#include "project_home.h"
#include "project_path_validator.h"
#include "recent_project_store.h"
#include "save_project_as_dialog.h"
#include "ui_language.h"
#include "workflow_module.h"
#include "workflow_scene.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QtTest/QTest>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void capture(QWidget& window, const char* variable) {
    const QString path = qEnvironmentVariable(variable);
    if (path.isEmpty()) return;
    window.show();
    QApplication::processEvents();
    require(window.grab().save(path), "could not capture home screen");
}

void change_language(studio::MainWindow& window, const char* language, bool accept) {
    auto* action = window.findChild<QAction*>("preferencesAction");
    require(action, "preferences action is missing");
    bool visited = false;
    bool notice = false;
    QTimer driver;
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        if (dialog->objectName() == "preferencesDialog") {
            auto* selector = dialog->findChild<QComboBox*>("languageSelector");
            if (!selector) { dialog->reject(); return; }
            const int index = selector->findData(language);
            if (index < 0) { dialog->reject(); return; }
            visited = true;
            selector->setCurrentIndex(index);
            if (accept) dialog->accept();
            else dialog->reject();
        } else if (auto* message = qobject_cast<QMessageBox*>(dialog)) {
            notice = true;
            message->accept();
        }
    });
    driver.start(5);
    action->trigger();
    driver.stop();
    require(visited, "language selector was unavailable");
    require(!accept || notice, "language restart notice was not shown");
}

void module_command_boundaries(studio::MainWindow& window, const QString& project_file) {
    studio::ProjectModule* project = nullptr;
    studio::WorkflowModule* workflow = nullptr;
    for (auto* child : window.children()) {
        if (auto* module = dynamic_cast<studio::ProjectModule*>(child)) project = module;
        if (auto* module = dynamic_cast<studio::WorkflowModule*>(child)) workflow = module;
    }
    require(project && workflow, "built-in modules are missing");
    auto* home = window.findChild<studio::ProjectHome*>("projectHome");
    auto* tabs = window.findChild<QTabBar*>("workflowTabs");
    auto* add = window.findChild<QPushButton*>("addWorkflowTabButton");
    auto* view = window.findChild<QGraphicsView*>("workflowView");
    auto* scene = view ? dynamic_cast<studio::WorkflowScene*>(view->scene()) : nullptr;
    auto* run = window.findChild<QAction*>("openTaskAction");
    auto* remove = window.findChild<QAction*>("deleteAction");
    auto* edit = window.findChild<QMenu*>("editMenu");
    auto* save = window.findChild<QAction*>("saveProjectAction");
    auto* recent = window.findChild<QMenu*>("recentProjectsMenu");
    require(home && tabs && add && scene && run && remove && edit && save && recent, "module contributions are missing");
    auto* grid = window.findChild<QToolButton*>("workflowGridButton");
    auto* fit = window.findChild<QToolButton*>("workflowFitButton");
    auto* status = window.findChild<QLabel*>("workflowStatusContext");
    require(grid && fit && status && !grid->icon().isNull() && !fit->icon().isNull() &&
            !grid->accessibleName().isEmpty() && !fit->accessibleName().isEmpty(),
            "canvas controls are missing accessible icons");
    grid->setChecked(!grid->isChecked());
    require(scene->grid_visible() == grid->isChecked(), "grid control did not update the canvas");
    require(!window.findChild<QWidget*>("workflowOverview") &&
            !window.findChild<QWidget*>("nodeLibrarySearch"), "redundant workflow controls remain");

    QTimer::singleShot(0, [] {
        if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
            dialog->setTextValue("Second workflow");
            dialog->accept();
        }
    });
    add->click();
    require(tabs->count() == 2 && tabs->currentIndex() == 1, "adding a workflow did not bind its tab");
    const QString validation_message = status->text();
    workflow->refreshProjectUi();
    require(!validation_message.isEmpty() && status->text() == validation_message && !run->isEnabled(),
            "workflow refresh hid the validation problem");
    auto* graph = scene->model();
    const int source = graph->add_node(studio::NodeKind::DatasetInput);
    const QString dataset_directory = QDir(QFileInfo(project_file).absolutePath()).filePath("dataset");
    require(QDir().mkpath(dataset_directory), "cannot create workflow dataset fixture");
    std::get<studio::DatasetConfig>(graph->node(source).config).directory =
        dataset_directory.toStdString();
    const int task = graph->add_task_node({"cls"});
    graph->connect(source, 0, task, 0);
    scene->rebuild();
    scene->model_changed();
    require(run->isEnabled(), "valid workflow cannot open its task");
    require(!remove->isEnabled(), "delete is available without a selection");
    for (auto* item : scene->items()) {
        if (item->data(0).toInt() == task) { item->setSelected(true); break; }
    }
    require(remove->isEnabled(), "delete did not follow the node selection");
    scene->clearSelection();
    require(!remove->isEnabled(), "delete stayed enabled after clearing the selection");
    scene->node_activated(task);
    require(!run->isVisible() && !remove->isVisible() && !edit->isEnabled(), "workflow commands remained available in the node workspace");
    workflow->showWorkflowWorkspace();
    require(run->isVisible() && remove->isVisible() && edit->isEnabled(), "workflow commands did not return with the canvas");
    int dialogs_seen = 0;
    QTimer dialogs;
    QObject::connect(&dialogs, &QTimer::timeout, [&] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            ++dialogs_seen;
            dialog->reject();
        }
    });
    dialogs.start(5);
    save->trigger();
    dialogs.stop();
    require(dialogs_seen == 0 && !window.windowTitle().endsWith(" *"), "workflow fixture could not be saved");
    studio::ProjectManager saved;
    saved.open_project(project_file);
    require(saved.workflows().size() == 2 && saved.workflows()[1].document.graph.nodes().size() == 2,
            "saved project lost the workflow fixture");
    const QString title = window.windowTitle();

    const auto project_busy = project->busy;
    const auto workflow_busy = workflow->busy;
    project->busy = workflow->busy = [] { return true; };
    project->set_editing_enabled(false);
    workflow->setEditingEnabled(false);
    project->refresh();
    project->updateRecentProjects();
    workflow->refreshValidation();
    workflow->refreshProjectUi();
    require(!home->isEnabled() && !recent->isEnabled() && !save->isEnabled() &&
            !add->isEnabled() && !view->isEnabled() && !run->isEnabled(),
            "module refresh unlocked commands during execution");

    dialogs.start(5);
    home->create_project();
    home->open_project();
    home->open_recent(project_file);
    home->delete_project(project_file);
    require(!recent->actions().isEmpty(), "recent project fixture is missing");
    recent->actions().front()->trigger();
    tabs->setCurrentIndex(0);
    QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0));
    QMetaObject::invokeMethod(tabs, "tabBarDoubleClicked", Qt::DirectConnection, Q_ARG(int, 0));
    dialogs.stop();
    require(dialogs_seen == 0 && QFile::exists(project_file) && tabs->count() == 2 &&
            tabs->currentIndex() == 1 && scene->model() == graph && window.windowTitle() == title,
            "a direct module request bypassed the execution lock");

    project->busy = project_busy;
    workflow->busy = workflow_busy;
    project->refresh();
    workflow->setEditingEnabled(true);
    workflow->refreshValidation();
    require(home->isEnabled() && recent->isEnabled() && add->isEnabled() && run->isEnabled(),
            "commands did not recover after execution");
    tabs->setCurrentIndex(0);
    require(scene->model() != graph, "workflow tab did not rebind the canvas");
    QTimer::singleShot(0, [] {
        if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
            dialog->setTextValue("Renamed workflow");
            dialog->accept();
        }
    });
    QMetaObject::invokeMethod(tabs, "tabBarDoubleClicked", Qt::DirectConnection, Q_ARG(int, 1));
    require(tabs->currentIndex() == 1 && tabs->tabText(1).startsWith("Renamed workflow"),
            "renaming an inactive tab did not activate its workflow");
    QTimer::singleShot(0, [] {
        if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            message->button(QMessageBox::Yes)->click();
    });
    QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 1));
    require(tabs->count() == 1 && tabs->currentIndex() == 0, "closing a workflow left stale tabs");
    dialogs.start(5);
    save->trigger();
    dialogs.stop();
    require(dialogs_seen == 0 && !window.windowTitle().endsWith(" *"), "workflow close could not be saved");
    saved.open_project(project_file);
    require(saved.workflows().size() == 1, "closed workflow remained in the saved project");
}

} // namespace

int main(int argc, char** argv) try {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QTemporaryDir settings_directory;
    require(settings_directory.isValid(), "could not create test settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_directory.path());

    {
        studio::MainWindow window;
        window.resize(1280, 800);
        window.show();
        app.processEvents();
        require(window.windowTitle() == "DeepLearning Workflow Studio", "empty window title is incorrect");
        require(!window.windowIcon().pixmap(32, 32).isNull(), "window icon cannot be rendered");
        require(!window.findChild<QWidget*>("homeNavigation"), "home sidebar still exists");
        require(!window.findChild<QDockWidget*>("outputDock"), "diagnostic output is still docked in the workspace");
        auto* activity = window.findChild<QAction*>("activityLogAction");
        auto* activity_dialog = window.findChild<QDialog*>("activityLogDialog");
        require(activity && activity_dialog && !activity_dialog->isVisible(), "log viewer is not available on demand");
        activity->trigger();
        require(activity_dialog->isVisible(), "activity log did not open in a separate window");
        activity_dialog->hide();
        require(window.findChild<QPushButton*>("homeNewProjectButton") &&
                window.findChild<QPushButton*>("homeOpenProjectButton"), "home project actions are missing");
        auto* file_menu = window.findChild<QMenu*>("fileMenu");
        require(file_menu && file_menu->title() == "File", "file menu is missing");
        for (const char* name : {"newProjectAction", "openProjectAction", "saveProjectAction", "saveProjectAsAction"}) {
            auto* action = window.findChild<QAction*>(name);
            require(action && file_menu->actions().contains(action), "project action is missing from file menu");
        }
        require(!window.findChild<QAction*>("saveProjectAction")->isEnabled() &&
                !window.findChild<QAction*>("saveProjectAsAction")->isEnabled(), "empty project can be saved");
        capture(window, "STUDIO_HOME_EN_CAPTURE");

        bool new_dialog_seen = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<studio::NewProjectDialog*>(QApplication::activeModalWidget());
            if (dialog) { new_dialog_seen = true; dialog->reject(); }
        });
        window.findChild<QPushButton*>("homeNewProjectButton")->click();
        require(new_dialog_seen, "new project card did not open the project dialog");
        bool open_dialog_seen = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (dialog) { open_dialog_seen = true; dialog->reject(); }
        });
        window.findChild<QPushButton*>("homeOpenProjectButton")->click();
        require(open_dialog_seen, "open project card did not open the file dialog");

        studio::ProjectManager demo;
        demo.new_project("Vision Demo");
        demo.save_project(settings_directory.filePath("Vision Demo"));
        studio::RecentProjectStore().add(demo.project_file());
        auto* home = window.findChild<studio::ProjectHome*>("projectHome");
        require(home, "home page is missing");
        home->refresh_recent_projects();
        auto* recent = home->findChild<QTableWidget*>("recentProjectsTable");
        require(recent && recent->rowCount() == 1 && recent->item(0, 1), "recent project was not displayed");
        app.processEvents();
        const QPoint recent_position = recent->visualItemRect(recent->item(0, 1)).center();
        QTest::mouseClick(recent->viewport(), Qt::LeftButton, Qt::NoModifier, recent_position);
        QTest::mouseDClick(recent->viewport(), Qt::LeftButton, Qt::NoModifier, recent_position);
        require(window.windowTitle().startsWith("Vision Demo - ") &&
                window.findChild<QAction*>("saveProjectAction")->isEnabled(), "recent project did not open");
        module_command_boundaries(window, demo.project_file());
        window.findChild<QAction*>("closeProjectAction")->trigger();
        studio::RecentProjectStore().remove(demo.project_file());
        home->refresh_recent_projects();

        change_language(window, "zh_CN", true);
        require(studio::ui_language() == "zh_CN",
                "language choice was not saved");
        auto* shortcut = window.findChild<QComboBox*>("homeLanguageSelector");
        require(shortcut && shortcut->currentData() == "zh_CN", "language shortcut was not synchronized");
        QTimer::singleShot(0, [] {
            if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) message->accept();
        });
        shortcut->setCurrentIndex(shortcut->findData("en"));
        require(studio::ui_language() == "en",
                "language shortcut did not save the choice");
        change_language(window, "zh_CN", true);
        change_language(window, "en", false);
        require(studio::ui_language() == "zh_CN",
                "cancel changed the saved language");
    }

    QTranslator qt_translator;
    QTranslator studio_translator;
    studio::install_ui_language(app, qt_translator, studio_translator);
    {
        studio::MainWindow window;
        window.resize(1280, 800);
        window.show();
        app.processEvents();
        auto* file_menu = window.findChild<QMenu*>("fileMenu");
        require(file_menu && file_menu->title() == QString::fromUtf8("文件"), "saved language was not applied to the menu");
        require(window.findChild<QAction*>("activityLogAction")->text().contains(QString::fromUtf8("日志")),
                "activity log menu was not translated");
        auto* create = window.findChild<QPushButton*>("homeNewProjectButton");
        require(create && create->accessibleName().contains(QString::fromUtf8("新建项目")), "home page was not translated");
        studio::NewProjectDialog new_project;
        studio::SaveProjectAsDialog save_as("Example", settings_directory.path());
        require(new_project.windowTitle() == QString::fromUtf8("新建项目") &&
                save_as.windowTitle() == QString::fromUtf8("项目另存为"), "project dialogs were not translated");
        require(studio::ProjectPathValidator::validate_name("") == QString::fromUtf8("请输入项目名称。"),
                "project validation message was not translated");
        capture(window, "STUDIO_HOME_ZH_CAPTURE");
        window.resize(760, 480);
        app.processEvents();
        require(!window.findChild<QWidget*>("homeHeroVisual")->isVisible(), "narrow home did not adapt");
        capture(window, "STUDIO_HOME_NARROW_CAPTURE");
    }
    {
        studio::MainWindow window;
        window.show();
        auto* menu = window.menuBar()->addMenu("Test extension");
        auto* page = new QWidget;
        window.mount_page(page);
        auto* dock = window.mount_dock("Test panel", "testExtensionDock", new QWidget, Qt::RightDockWidgetArea);
        QPointer<QAction> command;
        {
            QObject module;
            command = new QAction("Show test page", &module);
            menu->addAction(command);
            QObject::connect(command, &QAction::triggered, &module, [&] {
                window.show_task(page);
                dock->show();
            });
            command->trigger();
            app.processEvents();
            require(window.is_current_page(page) && dock->isVisible(), "extension could not mount its page and panel");
            dock->toggleViewAction()->trigger();
            require(!dock->isVisible(), "extension panel visibility could not be toggled");
        }
        require(command.isNull() && menu->actions().isEmpty(), "module action outlived its owner");
    }
    std::cout << "Shell and language checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
