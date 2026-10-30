#include "activity_log.h"
#include "execution_module.h"
#include "model_actions.h"
#include "node_workspace.h"
#include "project_manager.h"
#include "settings_module.h"
#include "task_workspace_service.h"
#include "task_workspace_pages.h"
#include "ui_language.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QWidget>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

void activity_log_contract(QWidget& host, const QString& root) {
    QString path;
    {
        studio::ActivityLog log(&host);
        QMenu menu;
        log.mount(menu);
        path = log.file_path();
        require(QFileInfo(path).absolutePath() ==
                    QDir(QCoreApplication::applicationDirPath()).filePath("logs"),
                "activity log was not placed beside the executable");
        log.append_log(QString::fromUtf8("File-backed diagnostic 日志"));
        require(!log.isVisible(), "appending a log opened a UI panel");
        QFile file(path);
        require(file.open(QIODevice::ReadOnly) && file.readAll().contains("File-backed diagnostic"),
                "activity log was not flushed to disk");
        require(menu.actions().size() == 1, "activity log action was not contributed");
        menu.actions().front()->trigger();
        auto* content = log.findChild<QPlainTextEdit*>("activityLogContent");
        require(log.isVisible() && content && content->isReadOnly() &&
                content->toPlainText().contains(QString::fromUtf8("日志")),
                "activity log popup did not read its UTF-8 file");
        log.hide();
        const QString run_directory = QDir(root).filePath("runs/run_1");
        require(QDir().mkpath(run_directory), "cannot create training log fixture");
        QFile run(QDir(run_directory).filePath("training.log"));
        require(run.open(QIODevice::WriteOnly), "cannot write training log fixture");
        run.write("Saved training diagnostic\n");
        run.close();
        log.show_run(root, "run_1");
        require(log.isVisible() && content->toPlainText().contains("Saved training diagnostic"),
                "saved training log did not open in the popup");
        log.hide();
    }
    require(QFile::exists(path), "closing the popup deleted the persistent log");
    require(QFile::remove(path), "cannot clean up the test session log");
}

void execution_failure_contract(studio::ProjectManager& project, studio::NodeWorkspace& workspace,
                                int node, const studio::ProjectModel& model, QWidget& host) {
    auto& graph = project.active_workflow().document.graph;
    const QString image_path = QDir(project.root_directory()).filePath("input.png");
    QImage image(16, 16, QImage::Format_RGB888);
    image.fill(Qt::white);
    require(image.save(image_path), "cannot create execution input");
    const int source = graph.add_node(studio::NodeKind::DatasetInput);
    std::get<studio::DatasetConfig>(graph.node(source).config).items = {
        {image_path.toStdString(), {}, true, "sample"}};
    project.active_workflow().document.positions[source] = {-200, 0};
    graph.connect(source, 0, node, 0);
    project.mark_active_dirty();
    project.save_project();
    const auto previous_status = studio::TaskWorkspaceService(graph, node).task().runtime.inference_status;

    studio::ExecutionModule execution(project, workspace, &host);
    QEventLoop completion;
    bool startup_locked = false;
    bool finished_unlocked = false;
    bool failed = false;
    QString runtime_state;
    int unlocks = 0;
    int notifications = 0;
    execution.editing_changed = [&](bool enabled) {
        if (enabled) {
            ++unlocks;
            finished_unlocked = !execution.is_running();
        } else {
            startup_locked = execution.is_running();
            // A second module command must be rejected before inspecting its target.
            execution.start_training(0);
        }
    };
    execution.log = [&](const QString& text) { failed = failed || text.startsWith("Inference failed:"); };
    execution.status = [](const QString&, int) {};
    execution.show_log = [] {};
    execution.runtime_status = [&](const QString&, const char* state) { runtime_state = state; };
    execution.changed = [&] { ++notifications; completion.quit(); };
    QTimer dialogs;
    int unexpected_dialogs = 0;
    QObject::connect(&dialogs, &QTimer::timeout, [&] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            ++unexpected_dialogs;
            dialog->reject();
        }
    });
    dialogs.start(5);
    studio::InferenceLaunchRequest launch;
    launch.model_id = QString::fromStdString(model.id);
    launch.weight_id = QString::fromStdString(model.weights.front().id);
    launch.device = jocelin::config::cpu_device();
    execution.start_inference(node, launch);
    require(startup_locked && execution.is_running(), "execution unlocked before its Job started");
    completion.exec();
    dialogs.stop();
    require(failed && finished_unlocked && !execution.is_running() && unlocks == 1 &&
            notifications == 1 && unexpected_dialogs == 0 && runtime_state == "error" &&
            studio::TaskWorkspaceService(graph, node).task().runtime.inference_status == previous_status,
            "failed execution did not restore its module boundary exactly once");
}
}

