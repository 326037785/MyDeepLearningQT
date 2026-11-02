#include "activity_log.h"
#include "execution_module.h"
#include "model_actions.h"
#include "project_model_browser.h"
#include "node_workspace.h"
#include "project_manager.h"
#include "settings_module.h"
#include "task_workspace_service.h"
#include "task_workspace_pages.h"
#include "ui_language.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QWidget>
#include <iostream>
#include <stdexcept>
#include <ATen/Parallel.h>

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
    require(menu.actions().size() == 1 && menu.actions().front()->menu(),
            "settings did not contribute its preferences menu");

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
    model.created_at = weight.created_at;
    model.weights.push_back(weight);
    const auto added = service.add_model(model);
    project.mark_active_dirty();
    project.save_project();
    // Explicit Select all includes hidden branches and empty model rows;
    // checking every displayed branch individually must keep branch-only scope.
    {
        studio::ProjectModelBrowser browser;
        studio::ProjectModel multiple = added;
        jocelin::model::ModelWeight alternate = multiple.weights.front();
        alternate.id = "alternate";
        multiple.weights.push_back(alternate);
        studio::ProjectModel empty = added;
        empty.id = "empty_model";
        empty.weights.clear();
        browser.set_models({multiple, empty});
        QCheckBox* select_all = browser.findChild<QCheckBox*>("projectModelSelectAllCheckBox");
        QAction* remove = browser.findChild<QAction*>("projectModelDeleteAction");
        int branch_deletions = 0;
        int model_clears = 0;
        browser.delete_model_weights_requested = [&](const studio::ModelWeightSelection& weights) {
            branch_deletions = static_cast<int>(weights.size());
        };
        browser.clear_models_requested = [&] { ++model_clears; };
        for (QCheckBox* target : browser.findChildren<QCheckBox*>("projectModelWeightDeleteCheckBox")) {
            target->setChecked(true);
        }
        remove->trigger();
        require(branch_deletions == 1 && model_clears == 0,
                "checking branches escalated into model deletion");
        browser.set_models({multiple, empty});
        select_all->click();
        remove->trigger();
        require(model_clears == 1 && branch_deletions == 1,
                "Select all did not clear complete models");
        browser.set_models({empty});
        require(select_all->isEnabled(), "empty model rows cannot be selected for deletion");
        select_all->click();
        remove->trigger();
        require(model_clears == 2, "Select all retained empty model rows");
        browser.set_models({});
        require(!remove->isEnabled() && !select_all->isEnabled(),
                "model deletion selection survived an empty list");
    }
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
    // Exercise real CPU checkpoint completion through the Qt job boundary.
    at::set_num_threads(1);
    service.synchronize();
    service.add_class("foreground");
    service.set_segmentation_sample_empty("sample");
    service.set_sample_split("sample", "train");
    service.task().training.image_width = service.task().training.image_height = 64;
    service.task().training.batch_size = 1;
    service.task().training.workers = 0;
    studio::ExecutionModule execution(project, workspace, &host);
    execution.log = [](const QString&) {};
    execution.show_log = [] {};
    execution.editing_changed = [](bool) {};
    execution.runtime_status = [](const QString&, const char*) {};
    QTimer training_dialogs;
    QString training_error;
    QObject::connect(&training_dialogs, &QTimer::timeout, [&] {
        if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
            dialog->setTextValue("CPU");
            dialog->accept();
        } else if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            training_error = message->text();
            message->accept();
        }
    });
    training_dialogs.start(5);
    QString paused_model;
    QString stopped_model;
    for (int scenario = 0; scenario < 5; ++scenario) {
        service.task().training.epochs = scenario == 2 ? 1 : 100;
        QEventLoop finished;
        execution.changed = [&] { finished.quit(); };
        execution.status = [&](const QString& text, int) {
            if (scenario == 1 && text.startsWith("Training epoch ")) {
                execution.stop();
            }
        };
        studio::TrainingLaunchRequest request;
        if (scenario == 4) {
            request.mode = "resume";
            request.model_id = paused_model;
        }
        execution.start_training(node, request);
        require(training_error.isEmpty() && execution.is_running(), "CPU training did not start");
        if (scenario == 0) {
            execution.stop();
        } else if (scenario >= 3) {
            execution.pause();
        }
        finished.exec();
        const auto* row = service.find_model(QString::fromStdString(service.task().runtime.training_model_id));
        require(row, "training completion lost its ModelEntry");
        const auto* checkpoint = jocelin::model::find_training_weight(*row);
        if (scenario < 2) {
            require(row->training_status == jocelin::model::ModelTrainingStatus::Stopped,
                "Stop changed the model lifecycle state");
            if (scenario == 0) {
                require(row->completed_epochs == 0 && !checkpoint, "early Stop fabricated a checkpoint");
            } else {
                require(row->completed_epochs >= 1 && checkpoint && QFileInfo(QString::fromStdString(checkpoint->path)).isFile() &&
                    QFileInfo(QString::fromStdString(checkpoint->path)).fileName() == "best.pt",
                    "Stop after an epoch failed to register best.pt");
                stopped_model = QString::fromStdString(row->id);
            }
        } else {
            const bool completed = scenario == 2;
            require(row->training_status == (completed ? jocelin::model::ModelTrainingStatus::Completed
                                                       : jocelin::model::ModelTrainingStatus::Paused) && checkpoint &&
                QFileInfo(QString::fromStdString(checkpoint->path)).fileName() == (completed ? "best.pt" : "last.pt"),
                "Completed or Paused selected the wrong checkpoint");
            if (scenario == 3) {
                paused_model = QString::fromStdString(row->id);
            } else if (scenario == 4) {
                require(QString::fromStdString(row->id) == paused_model && row->completed_epochs >= 2,
                    "Resume did not continue the paused ModelEntry");
            }
        }
        studio::ProjectManager verified;
        verified.open_project(project.project_file());
    }
    studio::TrainingLaunchRequest resume_stopped;
    resume_stopped.mode = "resume";
    resume_stopped.model_id = stopped_model;
    execution.start_training(node, resume_stopped);
    require(!execution.is_running() && !training_error.isEmpty(), "Stopped model accepted Resume");
    training_error.clear();
    QEventLoop inferred;
    execution.changed = [&] { inferred.quit(); };
    execution.start_inference(node, {stopped_model, "libtorch_fp32", jocelin::config::cpu_device()});
    require(execution.is_running() && training_error.isEmpty(), "Stopped checkpoint could not start inference");
    inferred.exec();
    require(service.inference_result(stopped_model, "libtorch_fp32"), "Stopped model did not produce inference results");
    training_dialogs.stop();
    // Delete through the real browser -> ModelActions -> ProjectManager path.
    // Include an empty model and verify every workspace and persisted owner.
    studio::ProjectModel empty = added;
    empty.id.clear();
    empty.weights.clear();
    empty.asset_directory = QDir(project.node_models_directory(node)).filePath("empty").toStdString();
    require(QDir().mkpath(QString::fromStdString(empty.asset_directory)), "cannot create empty model fixture");
    service.add_model(empty);
    const int peer = project.active_workflow().document.graph.add_task_node({"sg"});
    project.active_workflow().document.positions[peer] = {200, 0};
    studio::ProjectModel peer_model = empty;
    peer_model.asset_directory = QDir(project.node_models_directory(peer)).filePath("peer").toStdString();
    require(QDir().mkpath(QString::fromStdString(peer_model.asset_directory)), "cannot create peer model fixture");
    studio::TaskWorkspaceService peer_service(project.active_workflow().document.graph, peer);
    peer_service.add_model(peer_model);
    project.mark_active_dirty();
    project.save_project();
    QStringList removed_directories;
    for (const studio::ProjectModel& value : service.models()) {
        removed_directories.push_back(QString::fromStdString(value.asset_directory));
    }
    models.models_changed = [&] { ++changes; workspace.refresh_models(); };
    workspace.edit_node(&project.active_workflow().document.graph, node, {});
    QWidget* browser = workspace.findChild<QWidget*>("trainingProjectModelBrowser");
    QCheckBox* select_all = browser ? browser->findChild<QCheckBox*>("projectModelSelectAllCheckBox") : nullptr;
    QAction* remove = browser ? browser->findChild<QAction*>("projectModelDeleteAction") : nullptr;
    require(select_all && remove && select_all->isEnabled(), "Select all is unavailable in the Task workspace");
    QTimer deletion_dialogs;
    QObject::connect(&deletion_dialogs, &QTimer::timeout, [&] {
        QMessageBox* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (message && message->button(QMessageBox::Yes)) {
            message->button(QMessageBox::Yes)->click();
        }
    });
    select_all->click();
    deletion_dialogs.start(5);
    remove->trigger();
    deletion_dialogs.stop();
    require(service.models().empty() && service.task().runtime.results.empty() &&
            service.task().runtime.training_model_id.empty(), "Select all deletion retained model state");
    for (QTreeWidget* list : workspace.findChildren<QTreeWidget*>("projectModelBrowserList")) {
        require(list->topLevelItemCount() == 0, "a workspace retained deleted model rows");
    }
    QWidget* detail = browser->findChild<QWidget*>("projectModelDetailCard");
    for (QLabel* label : detail->findChildren<QLabel*>()) {
        require(label->toolTip().isEmpty(), "deleted model asset path remained in details");
    }
    for (const QString& directory : removed_directories) {
        require(!QDir(directory).exists(), "deleted model assets remain");
    }
    require(!QDir(QFileInfo(project.node_models_directory(node)).absolutePath() + "/inference").exists(),
            "deleted model inference assets remain");
    studio::ProjectManager reopened;
    reopened.open_project(project.project_file());
    require(studio::TaskWorkspaceService(reopened.active_workflow().document.graph, node).models().empty() &&
            studio::TaskWorkspaceService(reopened.active_workflow().document.graph, peer).models().size() == 1 &&
            QDir(QString::fromStdString(peer_model.asset_directory)).exists(),
            "Select all deletion failed persistence or affected another Node");
    std::cout << "Standalone feature modules passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}
