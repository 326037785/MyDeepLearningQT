#include "modules/diagnostics/activity_log.h"
#include "modules/execution/application/execution_module.h"
#include "modules/model_management/application/model_actions.h"
#include "modules/task/ui/common/project_model_browser.h"
#include "modules/workflow/ui/node_workspace.h"
#include "modules/project/application/project_manager.h"
#include "modules/settings/ui/settings_module.h"
#include "modules/task/application/task_workspace_service.h"
#include "modules/task/application/task_launch_requests.h"
#include "modules/task/ui/pages/training_workspace.h"
#include "modules/settings/application/ui_language.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QContextMenuEvent>
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
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QWidget>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <ATen/Parallel.h>
#include <training/training_service.h>

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
    {
        studio::TrainingWorkspace training;
        studio::ProjectModel source = added;
        source.classes.clear();
        source.model_index = service.task().model_index;
        source.model_scale = jocelin::config::model_scale_from_string(service.task().model_scale);
        training.set_service(&service);
        training.set_models({source});
        training.resize(1280, 720);
        training.show();
        QApplication::processEvents();
        QTreeWidget* list = training.findChild<QTreeWidget*>("projectModelBrowserList");
        QTreeWidgetItem* item = list->topLevelItem(0);
        QStyleOptionViewItem option;
        option.initFrom(list);
        option.rect = list->visualItemRect(item);
        option.features = QStyleOptionViewItem::HasCheckIndicator;
        const QPoint position = list->style()->subElementRect(
            QStyle::SE_ItemViewItemCheckIndicator, &option, list).center();
        QMouseEvent press(QEvent::MouseButtonPress, position, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, position, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(list->viewport(), &press);
        QApplication::sendEvent(list->viewport(), &release);
        require(item->checkState(0) == Qt::Checked,
                "completed model checkbox did not accept a mouse click");
        studio::TrainingLaunchRequest launch;
        training.start_requested = [&](const studio::TrainingLaunchRequest& request) { launch = request; };
        training.request_start();
        require(launch.mode == "inherit" && launch.model_id == QString::fromStdString(source.id),
                "checked compatible model did not launch inheritance");
        source.classes = added.classes;
        training.set_models({source});
        training.request_start();
        require(launch.mode == "transfer" && launch.model_id == QString::fromStdString(source.id),
                "checked model with a different class schema did not launch transfer");
        source.training_status = jocelin::model::ModelTrainingStatus::Stopped;
        training.set_models({source});
        list->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        QApplication::sendEvent(list->viewport(), &press);
        QApplication::sendEvent(list->viewport(), &release);
        require(list->topLevelItem(0)->checkState(0) == Qt::Checked,
                "stopped model checkbox did not accept a mouse click");
        launch = {};
        training.request_start();
        require(launch.mode == "transfer" && launch.model_id == QString::fromStdString(source.id),
                "stopped model with weights could not be selected for transfer");
        source.classes.clear();
        training.set_models({source});
        training.request_start();
        require(launch.mode == "inherit" && launch.model_id == QString::fromStdString(source.id),
                "stopped model with weights could not be selected for inheritance");
        source.weights.clear();
        training.set_models({source});
        require(!list->topLevelItem(0)->flags().testFlag(Qt::ItemIsUserCheckable),
                "stopped model without weights became a training source");

        QFile history(QDir(model_directory).filePath(jocelin::training::training_history_filename));
        require(history.open(QIODevice::WriteOnly), "cannot write training history fixture");
        const QByteArray csv = QByteArray(jocelin::training::training_history_header) +
            "\n1,1.2,,Mean IoU,,0.3\n3,0.7,0.8,Mean IoU,0,\n";
        require(history.write(csv) == csv.size(), "cannot write training history rows");
        history.close();
        studio::ProjectModel without_history = source;
        without_history.id = "without_training_history";
        without_history.asset_directory = temporary.filePath("without-training-history").toStdString();
        training.set_models({source, without_history});
        QWidget* chart = training.findChild<QWidget*>("embeddedLossChart");
        require(chart && list->topLevelItemCount() == 2, "training history chart or model rows are missing");
        list->setCurrentItem(list->topLevelItem(1));
        const QImage empty_chart = chart->grab().toImage();
        list->setCurrentItem(list->topLevelItem(0));
        require(!empty_chart.isNull() && chart->grab().toImage() != empty_chart,
                "selecting a saved model did not render training history");
        list->setCurrentItem(list->topLevelItem(1));
        require(chart->grab().toImage() == empty_chart, "model selection retained a different model's history");

        const QList<QLabel*> labels = training.findChildren<QLabel*>();
        const auto metric_label_is = [&labels](const QString& text) {
            return std::any_of(labels.cbegin(), labels.cend(), [&text](const QLabel* label) {
                return label->text() == text;
            });
        };
        jocelin::train::TrainingProgress progress;
        progress.completed_epochs = 1;
        progress.total_epochs = 2;
        progress.loss = 1.2;
        progress.validation_metric_name = "Mean IoU";
        training.update_progress(progress);
        require(metric_label_is(QStringLiteral("Mean IoU —")), "missing validation metric was displayed as a number");
        progress.completed_epochs = 2;
        progress.validation_metric = 0.0;
        training.update_progress(progress);
        require(metric_label_is(QStringLiteral("Mean IoU 0")), "a measured zero was displayed as missing");
    }
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
        browser.resize(800, 400);
        browser.show();
        QApplication::processEvents();
        QTreeWidget* list = browser.findChild<QTreeWidget*>("projectModelBrowserList");
        QToolButton* detail_button = browser.findChild<QToolButton*>("projectModelDetailsButton");
        QScrollArea* detail_scroll = browser.findChild<QScrollArea*>("projectModelDetailScroll");
        require(list && detail_button && detail_scroll && detail_scroll->isHidden(),
                "model details did not start collapsed");
        browser.resize(420, 210);
        detail_button->click();
        QApplication::processEvents();
        QScrollBar* detail_bar = detail_scroll->verticalScrollBar();
        require(detail_button->isChecked() && detail_scroll->isVisible() && detail_bar->maximum() > 0,
                "model details did not open with vertical scrolling in a short pane");
        const QList<QLabel*> detail_fields = detail_scroll->widget()->findChildren<QLabel*>();
        require(!detail_fields.isEmpty(), "model details did not contain fields");
        for (QLabel* field : detail_fields) {
            const int required_height = field->hasHeightForWidth()
                ? field->heightForWidth(field->width()) : field->minimumSizeHint().height();
            require(field->height() >= required_height, "model details clipped a field");
        }
        detail_bar->setValue(detail_bar->maximum());
        QLabel* last_field = detail_fields.back();
        const QPoint last_field_bottom = last_field->mapTo(detail_scroll->viewport(), last_field->rect().bottomLeft());
        require(detail_scroll->viewport()->rect().contains(last_field_bottom),
                "the last model detail could not be reached by scrolling");
        detail_button->click();
        require(!detail_button->isChecked() && detail_scroll->isHidden(), "eye button did not close model details");
        list->itemDoubleClicked(list->topLevelItem(0), 0);
        require(detail_button->isChecked() && detail_scroll->isVisible(), "double-click did not open model details");
        list->itemDoubleClicked(list->topLevelItem(0), 0);
        require(!detail_button->isChecked() && detail_scroll->isHidden(), "double-click did not close model details");
        browser.resize(800, 400);
        QApplication::processEvents();
        QAction* remove_row = browser.findChild<QAction*>("projectModelDeleteRowAction");
        QAction* menu_action = remove_row;
        QStringList removed_rows;
        browser.delete_models_requested = [&](const QStringList& ids) { removed_rows = ids; };
        bool menu_opened = false;
        QTimer close_menu;
        QObject::connect(&close_menu, &QTimer::timeout, [&] {
            QMenu* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (popup) {
                menu_opened = true;
                menu_action->trigger();
                popup->close();
            }
        });
        for (int row = 0; row < 2; ++row) {
            menu_opened = false;
            const QPoint row_position(40, list->visualItemRect(list->topLevelItem(row)).center().y());
            QContextMenuEvent context(QContextMenuEvent::Mouse, row_position,
                                     list->viewport()->mapToGlobal(row_position));
            close_menu.start(5);
            QApplication::sendEvent(list->viewport(), &context);
            close_menu.stop();
            require(menu_opened && removed_rows == QStringList{row == 0
                        ? QString::fromStdString(multiple.id) : QStringLiteral("empty_model")},
                    "right click did not offer deletion of the clicked model row");
        }
        QWidget* cell = list->itemWidget(list->topLevelItem(0), 1);
        QComboBox* branch = cell->findChild<QComboBox*>("projectModelWeightBranch");
        branch->setCurrentIndex(1);
        QMetaObject::invokeMethod(branch, "activated", Qt::DirectConnection, Q_ARG(int, 1));
        menu_action = browser.findChild<QAction*>("projectModelDeleteCellAction");
        QStringList removed_weight;
        browser.delete_model_weight_requested = [&](const QString& model_id, const QString& weight_id) {
            removed_weight = QStringList{model_id, weight_id};
        };
        for (QWidget* content : {cell, static_cast<QWidget*>(branch)}) {
            menu_opened = false;
            const QPoint position = content->rect().center();
            QContextMenuEvent context(QContextMenuEvent::Mouse, position, content->mapToGlobal(position));
            close_menu.start(5);
            QApplication::sendEvent(content, &context);
            close_menu.stop();
            require(menu_opened && removed_weight == QStringList{QString::fromStdString(multiple.id), "alternate"},
                    "right click did not target the displayed weight branch");
        }
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

        browser.set_branch_check_selection();
        browser.set_models({multiple, empty});
        studio::ModelWeightSelection removed_branches;
        browser.delete_model_weights_requested = [&](const studio::ModelWeightSelection& weights) {
            removed_branches = weights;
        };
        QTreeWidgetItem* model_row = list->topLevelItem(0);
        cell = list->itemWidget(model_row, 1);
        branch = cell->findChild<QComboBox*>("projectModelWeightBranch");
        QCheckBox* target = cell->findChild<QCheckBox*>("projectModelWeightDeleteCheckBox");
        require(branch && target, "Inference branch controls are missing");
        const QString selected_branch = branch->currentData().toString();
        const QString selected_model = QString::fromStdString(multiple.id);
        target->click();
        const int other_index = 1 - branch->currentIndex();
        branch->setCurrentIndex(other_index);
        QMetaObject::invokeMethod(branch, "activated", Qt::DirectConnection, Q_ARG(int, other_index));
        remove->trigger();
        require(!target->isChecked() && removed_branches == studio::ModelWeightSelection{{selected_model, selected_branch}},
                "Deletion used the displayed branch instead of the checked branch");
        model_row->setCheckState(0, Qt::Checked);
        remove->trigger();
        require(removed_branches.size() == 2 && model_clears == 2,
                "Checking an Inference row did not target only its branches");
        select_all->click();
        remove->trigger();
        require(model_clears == 3, "Inference Select all did not target the complete model list");
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
    for (const QString& mode : QStringList{"inherit", "transfer"}) {
        QEventLoop finished;
        execution.changed = [&] { finished.quit(); };
        studio::TrainingLaunchRequest request;
        request.mode = mode;
        request.model_id = stopped_model;
        execution.start_training(node, request);
        require(execution.is_running() && training_error.isEmpty(),
                "Stopped checkpoint could not start inheritance or transfer");
        execution.stop();
        finished.exec();
        const studio::ProjectModel* created = service.find_model(
            QString::fromStdString(service.task().runtime.training_model_id));
        require(created && created->parent_model_id == stopped_model.toStdString() &&
                    created->creation_mode == (mode == "inherit"
                        ? jocelin::model::ModelCreationMode::InheritTraining
                        : jocelin::model::ModelCreationMode::TransferTraining) && training_error.isEmpty(),
                "Stopped checkpoint lost its inheritance or transfer lineage");
    }
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
    QWidget* browser = workspace.findChild<QWidget*>("inferenceProjectModelBrowser");
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