int main(int argc, char** argv) try {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "cannot create module fixture");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QWidget host;
    activity_log_contract(host, temporary.path());
    QMenu menu;
    QToolBar toolbar;
    studio::SettingsModule settings(&host);
    settings.mount(menu, toolbar);
    require(menu.actions().size() == 1, "settings did not contribute its action");
    bool cancelled = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto* language = dialog->findChild<QComboBox*>("languageSelector");
        if (language) { language->setCurrentIndex(language->findData("zh_CN")); cancelled = true; }
        dialog->reject();
    });
    menu.actions().front()->trigger();
    require(cancelled && studio::ui_language() == "en", "standalone settings cancellation changed preferences");

    studio::ProjectManager project;
    project.new_project("Module test");
    const int node = project.active_workflow().document.graph.add_task_node({"sg"});
    project.active_workflow().document.positions[node] = {0, 0};
    project.save_project(temporary.filePath("Project"));
    studio::NodeWorkspace workspace(&host);
    studio::ModelActions models(project, &host);
    int changes = 0;
    models.models_changed = [&] { ++changes; };
    models.log = [](const QString&) {};
    models.status = [](const QString&, int) {};
    models.mount(workspace);
    bool import_cancelled = false;
    QTimer::singleShot(0, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            import_cancelled = true; dialog->reject();
        }
    });
    workspace.import_model_requested(node);
    require(import_cancelled && changes == 0 && !project.is_dirty(), "cancelled standalone model import changed the project");
    workspace.delete_models_requested(node, {});
    require(changes == 0, "empty model deletion changed the project");

    const auto invalid_manifest = temporary.filePath("invalid.json");
    QFile invalid(invalid_manifest);
    require(invalid.open(QIODevice::WriteOnly), "cannot write invalid manifest fixture");
    invalid.write("[]");
    invalid.close();
    bool rejected = false;
    QTimer import_dialogs;
    QObject::connect(&import_dialogs, &QTimer::timeout, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->findChild<QLineEdit*>("fileNameEdit")->setText(invalid_manifest);
            QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
        } else if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            rejected = message->text().contains("JSON object");
            message->accept();
        }
    });
    import_dialogs.start(20);
    workspace.import_model_requested(node);
    import_dialogs.stop();
    require(rejected && changes == 0 && !project.is_dirty(), "invalid model import changed the project");

    studio::TaskWorkspaceService service(project.active_workflow().document.graph, node);
    studio::ProjectModel model;
    model.name = "Export fixture";
    model.task = jocelin::config::ToolType::Sg;
    model.model_index = 0;
    model.model_scale = jocelin::config::ModelScale::S;
    model.image_width = 256;
    model.image_height = 256;
    model.classes = {{"foreground", "foreground", 1}};
    model.training_run_id = "export_fixture";
    const auto model_directory = QDir(project.node_models_directory(node)).filePath("export_fixture");
    require(QDir().mkpath(model_directory), "cannot create model fixture directory");
    model.asset_directory = model_directory.toStdString();
    const auto weight_path = QDir(model_directory).filePath("weights.pt");
    QFile weight_file(weight_path);
    require(weight_file.open(QIODevice::WriteOnly), "cannot write export weight fixture");
    weight_file.write("fixture bytes");
    weight_file.close();
    jocelin::model::ModelWeight weight;
    weight.id = "weights";
    weight.path = weight_path.toStdString();
    weight.created_at = "2026-10-29T00:00:00Z";
    model.weights.push_back(weight);
    const auto added = service.add_model(model);
    project.mark_active_dirty();
    project.save_project();
    const auto export_directory = temporary.filePath("Export");
    require(QDir().mkpath(export_directory), "cannot create export destination");
    QTimer export_dialog;
    QObject::connect(&export_dialog, &QTimer::timeout, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->findChild<QLineEdit*>("fileNameEdit")->setText(export_directory);
            QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
        }
    });
    export_dialog.start(20);
    workspace.export_model_requested(node, QString::fromStdString(added.id));
    export_dialog.stop();
    const auto exports = QDir(export_directory).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    require(exports.size() == 1, "model export did not create a bundle");
    QDir bundle(QDir(export_directory).filePath(exports.front()));
    QFile manifest(bundle.filePath("model.json"));
    require(manifest.open(QIODevice::ReadOnly), "model export manifest is missing");
    const auto exported = QJsonDocument::fromJson(manifest.readAll()).object();
    require(exported.value("format") == "deep_learning_project_model" &&
            exported.value("name") == QString::fromStdString(added.name) && bundle.exists("weights.pt") &&
            changes == 0 && !project.is_dirty(), "model export changed data or lost bundle metadata");
    // The copy-only weight fixture must fail in the CPU worker, without running a real model.
    execution_failure_contract(project, workspace, node, added, host);
    std::cout << "Standalone feature modules passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}
