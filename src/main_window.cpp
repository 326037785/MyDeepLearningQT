#include "main_window.h"
#include "model_spec_catalog.h"

#include "new_project_dialog.h"
#include "node_registry.h"
#include "node_workspace.h"
#include "project_home.h"
#include "recent_project_store.h"
#include "save_project_as_dialog.h"
#include "select_task_module_dialog.h"
#include <checkpoint/checkpoint_catalog.h>
#include <common/model/model_weight_matcher.h>
#include "properties_editor.h"
#include "sample_presentation.h"
#include "task_workspace_service.h"
#include "task_workspace_pages.h"
#include "workflow_scene.h"
#include "ui_theme.h"
#include <runtime/device_service.h>
#include <runtime/inference_route.h>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QDir>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGraphicsView>
#include <QGridLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QSettings>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStatusBar>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <set>
#include <stdexcept>

namespace studio {

namespace {

void write_json_file(const QString& path, const QJsonObject& object) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

void append_log_file(const QString& path, const QString& message) {
    if (path.isEmpty()) return;
    QFile file(path);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) ||
        !file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    file.write(
        (QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) +
         "  " + message + "\n")
            .toUtf8());
}

bool select_compute_device(
    QWidget* parent,
    jocelin::runtime::ComputeDevice* selected)
{
    const auto devices =
        jocelin::runtime::available_compute_devices();
    if (devices.empty()) {
        QMessageBox::critical(
            parent,
            QObject::tr("No compute device"),
            QObject::tr("No supported CPU or GPU device is available."));
        return false;
    }
    std::vector<jocelin::runtime::ComputeDevice> gpu_devices;
    for (const auto& device : devices)
        if (device.device.type == jocelin::config::DeviceType::Gpu)
            gpu_devices.push_back(device);
    if (gpu_devices.empty()) {
        *selected = devices.front();
        return true;
    }
    bool accepted = false;
    const QString type_choice = QInputDialog::getItem(
        parent,
        QObject::tr("Compute device"),
        QObject::tr("Device type"),
        {QObject::tr("CPU"), QObject::tr("GPU")},
        1,
        false,
        &accepted);
    if (!accepted) return false;
    if (type_choice == QObject::tr("CPU")) {
        *selected = devices.front();
        return true;
    }
    QStringList labels;
    for (const auto& device : gpu_devices)
        labels.push_back(QString::fromStdString(device.display_name));
    const QString gpu_choice = QInputDialog::getItem(
        parent,
        QObject::tr("Compute device"),
        QObject::tr("GPU"),
        labels,
        0,
        false,
        &accepted);
    if (!accepted) return false;
    const int index = labels.indexOf(gpu_choice);
    if (index < 0) return false;
    *selected = gpu_devices[static_cast<size_t>(index)];
    return true;
}

class WorkflowView final : public QGraphicsView {
public:
    explicit WorkflowView(QGraphicsScene* scene, QWidget* parent = nullptr)
        : QGraphicsView(scene, parent) {
        setObjectName("workflowView");
        setDragMode(QGraphicsView::NoDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setRenderHint(QPainter::Antialiasing);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }

    std::function<void()> viewport_changed;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton) {
            panning_ = true;
            pan_anchor_ = event->pos();
            viewport()->setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QGraphicsView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (panning_) {
            const QPoint delta = event->pos() - pan_anchor_;
            pan_anchor_ = event->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            if (viewport_changed) viewport_changed();
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton && panning_) {
            panning_ = false;
            viewport()->unsetCursor();
            event->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override {
        const qreal current = transform().m11();
        const qreal requested = event->angleDelta().y() > 0 ? current * 1.15 : current / 1.15;
        const qreal bounded = std::clamp(requested, 0.25, 4.0);
        if (!qFuzzyCompare(current, bounded)) scale(bounded / current, bounded / current);
        if (viewport_changed) viewport_changed();
        event->accept();
    }

    void resizeEvent(QResizeEvent* event) override {
        QGraphicsView::resizeEvent(event);
        if (viewport_changed) viewport_changed();
    }

private:
    bool panning_ = false;
    QPoint pan_anchor_;
};

class WorkflowMinimap final : public QGraphicsView {
public:
    explicit WorkflowMinimap(QGraphicsView* source, QWidget* parent = nullptr)
        : QGraphicsView(source ? source->scene() : nullptr, parent), source_(source) {
        setObjectName("workflowMinimapView");
        setInteractive(false);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setRenderHint(QPainter::Antialiasing);
        setMinimumHeight(92);
        setMaximumHeight(128);
    }

    void refresh_overview() {
        QRectF bounds;
        if (scene()) {
            for (QGraphicsItem* item : scene()->items()) {
                if (auto* node = dynamic_cast<NodeItem*>(item))
                    bounds = bounds.isNull() ? node->sceneBoundingRect() : bounds.united(node->sceneBoundingRect());
            }
        }
        if (bounds.isEmpty()) bounds = QRectF(-300, -200, 600, 400);
        fitInView(bounds.adjusted(-50, -50, 50, 50), Qt::KeepAspectRatio);
        viewport()->update();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !source_) return;
        dragging_ = true;
        source_->centerOn(mapToScene(event->pos()));
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!dragging_ || !source_) return;
        source_->centerOn(mapToScene(event->pos()));
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !dragging_) return;
        dragging_ = false;
        viewport()->setCursor(Qt::PointingHandCursor);
        event->accept();
    }

    void resizeEvent(QResizeEvent* event) override {
        QGraphicsView::resizeEvent(event);
        refresh_overview();
    }

    void paintEvent(QPaintEvent* event) override {
        QGraphicsView::paintEvent(event);
        if (!source_ || !source_->scene()) return;
        const QRectF visible = source_->mapToScene(source_->viewport()->rect()).boundingRect();
        const QPolygon viewport_polygon = mapFromScene(visible);
        QPainter painter(viewport());
        painter.setBrush(QColor(14, 166, 122, 35));
        painter.setPen(QPen(QColor("#0EA67A"), 2));
        painter.drawPolygon(viewport_polygon);
    }

private:
    QGraphicsView* source_ = nullptr;
    bool dragging_ = false;
};

QString issue_text(const ValidationIssue& issue) {
    QString text;
    if (issue.node_id != 0) text += MainWindow::tr("Node %1: ").arg(issue.node_id);
    return text + QString::fromStdString(issue.message);
}

QRectF node_bounds(WorkflowScene* scene) {
    QRectF result;
    for (QGraphicsItem* item : scene->items()) {
        if (auto* node = dynamic_cast<NodeItem*>(item))
            result = result.isNull() ? node->sceneBoundingRect() : result.united(node->sceneBoundingRect());
    }
    return result;
}

bool arrange_selected_nodes(WorkflowScene* scene, const QString& command, QString* error) {
    QList<NodeItem*> nodes;
    for (QGraphicsItem* item : scene->selectedItems())
        if (auto* node = dynamic_cast<NodeItem*>(item)) nodes.append(node);
    const int required = command.startsWith("distribute") ? 3 : 2;
    if (nodes.size() < required) {
        if (error) *error = MainWindow::tr("Select at least %1 nodes for this command.").arg(required);
        return false;
    }
    if (command == "align-left") {
        const qreal x = (*std::min_element(nodes.begin(), nodes.end(), [](auto* a, auto* b) { return a->x() < b->x(); }))->x();
        for (auto* node : nodes) node->setX(x);
    } else if (command == "align-top") {
        const qreal y = (*std::min_element(nodes.begin(), nodes.end(), [](auto* a, auto* b) { return a->y() < b->y(); }))->y();
        for (auto* node : nodes) node->setY(y);
    } else if (command == "distribute-horizontal") {
        std::sort(nodes.begin(), nodes.end(), [](auto* a, auto* b) { return a->x() < b->x(); });
        const qreal interval = (nodes.back()->x() - nodes.front()->x()) / (nodes.size() - 1);
        for (int index = 1; index + 1 < nodes.size(); ++index) nodes[index]->setX(nodes.front()->x() + interval * index);
    } else if (command == "distribute-vertical") {
        std::sort(nodes.begin(), nodes.end(), [](auto* a, auto* b) { return a->y() < b->y(); });
        const qreal interval = (nodes.back()->y() - nodes.front()->y()) / (nodes.size() - 1);
        for (int index = 1; index + 1 < nodes.size(); ++index) nodes[index]->setY(nodes.front()->y() + interval * index);
    }
    scene->node_move_completed();
    return true;
}

void fit_graph_capped(QGraphicsView* view, const QRectF& bounds, qreal maximum_scale) {
    if (!view || bounds.isEmpty()) return;
    view->fitInView(bounds, Qt::KeepAspectRatio);
    if (view->transform().m11() <= maximum_scale) return;
    view->resetTransform();
    view->scale(maximum_scale, maximum_scale);
    view->centerOn(bounds.center());
}

void fit_graph_down_only(QGraphicsView* view, const QRectF& bounds) {
    if (!view || bounds.isEmpty()) return;
    const QTransform previous = view->transform();
    const qreal previous_scale = previous.m11();
    view->fitInView(bounds, Qt::KeepAspectRatio);
    if (view->transform().m11() <= previous_scale) return;
    view->setTransform(previous);
    view->centerOn(bounds.center());
}

QFont studio_ui_font() {
    const QStringList installed = QFontDatabase().families();
    for (const QString& preferred : {"Segoe UI Variable Text", "Segoe UI", "Microsoft YaHei UI", "Noto Sans"}) {
        if (!installed.contains(preferred, Qt::CaseInsensitive)) continue;
        QFont font(preferred);
        font.setPointSizeF(8.5);
        font.setStyleStrategy(QFont::PreferAntialias);
        return font;
    }
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSizeF(8.5);
    return font;
}

} // namespace

MainWindow::MainWindow() {
    QApplication::setFont(studio_ui_font());
    apply_ui_theme(*qApp);
    setFont(QApplication::font());
    buildUi();
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    resize(qMin(1440, available.width() - 32), qMin(900, available.height() - 32));
    setMinimumSize(760, 480);
    restoreShellState();
    bindActiveWorkflow();

    scene_->model_changed = [this] { markWorkflowChanged(); };
    scene_->position_changed = [this] {
        project_.mark_active_dirty();
        refreshProjectUi();
    };
    scene_->validation_refresh_requested = [this] { refreshValidation(); };
    scene_->node_activated = [this](int node_id) { openNodeWorkspace(node_id); };
    scene_->node_action_requested = [this](int node_id, const QString& action) {
        if (action == "train")
            startTraining(node_id, TrainingLaunchRequest{});
        else if (action == "infer") {
            openNodeWorkspace(node_id);
            node_workspace_->show_inference();
        }
    };
    scene_->feedback = [this](const QString& message) {
        log_->appendPlainText(message);
        statusBar()->showMessage(message, 5000);
    };
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this] { showProperties(); });
    training_job_.progress = [this](const jocelin::train::TrainingProgress& progress) {
        if (auto* model = project_.find_model(active_training_model_id_)) {
            model->completed_epochs = progress.completed_epochs;
            model->training_loss = progress.loss;
            model->metric_name = progress.validation_metric_name;
            model->metric = progress.validation_metric;
            project_.mark_models_dirty();
        }
        if (active_training_node_id_ != 0 &&
            graph().nodes().count(active_training_node_id_) != 0)
            node_workspace_->update_training_progress(active_training_node_id_, progress);
        statusBar()->showMessage(tr("Training epoch %1 / %2").arg(progress.completed_epochs).arg(progress.total_epochs));
    };
    training_job_.log = [this](const QString& message) {
        log_->appendPlainText("[train] " + message);
        append_log_file(active_training_log_path_, message);
        node_workspace_->append_training_log(active_training_node_id_, message);
    };
    training_job_.warning = [this](const QString& message) {
        auto* warning = new QMessageBox(QMessageBox::Warning, tr("Training device fallback"),
                                       message, QMessageBox::Ok, this);
        warning->setAttribute(Qt::WA_DeleteOnClose);
        warning->open();
    };
    training_job_.completed = [this](const jocelin::training::TrainingResult& result, const QString& error) {
        setEditingEnabled(true);
        auto* model = project_.find_model(active_training_model_id_);
        if (model) {
            model->completed_epochs = result.completed_epochs;
            model->best_epoch = result.best_epoch;
            model->metric_name = result.metric_name;
            model->metric = result.best_metric;
            if (!result.model_directory.empty())
                model->asset_directory = result.model_directory;

            const bool paused = error.isEmpty() &&
                result.end_state == jocelin::training::TrainingEndState::Paused;
            const bool completed = error.isEmpty() &&
                result.end_state == jocelin::training::TrainingEndState::Completed;
            const std::string weight_path = paused
                ? result.last_model_path
                : completed ? result.best_model_path : std::string{};
            if ((paused || completed) && !weight_path.empty() &&
                QFileInfo::exists(QString::fromStdString(weight_path))) {
                const auto created_at =
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
                if (auto* weight = jocelin::model::find_training_weight(*model)) {
                    weight->path = weight_path;
                    weight->created_at = created_at;
                    weight->supported_device_types =
                        jocelin::model::default_supported_devices(weight->format);
                    weight->runtime_name = "libtorch";
                    weight->artifact_version = "checkpoint-v1";
                } else {
                    jocelin::model::ModelWeight created_weight;
                    created_weight.id = "libtorch_fp32";
                    created_weight.format = jocelin::config::WeightType::LibTorch;
                    created_weight.precision = jocelin::model::WeightPrecision::Float32;
                    created_weight.role = jocelin::model::WeightRole::Training;
                    created_weight.path = weight_path;
                    created_weight.created_at = created_at;
                    created_weight.supported_device_types =
                        jocelin::model::default_supported_devices(created_weight.format);
                    created_weight.runtime_name = "libtorch";
                    created_weight.artifact_version = "checkpoint-v1";
                    model->weights.push_back(std::move(created_weight));
                }
            }
            model->training_status = !error.isEmpty()
                ? jocelin::model::ModelTrainingStatus::Failed
                : result.end_state == jocelin::training::TrainingEndState::Paused
                    ? jocelin::model::ModelTrainingStatus::Paused
                : result.end_state == jocelin::training::TrainingEndState::Stopped
                    ? jocelin::model::ModelTrainingStatus::Stopped
                    : jocelin::model::ModelTrainingStatus::Completed;
            if ((model->training_status == jocelin::model::ModelTrainingStatus::Paused ||
                 model->training_status == jocelin::model::ModelTrainingStatus::Completed) &&
                !jocelin::model::find_training_weight(*model))
                model->training_status = jocelin::model::ModelTrainingStatus::Failed;
            project_.mark_models_dirty();
        }
        node_workspace_->set_runtime_busy(false);
        if (active_training_node_id_ != 0 &&
            graph().nodes().count(active_training_node_id_) != 0) {
            node_workspace_->finish_training(
                active_training_node_id_, result, error);
        }
        if (!active_training_run_directory_.isEmpty()) {
            QJsonObject run{
                {"format", "deep_learning_training_run"},
                {"run_id", active_training_run_id_},
                {"run_number", active_training_run_number_},
                {"model_id", active_training_model_id_},
                {"source_mode", active_training_source_mode_},
                {"source_model_id", active_training_parent_model_id_},
                {"source_weight_id", active_training_source_weight_id_},
                {"completed_epochs", result.completed_epochs},
                {"best_epoch", result.best_epoch},
                {"best_metric", result.best_metric},
                {"metric_name", QString::fromStdString(result.metric_name)},
                {"best_model", QString::fromStdString(result.best_model_path)},
                {"last_model", QString::fromStdString(result.last_model_path)},
                {"status", !error.isEmpty() ? "error" :
                    result.end_state == jocelin::training::TrainingEndState::Paused
                        ? "paused" :
                    result.end_state == jocelin::training::TrainingEndState::Stopped
                        ? "stopped" : "completed"},
                {"error", error},
                {"started_at", active_training_started_at_},
                {"completed_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
            if (active_training_node_id_ != 0 &&
                graph().nodes().count(active_training_node_id_) != 0) {
                TaskWorkspaceService run_service(
                    graph(), active_training_node_id_);
                const auto& task = run_service.task();
                run["task"] = QString::fromStdString(task.task);
                run["capacity"] = QString::fromStdString(task.model_scale);
                run["model_index"] = task.model_index;
                run["requested_epochs"] = task.training.epochs;
            }
            write_json_file(
                QDir(active_training_run_directory_).filePath("run.json"), run);
        }
        if (error.isEmpty()) {
            const QString completion =
                result.end_state == jocelin::training::TrainingEndState::Paused
                    ? tr("Training paused") :
                result.end_state == jocelin::training::TrainingEndState::Stopped
                    ? tr("Training stopped")
                    : tr("Training completed");
            log_->appendPlainText(completion);
            append_log_file(active_training_log_path_, completion);
        }
        else {
            log_->appendPlainText(tr("Training failed: %1").arg(error));
            append_log_file(
                active_training_log_path_,
                tr("Training failed: %1").arg(error));
            setRuntimeStatus(tr("Error"), "error");
        }
        try {
            if (project_.has_storage()) project_.save_project();
        } catch (const std::exception& catalog_error) {
            const QString message =
                tr("ModelList could not be persisted: %1")
                    .arg(QString::fromUtf8(catalog_error.what()));
            log_->appendPlainText(message);
            append_log_file(active_training_log_path_, message);
            output_dock_->show();
            output_tabs_->setCurrentWidget(log_);
        }
        node_workspace_->refresh_models();
        if (!error.isEmpty()) { output_dock_->show(); output_tabs_->setCurrentWidget(log_); }
        active_training_node_id_ = 0;
        active_training_model_id_.clear();
        active_training_source_mode_.clear();
        active_training_parent_model_id_.clear();
        active_training_source_weight_id_.clear();
        active_training_run_id_.clear();
        active_training_run_number_ = 0;
        active_training_run_directory_.clear();
        active_training_started_at_.clear();
        active_training_log_path_.clear();
        refreshValidation();
        refreshProjectUi();
    };
    inference_job_.progress = [this](size_t done, size_t total, const QString& image) {
        node_workspace_->update_inference_progress(
            active_inference_node_id_, done, total, image);
        QString display_name = tr("Task image");
        if (active_inference_node_id_ != 0 &&
            graph().nodes().count(active_inference_node_id_) != 0) {
            TaskWorkspaceService service(graph(), active_inference_node_id_);
            int sample_index = -1;
            int image_index = -1;
            const auto* sample = task_sample_for_path(
                service.binding(), image, &sample_index, &image_index);
            if (sample) {
                display_name = image_display_name(sample_index, *sample);
                if (sample->image_paths.size() > 1)
                    display_name = tr("%1 路 %2/%3")
                        .arg(display_name)
                        .arg(image_index + 1)
                        .arg(sample->image_paths.size());
            }
        }
        statusBar()->showMessage(tr("Inference %1 / %2: %3")
            .arg(done)
            .arg(total)
            .arg(display_name));
    };
    inference_job_.completed = [this](const InferenceOutcome& outcome) {
        setEditingEnabled(true);
        node_workspace_->set_runtime_busy(false);
        if (outcome.fallback.occurred) {
            const QString message = tr(
                "Inference route fallback: requested %1 on %2, effective %3 on %4. %5")
                .arg(
                    QString::fromStdString(jocelin::config::to_string(outcome.fallback.requested_weight_type)),
                    QString::fromStdString(outcome.fallback.requested_device.id),
                    QString::fromStdString(jocelin::config::to_string(outcome.fallback.effective_weight_type)),
                    QString::fromStdString(outcome.fallback.effective_device.id),
                    QString::fromStdString(outcome.fallback.reason));
            log_->appendPlainText(message);
            output_tabs_->setCurrentWidget(log_);
            output_dock_->show();
            auto* warning = new QMessageBox(QMessageBox::Warning, tr("Inference device fallback"),
                                           message, QMessageBox::Ok, this);
            warning->setAttribute(Qt::WA_DeleteOnClose);
            warning->open();
        }
        if (active_inference_node_id_ != 0 &&
            graph().nodes().count(active_inference_node_id_) != 0) {
            TaskWorkspaceService service(graph(), active_inference_node_id_);
            service.record_inference_result(
                outcome.report,
                active_inference_model_id_.toStdString(),
                active_inference_weight_id_.toStdString(),
                active_inference_weight_format_,
                active_inference_output_.toStdString(),
                outcome.error.toStdString());
            node_workspace_->finish_inference(
                active_inference_node_id_, outcome.error);
            project_.mark_active_dirty();
        }
        if (outcome.error.isEmpty()) {
            if (!active_inference_output_.isEmpty()) {
                const auto metrics_json = [](bool available, double precision, double recall, double f1) {
                    return QJsonObject{
                        {"available", available},
                        {"precision", precision},
                        {"recall", recall},
                        {"f1", f1}};
                };
                QJsonArray samples;
                QJsonObject task_metrics = metrics_json(false, 0.0, 0.0, 0.0);
                if (const auto* cls = std::get_if<jocelin::result::cls::ClsInferenceResult>(&outcome.report)) {
                    task_metrics = metrics_json(
                        cls->metrics.available, cls->metrics.precision,
                        cls->metrics.recall, cls->metrics.f1);
                    if (cls->metrics.available) task_metrics.insert("accuracy", cls->metrics.accuracy);
                    for (const auto& record : cls->samples) {
                        QJsonArray probabilities;
                        for (double value : record.probabilities) probabilities.append(value);
                        samples.append(QJsonObject{
                            {"image_path", QString::fromStdString(record.image_path)},
                            {"result_name", QString::fromStdString(record.result_name)},
                            {"predicted_class", record.predicted_class},
                            {"confidence", record.confidence},
                            {"confidence_threshold", record.confidence_threshold},
                            {"confidence_pass", record.confidence_pass},
                            {"interest", record.interest},
                            {"probabilities", probabilities},
                            {"heatmap_path", QString::fromStdString(record.heatmap_path)},
                            {"metadata_path", QString::fromStdString(record.metadata_path)}});
                    }
                } else {
                    const auto& sg = std::get<jocelin::result::sg::SgInferenceResult>(outcome.report);
                    for (const auto& record : sg.samples) {
                        auto sample_metrics = metrics_json(
                            record.metrics.available, record.metrics.precision,
                            record.metrics.recall, record.metrics.f1);
                        if (record.metrics.available) sample_metrics.insert("mean_iou", record.metrics.mean_iou);
                        samples.append(QJsonObject{
                            {"image_path", QString::fromStdString(record.image_path)},
                            {"result_name", QString::fromStdString(record.result_name)},
                            {"mean_confidence", record.mean_confidence},
                            {"predicted_area", static_cast<double>(record.predicted_area)},
                            {"metrics", sample_metrics},
                            {"mask_path", QString::fromStdString(record.mask_path)},
                            {"visualization_path", QString::fromStdString(record.visualization_path)},
                            {"metadata_path", QString::fromStdString(record.metadata_path)}});
                    }
                }
                write_json_file(QDir(active_inference_output_).filePath("result.json"), {
                    {"format", "deep_learning_inference_result"},
                    {"model_id", active_inference_model_id_},
                    {"weight_id", active_inference_weight_id_},
                    {"weight_format", QString::fromStdString(
                        jocelin::config::to_string(active_inference_weight_format_))},
                    {"image_count", active_inference_image_count_},
                    {"metrics", task_metrics},
                    {"samples", samples},
                    {"completed_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                    {"status", "completed"}
                });
            }
            result_->setPlainText(outcome.summary); output_tabs_->setCurrentWidget(result_);
            statusBar()->showMessage(tr("Inference completed"));
        }
        else {
            log_->appendPlainText(tr("Inference failed: %1").arg(outcome.error));
            setRuntimeStatus(tr("Error"), "error");
            output_tabs_->setCurrentWidget(log_);
            output_dock_->show();
        }
        active_inference_node_id_ = 0;
        active_inference_model_id_.clear();
        active_inference_weight_id_.clear();
        active_inference_output_.clear();
        active_inference_image_count_ = 0;
        refreshValidation();
        refreshProjectUi();
    };

    updateRecentProjects();
    refreshValidation();
    showProjectHome();
}

MainWindow::~MainWindow() {
    if (scene_) scene_->set_model(nullptr, nullptr);
    if (node_workspace_) node_workspace_->clear_node();
}

void MainWindow::buildUi() {
    toolbar_ = addToolBar(tr("Project and workflow"));
    toolbar_->setObjectName("workflowToolbar");
    toolbar_->setMovable(false);
    toolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);

    QFontMetrics fm(toolbar_->font());
    int toolbarHeight = fm.height() + 8;
    toolbarHeight = std::clamp(toolbarHeight, 20, 30);

    toolbar_->setMinimumHeight(toolbarHeight);
    toolbar_->setMaximumHeight(30);


    auto make_action = [this](const QString& text, const char* name, auto callback) {
        auto* action = toolbar_->addAction(text);
        action->setObjectName(name);

        connect(action, &QAction::triggered, this, callback);
        return action;
        };
    editing_actions_.push_back(make_action(tr("Home"), "projectHomeAction", [this] { showProjectHome(); }));
    toolbar_->addSeparator();
    auto* new_project_action = make_action(tr("New"), "newProjectAction", [this] { newProject(); });
    new_project_action->setShortcut(QKeySequence::New);
    editing_actions_.push_back(new_project_action);
    auto* open_project_action = make_action(tr("Open"), "openProjectAction", [this] { openProject(); });
    open_project_action->setShortcut(QKeySequence::Open);
    editing_actions_.push_back(open_project_action);
    save_project_action_ = make_action(tr("Save"), "saveProjectAction", [this] { saveProject(); });
    save_project_action_->setShortcut(QKeySequence::Save);
    editing_actions_.push_back(save_project_action_);
    toolbar_->addSeparator();
    editing_actions_.push_back(make_action(tr("Clear"), "clearWorkflowAction", [this] { clearWorkflow(); }));
    auto* delete_action = make_action(tr("Delete"), "deleteAction", [this] { deleteSelected(); });
    editing_actions_.push_back(delete_action);
    editing_actions_.push_back(make_action(tr("Fit"), "fitAction", [this] {
        const QRectF bounds = node_bounds(scene_);
        fit_graph_capped(view_, bounds.isEmpty() ? QRectF(-300, -200, 600, 400)
                                                 : bounds.adjusted(-80, -80, 80, 80), 1.0);
    }));
    toolbar_->addSeparator();
    run_action_ = make_action(tr("Open Task"), "openTaskAction", [this] { runWorkflow(); });
    if (auto* button = qobject_cast<QToolButton*>(toolbar_->widgetForAction(run_action_)))
        button->setProperty("runAction", true);
    if (auto* button = qobject_cast<QToolButton*>(toolbar_->widgetForAction(delete_action)))
        button->setProperty("destructive", true);

    auto* project_menu = menuBar()->addMenu(tr("Project"));
    project_menu->addAction(new_project_action);
    project_menu->addAction(open_project_action);
    recent_menu_ = project_menu->addMenu(tr("Recent Projects"));
    project_menu->addSeparator();
    project_menu->addAction(save_project_action_);
    save_project_as_action_ = project_menu->addAction(tr("Save As..."));
    save_project_as_action_->setObjectName("saveProjectAsAction");
    save_project_as_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(save_project_as_action_, &QAction::triggered, this, [this] { saveProjectAs(); });
    editing_actions_.push_back(save_project_as_action_);
    project_menu->addSeparator();
    auto* properties_action = project_menu->addAction(tr("Project Properties..."));
    properties_action->setObjectName("projectPropertiesAction");
    connect(properties_action, &QAction::triggered, this, [this] {
        showWorkflowWorkspace();
        project_dock_->show();
        project_dock_->raise();
        project_name_->setFocus();
    });
    auto* close_project_action = project_menu->addAction(tr("Close Project"));
    close_project_action->setObjectName("closeProjectAction");
    connect(close_project_action, &QAction::triggered, this, [this] {
        if (!confirmProjectTransition()) return;
        unbindWorkflowViews();
        project_.new_project();
        bindActiveWorkflow();
        showProjectHome();
    });
    auto* view_menu = menuBar()->addMenu(tr("View"));

    scene_ = new WorkflowScene(this);
    view_ = new WorkflowView(scene_, this);
    project_home_ = new ProjectHome(this);
    workflow_workspace_ = new QWidget(this);
    auto* workspace_layout = new QVBoxLayout(workflow_workspace_);
    workspace_layout->setContentsMargins(0, 0, 0, 0);
    workspace_layout->setSpacing(0);
    auto* workflow_header = new QFrame(workflow_workspace_);
    workflow_header->setObjectName("workflowContextHeader");
    auto* workflow_header_layout = new QVBoxLayout(workflow_header);
    workflow_header_layout->setContentsMargins(12, 5, 12, 0);
    workflow_header_layout->setSpacing(4);
    auto* workflow_top_row = new QHBoxLayout;
    workflow_top_row->setSpacing(10);
    auto* workflow_identity = new QVBoxLayout;
    workflow_identity->setSpacing(2);
    workflow_context_title_ = new QLabel(workflow_header);
    workflow_context_title_->setProperty("workflowContextTitle", true);
    workflow_context_hint_ = new QLabel(workflow_header);
    workflow_context_hint_->setProperty("muted", true);
    workflow_identity->addWidget(workflow_context_title_);
    workflow_identity->addWidget(workflow_context_hint_);
    workflow_top_row->addLayout(workflow_identity, 1);
    auto* library_hint = new QLabel(tr("Add nodes  >  Connect  >  Configure"), workflow_header);
    library_hint->setProperty("workflowSteps", true);
    workflow_top_row->addWidget(library_hint);
    workflow_header_layout->addLayout(workflow_top_row);
    auto* tabs_row = new QHBoxLayout;
    tabs_row->setContentsMargins(0, 0, 0, 0);
    tabs_row->setSpacing(4);
    workflow_tabs_ = new QTabBar(workflow_header);
    workflow_tabs_->setObjectName("workflowTabs");
    workflow_tabs_->setTabsClosable(false);
    workflow_tabs_->setExpanding(false);
    workflow_tabs_->setElideMode(Qt::ElideRight);
    auto* add_tab = new QPushButton(tr("+"), workflow_header);
    add_tab->setObjectName("addWorkflowTabButton");
    add_tab->setToolTip(tr("Add workflow"));
    add_tab->setMaximumWidth(34);
    tabs_row->addWidget(workflow_tabs_);
    tabs_row->addWidget(add_tab);
    tabs_row->addStretch();
    workflow_header_layout->addLayout(tabs_row);
    workspace_layout->addWidget(workflow_header);
    auto* canvas_host = new QFrame(workflow_workspace_);
    canvas_host->setObjectName("workflowCanvasHost");
    auto* canvas_layout = new QGridLayout(canvas_host);
    canvas_layout->setContentsMargins(0, 0, 0, 0);
    canvas_layout->addWidget(view_, 0, 0);
    auto* view_tools = new QFrame(canvas_host);
    view_tools->setObjectName("canvasViewTools");
    auto* view_tools_layout = new QHBoxLayout(view_tools);
    view_tools_layout->setContentsMargins(4, 4, 4, 4);
    view_tools_layout->setSpacing(4);
    auto* grid_button = new QPushButton(tr("Grid"), view_tools);
    grid_button->setObjectName("workflowGridButton");
    grid_button->setCheckable(true);
    const bool grid_visible = QSettings("Jocelin", "DeepLearningStudio").value("workflow/gridVisible", true).toBool();
    grid_button->setChecked(grid_visible);
    grid_button->setToolTip(tr("Show the workflow grid and snap moved nodes to it."));
    scene_->set_grid_visible(grid_visible);
    auto* align_button = new QToolButton(view_tools);
    align_button->setObjectName("workflowAlignButton");
    align_button->setText(tr("Arrange"));
    align_button->setToolTip(tr("Align or distribute the selected workflow nodes."));
    align_button->setPopupMode(QToolButton::InstantPopup);
    auto* arrange_menu = new QMenu(align_button);
    const QList<QPair<QString, QString>> arrange_commands = {
        {tr("Align left"), "align-left"}, {tr("Align top"), "align-top"},
        {tr("Distribute horizontally"), "distribute-horizontal"},
        {tr("Distribute vertically"), "distribute-vertical"}};
    for (const auto& item : arrange_commands) {
        auto* action = arrange_menu->addAction(item.first);
        connect(action, &QAction::triggered, this, [this, command = item.second] {
            QString error;
            if (!arrange_selected_nodes(scene_, command, &error)) statusBar()->showMessage(error, 3500);
            else statusBar()->showMessage(tr("Selected nodes arranged."), 2500);
        });
    }
    align_button->setMenu(arrange_menu);
    auto* fit_button = new QPushButton(tr("Fit"), view_tools);
    fit_button->setToolTip(tr("Fit the complete workflow into view"));
    view_tools_layout->addWidget(grid_button);
    view_tools_layout->addWidget(align_button);
    view_tools_layout->addWidget(fit_button);
    canvas_layout->addWidget(view_tools, 0, 0, Qt::AlignTop | Qt::AlignRight);
    canvas_empty_state_ = new QFrame(canvas_host);
    canvas_empty_state_->setObjectName("canvasEmptyState");
    canvas_empty_state_->setMinimumWidth(360);
    auto* empty_layout = new QVBoxLayout(canvas_empty_state_);
    empty_layout->setContentsMargins(28, 24, 28, 24);
    empty_layout->setSpacing(8);
    auto* empty_icon = new QLabel(tr("[ DATA ]  ->  [ TASK ]  ->  [ RESULT ]"), canvas_empty_state_);
    empty_icon->setAlignment(Qt::AlignCenter);
    empty_icon->setProperty("muted", true);
    auto* empty_title = new QLabel(tr("Your workflow is empty"), canvas_empty_state_);
    empty_title->setAlignment(Qt::AlignCenter);
    empty_title->setProperty("sectionTitle", true);
    auto* empty_hint = new QLabel(tr("Drag nodes from the library, or load an example workflow to get started."), canvas_empty_state_);
    empty_hint->setAlignment(Qt::AlignCenter);
    empty_hint->setWordWrap(true);
    empty_hint->setProperty("muted", true);
    auto* empty_action = new QPushButton(tr("Load Example Workflow"), canvas_empty_state_);
    empty_action->setProperty("accentSecondary", true);
    empty_layout->addWidget(empty_icon);
    empty_layout->addWidget(empty_title);
    empty_layout->addWidget(empty_hint);
    empty_layout->addWidget(empty_action, 0, Qt::AlignHCenter);
    canvas_layout->addWidget(canvas_empty_state_, 0, 0, Qt::AlignCenter);
    workspace_layout->addWidget(canvas_host, 1);
    connect(add_tab, &QPushButton::clicked, this, [this] { addWorkflow(); });
    connect(grid_button, &QPushButton::toggled, this, [this](bool visible) {
        scene_->set_grid_visible(visible);
        QSettings("Jocelin", "DeepLearningStudio").setValue("workflow/gridVisible", visible);
        statusBar()->showMessage(visible ? tr("Workflow grid and snapping enabled") : tr("Workflow grid and snapping disabled"), 2500);
    });
    connect(fit_button, &QPushButton::clicked, this, [this] {
        const QRectF bounds = node_bounds(scene_);
        fit_graph_capped(view_, bounds.isEmpty() ? QRectF(-300, -200, 600, 400)
                                                 : bounds.adjusted(-80, -80, 80, 80), 1.0);
    });
    connect(empty_action, &QPushButton::clicked, this, [this] { loadExampleWorkflow(); });
    connect(workflow_tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index < 0 || static_cast<size_t>(index) == project_.active_index()) return;
        unbindWorkflowViews();
        try {
            project_.set_active_index(static_cast<size_t>(index));
            bindActiveWorkflow();
        } catch (const std::exception& error) {
            bindActiveWorkflow();
            QMessageBox::warning(this, tr("Cannot switch workflow"), error.what());
        }
    });
    connect(workflow_tabs_, &QTabBar::tabCloseRequested, this, [this](int index) {
        if (index < 0) return;
        if (static_cast<size_t>(index) != project_.active_index()) {
            unbindWorkflowViews();
            project_.set_active_index(static_cast<size_t>(index));
            bindActiveWorkflow();
        }
        removeWorkflow();
    });
    connect(workflow_tabs_, &QTabBar::tabBarDoubleClicked, this, [this](int index) {
        if (index < 0) return;
        if (static_cast<size_t>(index) != project_.active_index()) {
            unbindWorkflowViews();
            project_.set_active_index(static_cast<size_t>(index));
            bindActiveWorkflow();
        }
        renameWorkflow();
    });
    node_workspace_ = new NodeWorkspace(this);
    workspace_stack_ = new QStackedWidget(this);
    workspace_stack_->addWidget(project_home_);
    workspace_stack_->addWidget(workflow_workspace_);
    workspace_stack_->addWidget(node_workspace_);
    setCentralWidget(workspace_stack_);
    project_home_->create_project = [this] { newProject(); };
    project_home_->quick_start = [this] { newProjectFromTemplate("sg"); };
    project_home_->create_template = [this](const QString& task) { newProjectFromTemplate(task); };
    project_home_->open_project = [this] { openProject(); };
    project_home_->open_recent = [this](const QString& path) { openProject(path); };
    project_home_->delete_project = [this](const QString& path) {
        const QFileInfo input(path);
        const QString project_file = input.isDir()
            ? QDir(input.absoluteFilePath()).filePath("project.json")
            : input.absoluteFilePath();
        const QFileInfo manifest(project_file);
        if (!manifest.isFile()) {
            QMessageBox::warning(this, tr("Cannot delete project"), tr("Project file is missing:\n%1").arg(project_file));
            RecentProjectStore().remove(path);
            project_home_->refresh_recent_projects();
            return;
        }
        const QString root = manifest.absolutePath();
        if (QDir(root).isRoot()) {
            QMessageBox::warning(this, tr("Cannot delete project"), tr("Refusing to delete a filesystem root."));
            return;
        }
        if (QMessageBox::question(
                this, tr("Delete project"),
                tr("Permanently delete this Project and all files under:\n\n%1\n\nThis cannot be recovered.").arg(root),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        const bool deleting_current = project_.has_storage() &&
            QFileInfo(project_.project_file()).absoluteFilePath().compare(project_file, Qt::CaseInsensitive) == 0;
        if (!QDir(root).removeRecursively()) {
            QMessageBox::warning(this, tr("Cannot delete project"), tr("Cannot delete Project directory:\n%1").arg(root));
            return;
        }
        RecentProjectStore().remove(project_file);
        if (deleting_current) {
            unbindWorkflowViews();
            project_.new_project();
            bindActiveWorkflow();
        }
        project_home_->refresh_recent_projects();
        showProjectHome();
    };
    node_workspace_->back_requested = [this] { showWorkflowWorkspace(); };
    node_workspace_->changed = [this] { markWorkflowChanged(); };
    node_workspace_->training_launch_requested =
        [this](int id, const TrainingLaunchRequest& launch) {
            startTraining(id, launch);
        };
    node_workspace_->import_model_requested = [this](int task_node_id) {
        if (!project_.has_storage()) {
            QMessageBox::information(
                this, tr("Save Project first"),
                tr("Save the Project before importing model assets."));
            return;
        }
        const QString manifest_path = QFileDialog::getOpenFileName(
            this, tr("Import model weights"), {},
            tr("Model weights or manifest (*.pt *.pth *.bin *.ckpt *.json);;All files (*)"));
        if (manifest_path.isEmpty()) return;
        QString imported_asset_directory;
        bool import_added = false;
        try {
            const bool raw_weights = QFileInfo(manifest_path).suffix().compare("json", Qt::CaseInsensitive) != 0;
            QJsonObject object;
            if (!raw_weights) {
                QFile manifest_file(manifest_path);
                if (!manifest_file.open(QIODevice::ReadOnly))
                    throw std::runtime_error("cannot read model manifest");
                const auto document = QJsonDocument::fromJson(manifest_file.readAll());
                if (!document.isObject())
                    throw std::runtime_error("model manifest must contain a JSON object");
                object = document.object();
                if (object.value("format").toString() != "deep_learning_project_model")
                    throw std::runtime_error("unsupported model manifest");
            }
            const QString source_directory = QFileInfo(manifest_path).absolutePath();
            struct ImportWeight {
                std::string id;
                jocelin::config::WeightType format;
                jocelin::model::WeightPrecision precision;
                jocelin::model::WeightRole role;
                QString source_path;
                std::string parent_weight_id;
                std::string created_at;
                std::vector<jocelin::config::DeviceType> supported_device_types;
                std::string runtime_name;
                std::string runtime_version;
                std::string artifact_version;
                int onnx_opset = 0;
                std::string input_signature;
                std::string output_signature;
                std::string target_architecture;
            };
            std::vector<ImportWeight> import_weights;
            if (raw_weights) {
                import_weights.push_back({
                    "libtorch_fp32", jocelin::config::WeightType::LibTorch,
                    jocelin::model::WeightPrecision::Float32,
                    jocelin::model::WeightRole::Training, manifest_path, {}, {}});
            } else if (object.value("weights").isArray()) {
                for (const auto& entry : object.value("weights").toArray()) {
                    if (!entry.isObject()) continue;
                    const auto weight = entry.toObject();
                    const QString file = weight.value("file").toString();
                    if (file.isEmpty() || QFileInfo(file).isAbsolute())
                        throw std::runtime_error("model weight file must be relative");
                    const QString source = QDir(source_directory).absoluteFilePath(file);
                    if (!QFileInfo(source).isFile())
                        throw std::runtime_error("model weight file is missing");
                    const auto format = jocelin::config::weight_type_from_string(
                        weight.value("format").toString().toStdString());
                    const auto role = weight.contains("role")
                        ? jocelin::model::weight_role_from_string(
                              weight.value("role").toString().toStdString())
                        : (format == jocelin::config::WeightType::LibTorch
                               ? jocelin::model::WeightRole::Training
                               : jocelin::model::WeightRole::Inference);
                    ImportWeight imported{
                        weight.value("id").toString().toStdString(),
                        format,
                        jocelin::model::weight_precision_from_string(
                            weight.value("precision").toString().toStdString()),
                        role,
                        source,
                        weight.value("parent_weight_id").toString().toStdString(),
                        weight.value("created_at").toString().toStdString()};
                    for (const auto device : weight.value("supported_devices").toArray())
                        imported.supported_device_types.push_back(
                            jocelin::config::device_type_from_string(device.toString().toStdString()));
                    imported.runtime_name = weight.value("runtime_name").toString().toStdString();
                    imported.runtime_version = weight.value("runtime_version").toString().toStdString();
                    imported.artifact_version = weight.value("artifact_version").toString().toStdString();
                    imported.onnx_opset = weight.value("onnx_opset").toInt();
                    imported.input_signature = weight.value("input_signature").toString().toStdString();
                    imported.output_signature = weight.value("output_signature").toString().toStdString();
                    imported.target_architecture = weight.value("target_architecture").toString().toStdString();
                    import_weights.push_back(std::move(imported));
                }
            } else {
                const QString checkpoint_name = object.value("checkpoint").toString();
                if (checkpoint_name.isEmpty() || QFileInfo(checkpoint_name).isAbsolute())
                    throw std::runtime_error("model manifest checkpoint must be a relative path");
                const QString source = QDir(source_directory).absoluteFilePath(checkpoint_name);
                if (!QFileInfo(source).isFile())
                    throw std::runtime_error("model checkpoint is missing");
                import_weights.push_back({
                    "libtorch_fp32", jocelin::config::WeightType::LibTorch,
                    jocelin::model::WeightPrecision::Float32,
                    jocelin::model::WeightRole::Training, source, {}, {}});
            }
            if (import_weights.empty())
                throw std::runtime_error("model manifest contains no weights");
            const auto libtorch = std::find_if(
                import_weights.begin(), import_weights.end(),
                [](const auto& weight) {
                    return weight.format == jocelin::config::WeightType::LibTorch;
                });
            if (libtorch == import_weights.end())
                throw std::runtime_error("import currently requires a LibTorch weight branch");
            TaskWorkspaceService task_service(graph(), task_node_id);
            const auto& task = task_service.task();
            jocelin::config::AiConfig config;
            config.tool_type = jocelin::config::tool_type_from_string(task.task);
            config.model_index = task.model_index;
            config.model_scale = jocelin::config::model_scale_from_string(task.model_scale);
            config.image_width = task.training.image_width;
            config.image_height = task.training.image_height;
            config.normalize = task_service.binding().preprocessing.normalize;
            std::vector<jocelin::model::ModelClass> classes;
            for (const auto& entry : object.value("classes").toArray()) {
                const auto value = entry.toObject();
                classes.push_back({value.value("id").toString().toStdString(),
                    value.value("name").toString().toStdString(), value.value("mask_value").toInt()});
            }
            if (classes.empty())
                for (const auto& value : task_service.binding().classes)
                    classes.push_back({value.id, value.name, value.mask_value});
            config.num_classes = 1;
            for (const auto& value : classes)
                config.num_classes = std::max(config.num_classes,
                    value.mask_value + (config.tool_type == jocelin::config::ToolType::Sg ? 1 : 0));
            std::optional<jocelin::checkpoint::CheckpointMetadata> metadata;
            try {
                metadata = jocelin::checkpoint::inspect_checkpoint(libtorch->source_path.toStdString());
            } catch (const std::exception&) {
                // External state dictionaries need not carry project metadata.
            }
            const bool manifest_spec = object.contains("task") &&
                object.contains("model_index") && object.contains("model_scale");
            if (metadata) {
                config.tool_type = metadata->tool_type;
                config.model_index = metadata->model_index;
                config.model_scale = metadata->model_scale;
                config.image_width = metadata->image_width;
                config.image_height = metadata->image_height;
                config.num_classes = metadata->num_classes;
                config.model_embed_dim = metadata->model_embed_dim;
                config.model_depth = metadata->model_depth;
                config.model_num_heads = metadata->model_num_heads;
                config.normalize = metadata->normalize;
                std::copy(metadata->mean.begin(), metadata->mean.end(), config.mean);
                std::copy(metadata->std.begin(), metadata->std.end(), config.std);
            } else if (manifest_spec) {
                config.tool_type = jocelin::config::tool_type_from_string(object.value("task").toString().toStdString());
                config.model_index = object.value("model_index").toInt();
                config.model_scale = jocelin::config::model_scale_from_string(object.value("model_scale").toString().toStdString());
                config.image_width = object.value("image_width").toInt(config.image_width);
                config.image_height = object.value("image_height").toInt(config.image_height);
            } else {
                QDialog dialog(this);
                dialog.setObjectName("importModelSpecDialog");
                dialog.setWindowTitle(tr("Choose target model"));
                auto* layout = new QFormLayout(&dialog);
                layout->addRow(tr("Task"), new QLabel(QString::fromStdString(task.task), &dialog));
                auto* spec = new QComboBox(&dialog);
                spec->setObjectName("importModelSpec");
                for (const auto& option : model_spec_options) {
                    if (task.task != option.task) continue;
                    spec->addItem(tr("Capacity %1 / Index %2").arg(option.scale).arg(option.index), option.index);
                    spec->setItemData(spec->count() - 1, option.scale, Qt::UserRole + 1);
                    if (task.model_index == option.index && task.model_scale == option.scale)
                        spec->setCurrentIndex(spec->count() - 1);
                }
                auto* width = new QSpinBox(&dialog);
                auto* height = new QSpinBox(&dialog);
                auto* count = new QSpinBox(&dialog);
                width->setObjectName("importImageWidth");
                height->setObjectName("importImageHeight");
                count->setObjectName("importClassCount");
                width->setRange(1, task.task == "cls" ? 1024 : 65536);
                height->setRange(1, task.task == "cls" ? 1024 : 65536);
                count->setRange(1, 65536);
                width->setValue(config.image_width);
                height->setValue(config.image_height);
                count->setValue(std::max(2, config.num_classes));
                layout->addRow(tr("Target model"), spec);
                layout->addRow(tr("Input width"), width);
                layout->addRow(tr("Input height"), height);
                layout->addRow(task.task == "sg" ? tr("Output classes (including background)") : tr("Output classes"), count);
                auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
                layout->addRow(buttons);
                connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
                connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                if (dialog.exec() != QDialog::Accepted) return;
                config.model_index = spec->currentData().toInt();
                config.model_scale = jocelin::config::model_scale_from_string(spec->currentData(Qt::UserRole + 1).toString().toStdString());
                config.image_width = width->value();
                config.image_height = height->value();
                config.num_classes = count->value();
            }
            const auto imported_task = jocelin::config::to_string(config.tool_type);
            if (imported_task != task.task)
                throw std::runtime_error("imported model belongs to a different task repository");
            if (!supports_model_spec(imported_task, config.model_index, jocelin::config::to_string(config.model_scale)))
                throw std::runtime_error("imported model spec is not registered for this task");
            const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmsszzz");
            const QString asset_directory = QDir(project_.metadata().root_directory)
                .filePath("models/imported/" + stamp);
            if (!QDir().mkpath(asset_directory))
                throw std::runtime_error("cannot create imported model directory");

            imported_asset_directory = asset_directory;
            ProjectModel model;
            model.training_run_id = ("import-" + stamp).toStdString();
            model.created_at = object.value("created_at").toString().toStdString();
            if (model.created_at.empty())
                model.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
            model.task = config.tool_type;
            model.model_index = config.model_index;
            model.model_scale = config.model_scale;
            model.image_width = config.image_width;
            model.image_height = config.image_height;
            model.asset_directory = asset_directory.toStdString();
            model.metric_name = object.value("metric_name").toString().toStdString();
            model.metric = object.value("metric").toDouble(0.0);
            model.creation_mode = jocelin::model::ModelCreationMode::Import;
            model.training_status = jocelin::model::ModelTrainingStatus::Imported;
            const int last_class = config.tool_type == jocelin::config::ToolType::Cls
                ? config.num_classes : config.num_classes - 1;
            for (int label = 1; label <= last_class; ++label) {
                const auto found = std::find_if(classes.begin(), classes.end(),
                    [label](const auto& value) { return value.mask_value == label; });
                model.classes.push_back(found != classes.end() ? *found : jocelin::model::ModelClass{
                    "imported_" + std::to_string(label), "Class " + std::to_string(label), label});
            }
            for (const auto& weight : import_weights) {
                const QFileInfo source_info(weight.source_path);
                const bool native = weight.format == jocelin::config::WeightType::LibTorch;
                const QString filename = QString::fromStdString(weight.id) +
                    (native ? ".pt" : (source_info.suffix().isEmpty() ? QString{} : "." + source_info.suffix()));
                const QString destination = QDir(asset_directory).filePath(filename);
                if (native) {
                    const auto report = jocelin::model::import_model_weights(
                        config, weight.source_path.toStdString(), destination.toStdString());
                    QFile report_file(QDir(asset_directory).filePath(QString::fromStdString(weight.id) + "-load-report.txt"));
                    if (!report_file.open(QIODevice::WriteOnly | QIODevice::Text))
                        throw std::runtime_error("cannot save model import report");
                    report_file.write(QByteArray::fromStdString(report.summary() + "\n"));
                    for (const auto& message : report.messages)
                        report_file.write(QByteArray::fromStdString(message + "\n"));
                    log_->appendPlainText(tr("[import] %1: %2").arg(QString::fromStdString(weight.id),
                        QString::fromStdString(report.summary())));
                    log_->appendPlainText(tr("Load report: %1").arg(report_file.fileName()));
                } else if (!QFile::copy(weight.source_path, destination)) {
                    throw std::runtime_error("cannot copy imported model weight into Project");
                }
                jocelin::model::ModelWeight imported;
                imported.id = weight.id;
                imported.format = weight.format;
                imported.precision = native ? jocelin::model::WeightPrecision::Float32 : weight.precision;
                imported.role = weight.role;
                imported.path = destination.toStdString();
                imported.parent_weight_id = weight.parent_weight_id;
                imported.created_at = weight.created_at.empty() ? model.created_at : weight.created_at;
                imported.supported_device_types = weight.supported_device_types.empty()
                    ? jocelin::model::default_supported_devices(weight.format)
                    : weight.supported_device_types;
                imported.runtime_name = weight.runtime_name;
                imported.runtime_version = weight.runtime_version;
                imported.artifact_version = weight.artifact_version;
                imported.onnx_opset = weight.onnx_opset;
                imported.input_signature = weight.input_signature;
                imported.output_signature = weight.output_signature;
                imported.target_architecture = weight.target_architecture;
                model.weights.push_back(std::move(imported));
            }
            const auto added = project_.add_model(std::move(model));
            import_added = true;
            project_.save_project();
            node_workspace_->refresh_models();
            statusBar()->showMessage(
                tr("%1 imported into Project Model List").arg(QString::fromStdString(added.name)), 5000);
        } catch (const std::exception& error) {
            if (!import_added && !imported_asset_directory.isEmpty())
                QDir(imported_asset_directory).removeRecursively();
            QMessageBox::warning(
                this, tr("Cannot import model"), QString::fromUtf8(error.what()));
        }
    };
    node_workspace_->export_model_requested = [this](int, const QString& model_id) {
        const auto* model = project_.find_model(model_id);
        if (!model) return;
        const QString parent = QFileDialog::getExistingDirectory(
            this, tr("Export Project model"), project_.metadata().root_directory);
        if (parent.isEmpty()) return;
        try {
            const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss");
            const QString destination = QDir(parent).filePath(
                QString::fromStdString(model->id) + "-" + stamp);
            if (!QDir().mkpath(destination))
                throw std::runtime_error("cannot create model export directory");
            QJsonArray classes;
            for (const auto& value : model->classes)
                classes.append(QJsonObject{
                    {"id", QString::fromStdString(value.id)},
                    {"name", QString::fromStdString(value.name)},
                    {"mask_value", value.mask_value}});
            QJsonArray weights;
            for (const auto& weight : model->weights) {
                const QFileInfo source(QString::fromStdString(weight.path));
                if (!source.isFile())
                    throw std::runtime_error("model weight is missing");
                const QString filename = QString::fromStdString(weight.id) +
                    (source.suffix().isEmpty() ? QString{} : "." + source.suffix());
                if (!QFile::copy(source.absoluteFilePath(), QDir(destination).filePath(filename)))
                    throw std::runtime_error("cannot copy model weight");
                QJsonArray supported_devices;
                const auto device_types = weight.supported_device_types.empty()
                    ? jocelin::model::default_supported_devices(weight.format)
                    : weight.supported_device_types;
                for (const auto type : device_types)
                    supported_devices.append(QString::fromStdString(jocelin::config::to_string(type)));
                weights.append(QJsonObject{
                    {"id", QString::fromStdString(weight.id)},
                    {"format", QString::fromStdString(jocelin::config::to_string(weight.format))},
                    {"precision", QString::fromStdString(jocelin::model::to_string(weight.precision))},
                    {"role", QString::fromStdString(jocelin::model::to_string(weight.role))},
                    {"file", filename},
                    {"parent_weight_id", QString::fromStdString(weight.parent_weight_id)},
                    {"created_at", QString::fromStdString(weight.created_at)},
                    {"supported_devices", supported_devices},
                    {"runtime_name", QString::fromStdString(weight.runtime_name)},
                    {"runtime_version", QString::fromStdString(weight.runtime_version)},
                    {"artifact_version", QString::fromStdString(weight.artifact_version)},
                    {"onnx_opset", weight.onnx_opset},
                    {"input_signature", QString::fromStdString(weight.input_signature)},
                    {"output_signature", QString::fromStdString(weight.output_signature)},
                    {"target_architecture", QString::fromStdString(weight.target_architecture)}});
            }
            write_json_file(QDir(destination).filePath("model.json"), QJsonObject{
                {"format", "deep_learning_project_model"},
                {"name", QString::fromStdString(model->name)},
                {"created_at", QString::fromStdString(model->created_at)},
                {"task", QString::fromStdString(jocelin::config::to_string(model->task))},
                {"model_index", model->model_index},
                {"model_scale", QString::fromStdString(jocelin::config::to_string(model->model_scale))},
                {"image_width", model->image_width},
                {"image_height", model->image_height},
                {"classes", classes},
                {"metric_name", QString::fromStdString(model->metric_name)},
                {"metric", model->metric},
                {"weights", weights}});
            statusBar()->showMessage(
                tr("%1 exported to %2").arg(QString::fromStdString(model->name), destination), 5000);
        } catch (const std::exception& error) {
            QMessageBox::warning(
                this, tr("Cannot export model"), QString::fromUtf8(error.what()));
        }
    };
    node_workspace_->delete_models_requested = [this](int, const QStringList& model_ids) {
        if (!project_.has_storage() || model_ids.isEmpty()) return;

        const QString models_root = QDir(project_.metadata().root_directory).filePath("models");
        QStringList valid_model_ids;
        QStringList model_names;
        QStringList model_directories;
        for (const auto& model_id : model_ids) {
            const auto* model = project_.find_model(model_id);
            if (!model) continue;

            const QString model_directory = QString::fromStdString(model->asset_directory);
            const QString relative = QDir(models_root).relativeFilePath(model_directory);
            if (relative == ".." || relative.startsWith("../") || QFileInfo(relative).isAbsolute()) {
                QMessageBox::warning(
                    this, tr("Cannot delete models"),
                    tr("A selected model is not stored inside this Project's models directory:\n%1")
                        .arg(model_directory));
                return;
            }
            valid_model_ids.push_back(model_id);
            model_names.push_back(QString::fromStdString(model->name));
            model_directories.push_back(model_directory);
        }
        if (valid_model_ids.isEmpty()) return;

        if (QMessageBox::question(
                this, tr("Delete selected models"),
                tr("Permanently delete %1 selected model(s)?\n\n%2\n\nAll files for these models will be deleted and cannot be recovered.")
                    .arg(valid_model_ids.size())
                    .arg(model_names.join("\n")),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;

        for (const auto& model_directory : model_directories) {
            if (QDir(model_directory).exists() && !QDir(model_directory).removeRecursively()) {
                QMessageBox::warning(
                    this, tr("Cannot delete models"),
                    tr("Cannot delete model directory:\n%1").arg(model_directory));
                return;
            }
        }
        for (const auto& model_id : valid_model_ids) project_.remove_model(model_id);
        try {
            project_.save_project();
            node_workspace_->refresh_models();
            statusBar()->showMessage(
                tr("%1 model(s) deleted").arg(valid_model_ids.size()), 5000);
        } catch (const std::exception& error) {
            node_workspace_->refresh_models();
            QMessageBox::warning(
                this, tr("Cannot save model deletion"), QString::fromUtf8(error.what()));
        }
    };
    node_workspace_->clear_models_requested = [this](int) {
        if (!project_.has_storage() || project_.models().empty()) return;

        const QString models_root = QDir(project_.metadata().root_directory).filePath("models");
        QStringList model_directories;
        model_directories.reserve(static_cast<int>(project_.models().size()));
        for (const auto& model : project_.models()) {
            const QString model_directory = QString::fromStdString(model.asset_directory);
            const QString relative = QDir(models_root).relativeFilePath(model_directory);
            if (relative == ".." || relative.startsWith("../") || QFileInfo(relative).isAbsolute()) {
                QMessageBox::warning(
                    this, tr("Cannot clear models"),
                    tr("A model is not stored inside this Project's models directory:\n%1")
                        .arg(model_directory));
                return;
            }
            model_directories.push_back(model_directory);
        }

        if (QMessageBox::question(
                this, tr("Clear Project models"),
                tr("Permanently delete all %1 Project models?\n\nAll model asset directories will be deleted and cannot be recovered.")
                    .arg(project_.models().size()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;

        for (const auto& model_directory : model_directories) {
            if (QDir(model_directory).exists() && !QDir(model_directory).removeRecursively()) {
                QMessageBox::warning(
                    this, tr("Cannot clear models"),
                    tr("Cannot delete model directory:\n%1").arg(model_directory));
                return;
            }
        }
        project_.clear_models();
        try {
            project_.save_project();
            node_workspace_->refresh_models();
            statusBar()->showMessage(tr("Project ModelList cleared"), 5000);
        } catch (const std::exception& error) {
            node_workspace_->refresh_models();
            QMessageBox::warning(
                this, tr("Cannot save ModelList clear"), QString::fromUtf8(error.what()));
        }
    };
    node_workspace_->infer_requested = [this](
        int id, const InferenceLaunchRequest& request) {
        startInference(id, request);
    };
    node_workspace_->stop_requested = [this] { training_job_.request_stop(); };
    node_workspace_->pause_requested = [this] { training_job_.request_pause(); };
    node_workspace_->run_details_requested = [this](const QString& run_id) {
        output_dock_->show();
        if (!run_id.isEmpty() && project_.has_storage()) {
            const QString path =
                QDir(project_.metadata().root_directory)
                    .filePath("runs/" + run_id + "/training.log");
            QFile file(path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                result_->setPlainText(
                    tr("Training run %1\n%2\n\n%3")
                        .arg(run_id, path,
                             QString::fromUtf8(file.readAll())));
                output_tabs_->setCurrentWidget(result_);
            } else {
                log_->appendPlainText(
                    tr("Run log is unavailable: %1").arg(path));
                output_tabs_->setCurrentWidget(log_);
            }
        } else {
            output_tabs_->setCurrentWidget(log_);
        }
        output_dock_->raise();
    };
    node_workspace_->node_requested = [this](int id) { openNodeWorkspace(id); };
    node_workspace_->status = [this](const QString& message) { statusBar()->showMessage(message, 6000); };

    project_dock_ = new QDockWidget(tr("Project"), this);
    project_dock_->setObjectName("projectDock");
    project_dock_->setMinimumWidth(170);
    auto* project_panel = new QWidget(project_dock_);
    project_panel->setMinimumWidth(0);
    auto* project_layout = new QVBoxLayout(project_panel);
    auto* metadata_group = new QGroupBox(tr("Project metadata"), project_panel);
    auto* metadata_form = new QFormLayout(metadata_group);
    project_name_ = new QLineEdit(metadata_group);
    project_name_->setMinimumWidth(0);
    project_name_->setObjectName("projectNameEdit");
    project_description_ = new QLineEdit(metadata_group);
    project_description_->setMinimumWidth(0);
    project_description_->setObjectName("projectDescriptionEdit");
    project_root_ = new QLabel(metadata_group);
    project_root_->setWordWrap(false);
    metadata_form->addRow(tr("Name"), project_name_);
    metadata_form->addRow(tr("Description"), project_description_);
    metadata_form->addRow(tr("Location"), project_root_);
    project_layout->addWidget(metadata_group);
    auto* workflow_hint = new QLabel(tr("Manage workflows from the tabs above the canvas. Double-click a tab to rename it."), project_panel);
    workflow_hint->setWordWrap(true);
    workflow_hint->setProperty("muted", true);
    project_layout->addWidget(workflow_hint);
    project_layout->addStretch();
    project_dock_->setWidget(project_panel);
    addDockWidget(Qt::LeftDockWidgetArea, project_dock_);
    connect(project_name_, &QLineEdit::editingFinished, this, [this] {
        try { project_.set_project_name(project_name_->text()); updateWindowTitle(); }
        catch (const std::exception& error) { QMessageBox::warning(this, tr("Invalid project name"), error.what()); refreshProjectUi(); }
    });
    connect(project_description_, &QLineEdit::editingFinished, this, [this] {
        project_.set_project_description(project_description_->text()); updateWindowTitle();
    });
    library_dock_ = new QDockWidget(tr("Node Library"), this);
    library_dock_->setObjectName("nodeLibraryDock");
    library_dock_->setMinimumWidth(170);
    auto* library = new QWidget(library_dock_);
    auto* library_layout = new QVBoxLayout(library);
    library_layout->setContentsMargins(8, 8, 8, 8);
    library_layout->setSpacing(5);
    auto* library_search = new QLineEdit(library);
    library_search->setObjectName("nodeLibrarySearch");
    library_search->setPlaceholderText(tr("Search nodes..."));
    library_search->setClearButtonEnabled(true);
    library_layout->addWidget(library_search);
    auto* library_help = new QLabel(tr("Click to add. Double-click a canvas node to configure."), library);
    library_help->setWordWrap(true);
    library_help->setProperty("muted", true);
    library_layout->addWidget(library_help);
    library_layout->addSpacing(4);
    QVector<QPair<QLabel*, QPushButton*>> library_items;
    for (const auto& descriptor : node_descriptors()) {
        const QString detail = descriptor.kind == NodeKind::DatasetInput
            ? tr("Import and validate raw images")
            : tr("Open the complete six-step task workspace");
        const QString group = descriptor.kind == NodeKind::DatasetInput
            ? "data" : "task";
        auto* group_label = new QLabel(group.toUpper(), library);
        group_label->setProperty("eyebrow", true);
        library_layout->addWidget(group_label);
        auto* button = new QPushButton(descriptor.badge + "   " + descriptor.title, library);
        button->setObjectName("add" + descriptor.title.simplified().remove(' ') + "Button");
        button->setProperty("nodeLibraryItem", true);
        button->setProperty("group", group);
        button->setToolTip(detail);
        button->setMinimumHeight(34);
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        connect(button, &QPushButton::clicked, this, [this, kind = descriptor.kind] { addNode(kind); });
        library_layout->addWidget(button);
        library_items.push_back({group_label, button});
    }
    connect(library_search, &QLineEdit::textChanged, this, [library_items](const QString& text) {
        const QString term = text.trimmed();
        for (const auto& item : library_items) {
            const bool visible = term.isEmpty() || item.second->text().contains(term, Qt::CaseInsensitive) ||
                                 item.second->toolTip().contains(term, Qt::CaseInsensitive);
            item.first->setVisible(visible);
            item.second->setVisible(visible);
        }
    });
    library_layout->addSpacing(12);
    auto* example = new QPushButton(tr("Load Example Workflow"), library);
    example->setObjectName("loadExampleButton");
    connect(example, &QPushButton::clicked, this, [this] { loadExampleWorkflow(); });
    library_layout->addWidget(example);
    library_layout->addStretch();
    library_dock_->setWidget(library);
    addDockWidget(Qt::LeftDockWidgetArea, library_dock_);
    project_dock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    library_dock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    tabifyDockWidget(project_dock_, library_dock_);
    project_dock_->raise();

    property_dock_ = new QDockWidget(tr("Properties"), this);
    property_dock_->setObjectName("propertiesDock");
    property_dock_->setMinimumWidth(250);
    auto* property_panel = new QWidget(property_dock_);
    auto* property_layout = new QVBoxLayout(property_panel);
    property_layout->setContentsMargins(0, 0, 0, 0);
    property_layout->setSpacing(0);
    property_editor_ = new PropertiesEditor(property_panel);
    property_layout->addWidget(property_editor_, 1);
    auto* overview = new QFrame(property_panel);
    overview->setObjectName("workflowOverview");
    auto* overview_layout = new QGridLayout(overview);
    overview_layout->setContentsMargins(12, 10, 12, 10);
    overview_layout->setHorizontalSpacing(12);
    overview_layout->setVerticalSpacing(6);
    auto* overview_title = new QLabel(tr("WORKFLOW OVERVIEW"), overview);
    overview_title->setProperty("eyebrow", true);
    overview_layout->addWidget(overview_title, 0, 0, 1, 2);
    const QStringList overview_names = {tr("Nodes"), tr("Connections"), tr("Parameters"), tr("Status")};
    QLabel** overview_values[] = {&overview_nodes_, &overview_connections_, &overview_parameters_, &overview_status_};
    for (int row = 0; row < overview_names.size(); ++row) {
        overview_layout->addWidget(new QLabel(overview_names[row], overview), row + 1, 0);
        *overview_values[row] = new QLabel(overview);
        (*overview_values[row])->setProperty("overviewValue", true);
        overview_layout->addWidget(*overview_values[row], row + 1, 1, Qt::AlignRight);
    }
    property_layout->addWidget(overview);
    auto* minimap = new QFrame(property_panel);
    minimap->setObjectName("minimapPanel");
    auto* minimap_layout = new QVBoxLayout(minimap);
    minimap_layout->setContentsMargins(12, 10, 12, 10);
    auto* minimap_title = new QLabel(tr("MINIMAP"), minimap);
    minimap_title->setProperty("eyebrow", true);
    auto* minimap_view = new WorkflowMinimap(view_, minimap);
    minimap_view->setToolTip(tr("Read-only workflow overview. The green frame is the visible canvas."));
    minimap_layout->addWidget(minimap_title);
    minimap_layout->addWidget(minimap_view);
    property_layout->addWidget(minimap);
    connect(scene_, &QGraphicsScene::changed, minimap_view, [minimap_view] { minimap_view->refresh_overview(); });
    connect(view_->horizontalScrollBar(), &QScrollBar::valueChanged, minimap_view, [minimap_view] { minimap_view->viewport()->update(); });
    connect(view_->verticalScrollBar(), &QScrollBar::valueChanged, minimap_view, [minimap_view] { minimap_view->viewport()->update(); });
    if (auto* workflow_view = dynamic_cast<WorkflowView*>(view_))
        workflow_view->viewport_changed = [minimap_view] { minimap_view->viewport()->update(); };
    minimap_view->refresh_overview();
    property_dock_->setWidget(property_panel);
    addDockWidget(Qt::RightDockWidgetArea, property_dock_);

    output_dock_ = new QDockWidget(tr("Output"), this);
    output_dock_->setObjectName("outputDock");
    output_tabs_ = new QTabWidget(output_dock_);
    result_ = new QTextEdit(output_tabs_);
    result_->setObjectName("taskRunSummary");
    result_->setReadOnly(true);
    log_ = new QPlainTextEdit(output_tabs_);
    log_->setObjectName("executionLog");
    log_->setReadOnly(true);
    output_tabs_->addTab(result_, tr("Run Summary"));
    output_tabs_->addTab(log_, tr("Activity Log"));
    output_dock_->setWidget(output_tabs_);
    addDockWidget(Qt::BottomDockWidgetArea, output_dock_);
    view_menu->addAction(project_dock_->toggleViewAction());
    view_menu->addAction(library_dock_->toggleViewAction());
    view_menu->addAction(property_dock_->toggleViewAction());
    view_menu->addAction(output_dock_->toggleViewAction());
    resizeDocks({project_dock_, property_dock_}, {196, 260}, Qt::Horizontal);
    resizeDocks({output_dock_}, {160}, Qt::Vertical);

    status_context_ = new QLabel(statusBar());
    status_saved_ = new QLabel(statusBar());
    status_runtime_ = new QLabel(tr("Ready"), statusBar());
    status_context_->setObjectName("workflowStatusContext");
    status_saved_->setObjectName("savedStatus");
    status_runtime_->setObjectName("runtimeStatus");
    status_context_->setMinimumWidth(0);
    status_context_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    status_runtime_->setProperty("status", "ready");
    statusBar()->addWidget(status_context_, 1);
    statusBar()->addPermanentWidget(status_saved_);
    statusBar()->addPermanentWidget(status_runtime_);

}

void MainWindow::restoreShellState() {
    QSettings settings("Jocelin", "DeepLearningStudio");
    const QByteArray geometry = settings.value("shell/geometry").toByteArray();
    const QByteArray state = settings.value("shell/docks").toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    if (!state.isEmpty()) restoreState(state);

    bool visible = false;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->availableGeometry().intersects(frameGeometry())) {
            visible = true;
            break;
        }
    }
    if (!visible) {
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
        resize(qMin(1440, available.width() - 32), qMin(900, available.height() - 32));
        move(available.center() - rect().center());
    }
    normalizeDockState();
}

void MainWindow::normalizeDockState() {
    // Project navigation and node creation must never restore as overlapping
    // floating palettes. Tabs preserve canvas width on small displays.
    for (QDockWidget* dock : {project_dock_, library_dock_}) {
        if (dock->isFloating()) dock->setFloating(false);
        addDockWidget(Qt::LeftDockWidgetArea, dock);
    }
    tabifyDockWidget(project_dock_, library_dock_);
    project_dock_->raise();
    if (property_dock_->isFloating()) property_dock_->setFloating(false);
    addDockWidget(Qt::RightDockWidgetArea, property_dock_);
    resizeDocks({project_dock_, property_dock_}, {196, 260}, Qt::Horizontal);
}

WorkflowGraph& MainWindow::graph() { return project_.active_workflow().document.graph; }
std::map<int, QPointF>& MainWindow::positions() { return project_.active_workflow().document.positions; }

void MainWindow::bindActiveWorkflow() {
    node_workspace_->clear_node();
    scene_->set_model(&graph(), &positions());
    if (workspace_stack_->currentWidget() == node_workspace_)
        workspace_stack_->setCurrentWidget(workflow_workspace_);
    result_->clear();
    refreshProjectUi();
    showProperties();
    refreshValidation();
}

void MainWindow::unbindWorkflowViews() {
    if (scene_) scene_->set_model(nullptr, nullptr);
    if (node_workspace_) node_workspace_->clear_node();
    if (property_editor_) property_editor_->show_workflow({}, {}, 0, 0);
}

void MainWindow::showProjectHome() {
    setMinimumSize(760, 480);
    project_home_->refresh_recent_projects();
    workspace_stack_->setCurrentWidget(project_home_);
    statusBar()->clearMessage();
    toolbar_->hide();
    project_dock_->hide(); library_dock_->hide(); property_dock_->hide(); output_dock_->hide();
    if (status_context_) status_context_->clear();
    if (status_saved_) status_saved_->clear();
}

void MainWindow::showWorkflowWorkspace() {
    setMinimumSize(760, 480);
    workspace_stack_->setCurrentWidget(workflow_workspace_);
    statusBar()->clearMessage();
    toolbar_->show();
    toolbar_->setVisible(true);
    project_dock_->show(); library_dock_->show();
    library_dock_->raise();
    if (width() >= 1200) property_dock_->show();
    else property_dock_->hide();
    refreshProjectUi();
    QTimer::singleShot(0, this, [this] {
        if (workspace_stack_->currentWidget() != workflow_workspace_) return;
        const QRectF bounds = node_bounds(scene_);
        if (!bounds.isEmpty())
            fit_graph_down_only(view_, bounds.adjusted(-70, -70, 70, 70));
    });
}

QString MainWindow::connectedDatasetDirectory(int task_node_id) {
    for (const auto& edge : graph().connections()) {
        if (edge.to_node != task_node_id || edge.to_port != 0) continue;
        const auto& source = graph().node(edge.from_node);
        if (source.kind != NodeKind::DatasetInput) continue;
        return QString::fromStdString(std::get<DatasetConfig>(source.config).directory);
    }
    return {};
}

void MainWindow::openNodeWorkspace(int node_id) {
    if (graph().nodes().count(node_id) == 0) return;
    const QString dataset = graph().node(node_id).kind == NodeKind::TaskConfig
        ? connectedDatasetDirectory(node_id) : QString{};
    const QString annotation_directory =
        graph().node(node_id).kind == NodeKind::TaskConfig &&
        project_.has_storage()
        ? QDir(project_.metadata().root_directory).filePath(
              QStringLiteral("annotations/%1/task_%2/masks")
                  .arg(project_.active_workflow().id)
                  .arg(node_id))
        : QString{};
    node_workspace_->edit_node(
        &graph(), node_id, dataset, &project_.models(),
        annotation_directory);
    setMinimumSize(760, 480);
    workspace_stack_->setCurrentWidget(node_workspace_);
    toolbar_->show();
    run_action_->setVisible(false);
    project_dock_->hide(); library_dock_->hide(); property_dock_->hide(); output_dock_->hide();
}

void MainWindow::startTraining(
    int task_node_id, const TrainingLaunchRequest& launch)
{
    bool created_model_row = false;
    bool resumed_model_row = false;
    try {
        if (graph().nodes().count(task_node_id) == 0 || graph().node(task_node_id).kind != NodeKind::TaskConfig)
            throw std::runtime_error("training must start from a Task Config node");
        TaskWorkspaceService service(graph(), task_node_id);
        service.synchronize();
        auto& task = service.task();
        const auto tool_type = jocelin::config::tool_type_from_string(task.task);
        const auto model_scale = jocelin::config::model_scale_from_string(task.model_scale);
        if (!supports_model_spec(task.task, task.model_index, task.model_scale))
            throw std::runtime_error("model spec is not registered for the selected task");
        if (launch.mode != "new" && launch.mode != "inherit" &&
            launch.mode != "transfer" && launch.mode != "resume")
            throw std::runtime_error("unsupported training model source mode");
        const ProjectModel* parent = nullptr;
        const ProjectModel* resume_model = nullptr;
        const jocelin::model::ModelWeight* parent_training_weight = nullptr;
        if (launch.mode == "inherit" || launch.mode == "transfer") {
            parent = project_.find_model(launch.model_id);
            if (!parent)
                throw std::runtime_error(
                    "selected Project model no longer exists");
            if (parent->training_status != jocelin::model::ModelTrainingStatus::Completed &&
                parent->training_status != jocelin::model::ModelTrainingStatus::Imported)
                throw std::runtime_error(
                    "selected Project model training is not complete");
            parent_training_weight = jocelin::model::find_training_weight(*parent);
            if (!parent_training_weight ||
                !QFileInfo::exists(QString::fromStdString(parent_training_weight->path)))
                throw std::runtime_error(
                    "selected Project model has no available LibTorch training weight");
            if (launch.mode == "inherit") {
                if (parent->task != tool_type ||
                    parent->model_index != task.model_index ||
                    parent->model_scale != model_scale)
                    throw std::runtime_error(
                        "selected inheritance model task or architecture is incompatible");
                jocelin::config::AiConfig expected;
                expected.tool_type = tool_type;
                expected.model_index = task.model_index;
                expected.model_scale = model_scale;
                const auto metadata = jocelin::checkpoint::inspect_checkpoint(
                    parent_training_weight->path);
                jocelin::checkpoint::require_matching_spec(metadata, expected);
            }
        }
        jocelin::checkpoint::CheckpointMetadata resume_metadata;
        if (launch.mode == "resume") {
            resume_model = project_.find_model(launch.model_id);
            if (!resume_model ||
                resume_model->training_status != jocelin::model::ModelTrainingStatus::Paused)
                throw std::runtime_error("selected ModelList row is not paused");
            const auto* resume_weight = jocelin::model::find_training_weight(*resume_model);
            if (!resume_weight ||
                !QFileInfo::exists(QString::fromStdString(resume_weight->path)))
                throw std::runtime_error("paused training checkpoint is unavailable");
            resume_metadata = jocelin::checkpoint::inspect_checkpoint(
                resume_weight->path);
            if (!resume_metadata.resumable())
                throw std::runtime_error("paused checkpoint is not resumable");
            if (resume_metadata.tool_type != tool_type ||
                resume_metadata.model_index != task.model_index ||
                resume_metadata.model_scale != model_scale)
                throw std::runtime_error(
                    "paused checkpoint task or architecture does not match the current task");
            if (resume_metadata.completed_epochs >= task.training.epochs)
                throw std::runtime_error(
                    "paused checkpoint has already reached the requested epoch count");
        }
        jocelin::runtime::ComputeDevice device;
        if (!select_compute_device(this, &device)) return;
        task.device = device.device;
        if (task.training.output_directory.empty()) {
            if (!project_.has_storage())
                throw std::runtime_error(
                    "save the project or configure a model output directory before training");
            task.training.output_directory =
                QDir(project_.metadata().root_directory)
                    .filePath("models")
                    .toStdString();
        }
        if (!QDir().mkpath(QString::fromStdString(task.training.output_directory)))
            throw std::runtime_error("cannot create the training output directory");

        const int run_number = project_.allocate_training_run_number();
        const QString run_id =
            QString("train-%1").arg(run_number, 4, 10, QChar('0'));
        const QString run_directory_name =
            QString("%1-%2-m%3-%4")
                .arg(QString::fromStdString(task.task))
                .arg(QString::fromStdString(task.model_scale))
                .arg(task.model_index)
                .arg(run_id);
        const QString run_directory =
            QDir(QString::fromStdString(task.training.output_directory))
                .filePath(run_directory_name);
        if (!QDir().mkpath(run_directory))
            throw std::runtime_error(
                "cannot create the training run directory");

        jocelin::config::WeightSource weight_source;
        if (launch.mode == "inherit")
            weight_source.mode = jocelin::config::WeightMode::Inheritance;
        else if (launch.mode == "transfer")
            weight_source.mode = jocelin::config::WeightMode::Transfer;
        else if (launch.mode == "resume")
            weight_source.mode = jocelin::config::WeightMode::Resume;
        else
            weight_source.mode = jocelin::config::WeightMode::DefaultPretrained;
        if (launch.mode == "resume")
            weight_source.path = jocelin::model::find_training_weight(*resume_model)->path;
        else if (parent_training_weight)
            weight_source.path = parent_training_weight->path;
        auto request = service.build_training_request(
            device.device, std::move(weight_source));
        if (launch.mode == "resume" &&
            resume_metadata.num_classes !=
                static_cast<int>(request.class_names.size()))
            throw std::runtime_error(
                "paused checkpoint class schema does not match the current task");
        request.config.model_save_dir = run_directory.toStdString();
        const QString parent_model_id = parent
            ? QString::fromStdString(parent->id)
            : QString{};
        const QString parent_weight_id = parent_training_weight
            ? QString::fromStdString(parent_training_weight->id)
            : QString{};

        if (launch.mode == "resume") {
            auto* row = project_.find_model(launch.model_id);
            row->training_status = jocelin::model::ModelTrainingStatus::Running;
            project_.mark_models_dirty();
            active_training_model_id_ = launch.model_id;
            resumed_model_row = true;
        } else {
            ProjectModel model;
            model.training_run_id = run_id.toStdString();
            model.created_at =
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
            model.task = tool_type;
            model.model_index = task.model_index;
            model.model_scale = model_scale;
            for (const auto& value : service.binding().classes) {
                if (!value.active) continue;
                model.classes.push_back({value.id, value.name, value.mask_value});
            }
            std::sort(
                model.classes.begin(), model.classes.end(),
                [](const auto& left, const auto& right) {
                    return left.mask_value < right.mask_value;
                });
            model.image_width = task.training.image_width;
            model.image_height = task.training.image_height;
            model.asset_directory = run_directory.toStdString();
            model.creation_mode = launch.mode == "inherit"
                ? jocelin::model::ModelCreationMode::InheritTraining
                : launch.mode == "transfer"
                    ? jocelin::model::ModelCreationMode::TransferTraining
                    : jocelin::model::ModelCreationMode::NewTraining;
            model.training_status = jocelin::model::ModelTrainingStatus::Running;
            model.parent_model_id = parent ? parent->id : std::string{};
            const auto added = project_.add_model(std::move(model));
            active_training_model_id_ = QString::fromStdString(added.id);
            created_model_row = true;
        }
        service.record_training_started(active_training_model_id_.toStdString());
        active_training_node_id_ = task_node_id;
        active_training_source_mode_ = launch.mode;
        active_training_parent_model_id_ = parent_model_id;
        active_training_source_weight_id_ = parent_weight_id;
        active_training_run_id_ = run_id;
        active_training_run_number_ = run_number;
        active_training_run_directory_ = run_directory;
        active_training_started_at_ =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        active_training_log_path_ =
            QDir(run_directory).filePath("training.log");
        project_.mark_active_dirty();
        if (project_.has_storage()) project_.save_project();
        setEditingEnabled(false);
        node_workspace_->setEnabled(true);
        node_workspace_->training_started(task_node_id);
        node_workspace_->set_runtime_busy(true);
        const QString start_message =
            tr("Training %1 started on %2 路 source %3")
                .arg(run_id,
                     QString::fromStdString(device.display_name),
                     launch.mode);
        log_->appendPlainText(start_message);
        append_log_file(active_training_log_path_, start_message);
        training_job_.start(request);
    } catch (const std::exception& error) {
        if (!training_job_.is_running()) {
            if (created_model_row && !active_training_model_id_.isEmpty()) {
                project_.remove_model(active_training_model_id_);
                if (graph().nodes().count(task_node_id) != 0) {
                    TaskWorkspaceService service(graph(), task_node_id);
                    service.task().runtime.training_model_id.clear();
                    project_.mark_active_dirty();
                }
            } else if (resumed_model_row) {
                if (auto* row = project_.find_model(active_training_model_id_)) {
                    row->training_status = jocelin::model::ModelTrainingStatus::Paused;
                    project_.mark_models_dirty();
                }
            }
            active_training_node_id_ = 0;
            active_training_model_id_.clear();
            active_training_source_mode_.clear();
            active_training_parent_model_id_.clear();
            active_training_source_weight_id_.clear();
            active_training_run_id_.clear();
            active_training_run_number_ = 0;
            active_training_run_directory_.clear();
            active_training_started_at_.clear();
            active_training_log_path_.clear();
            setEditingEnabled(true);
            node_workspace_->set_runtime_busy(false);
        }
        QMessageBox::critical(this, tr("Cannot start training"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::startInference(
    int task_node_id,
    const InferenceLaunchRequest& launch)
{
    try {
        if (graph().nodes().count(task_node_id) == 0 || graph().node(task_node_id).kind != NodeKind::TaskConfig)
            throw std::runtime_error("inference must start from a Task Config node");
        if (launch.model_id.isEmpty())
            throw std::runtime_error("select a Project model before starting inference");
        if (launch.weight_id.isEmpty())
            throw std::runtime_error("select an inference weight before starting inference");

        TaskWorkspaceService service(graph(), task_node_id);
        service.synchronize();
        auto& task = service.task();
        const auto tool_type = jocelin::config::tool_type_from_string(task.task);
        const QString model_id = launch.model_id;
        const auto* model = project_.find_model(model_id);
        if (!model)
            throw std::runtime_error("selected Project model no longer exists");
        if (model->task != tool_type)
            throw std::runtime_error("selected Project model belongs to a different task repository");

        const auto* inference_weight = jocelin::model::find_weight(
            *model, launch.weight_id.toStdString());
        if (!inference_weight)
            throw std::runtime_error("selected inference weight no longer exists");
        if (!project_.has_storage())
            throw std::runtime_error("save the project before inference");
        const QString weight_format = QString::fromStdString(
            jocelin::config::to_string(inference_weight->format));
        const QString output = QDir(project_.metadata().root_directory).filePath(
            "results/" + model_id + "/" + weight_format);
        QDir output_directory(output);
        if (output_directory.exists() && !output_directory.removeRecursively())
            throw std::runtime_error("cannot replace the existing inference result directory");
        if (!QDir().mkpath(output))
            throw std::runtime_error("cannot create the inference result directory");
        task.inference_output_directory = output.toStdString();
        task.device = launch.device;
        auto request = service.build_inference_request(
            launch.device, *model, *inference_weight);
        service.record_inference_started();
        setEditingEnabled(false);
        node_workspace_->setEnabled(true);
        active_inference_node_id_ = task_node_id;
        active_inference_model_id_ = model_id;
        active_inference_weight_id_ = QString::fromStdString(inference_weight->id);
        active_inference_weight_format_ = inference_weight->format;
        active_inference_output_ = output;
        active_inference_image_count_ = static_cast<int>(request.image_paths.size());
        project_.mark_active_dirty();
        node_workspace_->inference_started(task_node_id);
        node_workspace_->set_runtime_busy(true);
        log_->appendPlainText(
            tr("Inference started with %1 on %2")
                .arg(QString::fromStdString(model->name),
                     QString::fromStdString(launch.device.id)));
        inference_job_.start(request);
    } catch (const std::exception& error) {
        if (!inference_job_.is_running()) {
            active_inference_node_id_ = 0;
            active_inference_model_id_.clear();
        active_inference_weight_id_.clear();
            active_inference_output_.clear();
            active_inference_image_count_ = 0;
            setEditingEnabled(true);
            node_workspace_->set_runtime_busy(false);
        }
        QMessageBox::critical(this, tr("Cannot start inference"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::refreshProjectUi() {
    const QSignalBlocker name_block(project_name_);
    const QSignalBlocker description_block(project_description_);
    const QSignalBlocker workflow_block(workflow_tabs_);
    project_name_->setText(project_.metadata().name);
    project_description_->setText(project_.metadata().description);
    const QString root = project_.has_storage() ? project_.metadata().root_directory : tr("Not saved");
    project_root_->setText(QFontMetrics(project_root_->font()).elidedText(root, Qt::ElideMiddle, 190));
    project_root_->setToolTip(root);
    while (workflow_tabs_->count() > 0) workflow_tabs_->removeTab(0);
    for (const auto& workflow : project_.workflows()) {
        const int index = workflow_tabs_->addTab(workflow.name + (workflow.dirty ? " *" : ""));
        auto* close = new QToolButton(workflow_tabs_);
        close->setObjectName("workflowTabCloseButton");
        close->setText("x");
        close->setToolTip(tr("Close workflow"));
        close->setAutoRaise(true);
        close->setFixedSize(18, 18);
        workflow_tabs_->setTabButton(index, QTabBar::RightSide, close);
        connect(close, &QToolButton::clicked, this, [this, index] {
            if (index < 0 || static_cast<size_t>(index) >= project_.workflows().size()) return;
            if (static_cast<size_t>(index) != project_.active_index()) {
                unbindWorkflowViews();
                project_.set_active_index(static_cast<size_t>(index));
                bindActiveWorkflow();
            }
            removeWorkflow();
        });
    }
    workflow_tabs_->setCurrentIndex(static_cast<int>(project_.active_index()));
    if (workflow_context_title_ && workflow_context_hint_) {
        workflow_context_title_->setText(project_.metadata().name + "  /  " + project_.active_workflow().name);
        workflow_context_hint_->setText(graph().nodes().empty()
            ? tr("Empty workflow - add a Dataset node from the left.")
            : tr("Drag nodes from the left. Double-click to configure."));
    }
    if (canvas_empty_state_) canvas_empty_state_->setVisible(graph().nodes().empty());
    if (overview_nodes_) overview_nodes_->setText(QString::number(graph().nodes().size()));
    if (overview_connections_) overview_connections_->setText(QString::number(graph().connections().size()));
    if (overview_parameters_) overview_parameters_->setText(QString::number(graph().nodes().size()));
    if (overview_status_) overview_status_->setText(issues_.empty() && !graph().nodes().empty() ? tr("Ready") :
                                                    graph().nodes().empty() ? tr("Empty") : tr("Needs review"));
    if (status_context_) status_context_->setText(graph().nodes().empty()
        ? tr("Workflow: Empty")
        : tr("Workflow: %1 nodes | %2 connections").arg(graph().nodes().size()).arg(graph().connections().size()));
    if (status_saved_) status_saved_->setText(project_.is_dirty() ? tr("Unsaved changes  *") :
                                              project_.has_storage() ? tr("Saved") : tr("Saved: Never"));
    updateWindowTitle();
}

void MainWindow::updateWindowTitle() {
    setWindowTitle(project_.metadata().name + " - DeepLearning Workflow Studio" + (project_.is_dirty() ? " *" : ""));
}

void MainWindow::updateRecentProjects(const QString& project_file) {
    RecentProjectStore store;
    const QStringList recent = project_file.isEmpty()
        ? store.prune() : store.add(project_file);
    recent_menu_->clear();
    for (const QString& path : recent) {
        auto* action = recent_menu_->addAction(path);
        connect(action, &QAction::triggered, this, [this, path] { openProject(path); });
    }
    recent_menu_->setEnabled(!recent.isEmpty());
    if (project_home_) project_home_->refresh_recent_projects();
}

bool MainWindow::confirmProjectTransition() {
    if (!project_.is_dirty()) return true;
    const auto answer = QMessageBox::warning(
        this, tr("Unsaved project"), tr("Save changes to %1 before continuing?").arg(project_.metadata().name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return saveProject();
    return true;
}

bool MainWindow::saveProject() {
    QString root = project_.metadata().root_directory;
    if (root.isEmpty()) return saveProjectAs();
    try {
        project_.save_project(root);
        refreshProjectUi();
        updateRecentProjects(project_.project_file());
        log_->appendPlainText(tr("Project saved: %1").arg(project_.project_file()));
        statusBar()->showMessage(tr("Project saved"), 4000);
        return true;
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Save failed"), QString::fromUtf8(error.what()));
        return false;
    }
}

bool MainWindow::saveProjectAs() {
    SaveProjectAsDialog dialog(
        project_.metadata().name, project_.metadata().root_directory, this);
    if (dialog.exec() != QDialog::Accepted) return false;
    const size_t external_count = project_.external_reference_count();
    if (external_count > 0) {
        QMessageBox::information(
            this, tr("Shared external resources"),
            tr("This project contains %1 external resource reference(s). They will remain shared and will not be copied.")
                .arg(external_count));
    }
    try {
        const QString old_project = project_.project_file();
        project_.save_project_as(dialog.project_name(), dialog.parent_directory());
        refreshProjectUi();
        if (!old_project.isEmpty()) updateRecentProjects(old_project);
        updateRecentProjects(project_.project_file());
        log_->appendPlainText(tr("Project copy created: %1").arg(project_.project_file()));
        statusBar()->showMessage(tr("Project saved as an independent copy"), 4000);
        return true;
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Save Project As failed"), QString::fromUtf8(error.what()));
        return false;
    }
}

void MainWindow::newProject() {
    newProjectFromTemplate({});
}

void MainWindow::newProjectFromTemplate(const QString& template_task) {
    if (!confirmProjectTransition()) return;
    NewProjectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString name = dialog.projectName();
    const QString location = dialog.projectLocation();
    const QString desc = dialog.projectDescription();
    unbindWorkflowViews();
    try {
        project_.new_project(name);
        if (!desc.isEmpty()) project_.set_project_description(desc);
        if (!template_task.isEmpty()) {
            auto& template_graph = graph();
            const int dataset = template_graph.add_node(NodeKind::DatasetInput);
            const int task = template_graph.add_task_node({template_task.toStdString()});
            template_graph.connect(dataset, 0, task, 0);
            positions()[dataset] = QPointF(-300, -40);
            positions()[task] = QPointF(80, -40);
            project_.mark_active_dirty();
        }
        project_.save_project(location);
        bindActiveWorkflow();
        showWorkflowWorkspace();
        updateRecentProjects(project_.project_file());
        log_->appendPlainText(tr("Project created: %1").arg(project_.project_file()));
        statusBar()->showMessage(template_task.isEmpty() ? tr("Project created") : tr("Project created from template"), 4000);
    } catch (const std::exception& error) {
        bindActiveWorkflow();
        QMessageBox::critical(this, tr("Create project failed"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::openProject(const QString& requested_path) {
    if (!confirmProjectTransition()) return;
    QString path = requested_path;
    if (path.isEmpty()) path = QFileDialog::getOpenFileName(this, tr("Open project"), {}, tr("DeepLearning Project (project.json)"));
    if (path.isEmpty()) return;
    unbindWorkflowViews();
    try {
        project_.open_project(path);
        bindActiveWorkflow();
        showWorkflowWorkspace();
        updateRecentProjects(project_.project_file());
        log_->appendPlainText(tr("Project opened: %1").arg(project_.project_file()));
        statusBar()->showMessage(tr("Project opened - add nodes from Node Library or double-click an existing node to configure it."), 8000);
    } catch (const std::exception& error) {
        bindActiveWorkflow();
        QMessageBox::critical(this, tr("Open rejected"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::addWorkflow() {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Add workflow"), tr("Workflow name"), QLineEdit::Normal,
                                                tr("Workflow %1").arg(project_.workflows().size() + 1), &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    unbindWorkflowViews();
    try {
        project_.add_workflow(name);
        bindActiveWorkflow();
    } catch (const std::exception& error) {
        bindActiveWorkflow();
        QMessageBox::warning(this, tr("Cannot add workflow"), error.what());
    }
}

void MainWindow::removeWorkflow() {
    if (QMessageBox::question(this, tr("Remove workflow"),
        tr("Remove workflow '%1' from this project?").arg(project_.active_workflow().name)) != QMessageBox::Yes) return;
    unbindWorkflowViews();
    try {
        project_.remove_workflow(project_.active_index());
        bindActiveWorkflow();
    } catch (const std::exception& error) {
        bindActiveWorkflow();
        QMessageBox::warning(this, tr("Cannot remove workflow"), error.what());
    }
}

void MainWindow::renameWorkflow() {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Rename workflow"), tr("Workflow name"), QLineEdit::Normal,
                                                project_.active_workflow().name, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    try { project_.rename_active_workflow(name); refreshProjectUi(); showProperties(); }
    catch (const std::exception& error) { QMessageBox::warning(this, tr("Cannot rename workflow"), error.what()); }
}

void MainWindow::addNode(NodeKind kind) {
    QString task_module;
    if (kind == NodeKind::TaskConfig) {
        SelectTaskModuleDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted) return;
        task_module = dialog.selected_module_id();
        if (task_module.isEmpty()) return;
    }
    const int kind_index = static_cast<int>(std::count_if(graph().nodes().begin(), graph().nodes().end(),
        [kind](const auto& item) { return item.second.kind == kind; }));
    const int id = kind == NodeKind::TaskConfig
        ? graph().add_task_node({task_module.toStdString()})
        : graph().add_node(kind);
    const auto& descriptor = node_descriptor(kind);
    positions()[id] = descriptor.initial_position + QPointF(0, kind_index * descriptor.vertical_step);
    scene_->rebuild();
    QGraphicsItem* added_item = nullptr;
    for (auto* item : scene_->items()) {
        if (item->data(0).toInt() == id) {
            item->setSelected(true);
            added_item = item;
            break;
        }
    }
    if (added_item) view_->ensureVisible(added_item, 48, 48);
    markWorkflowChanged();
}

void MainWindow::clearWorkflow() {
    if (!graph().nodes().empty() &&
        QMessageBox::question(this, tr("Clear workflow"), tr("Remove all nodes from the active workflow?")) != QMessageBox::Yes) return;
    unbindWorkflowViews();
    project_.active_workflow().document = WorkflowDocument{};
    project_.mark_active_dirty();
    bindActiveWorkflow();
}

void MainWindow::loadExampleWorkflow() {
    if (!graph().nodes().empty() &&
        QMessageBox::question(this, tr("Load example"), tr("Replace the active workflow with the example?")) != QMessageBox::Yes) return;
    unbindWorkflowViews();
    project_.active_workflow().document = WorkflowDocument{};
    auto& active_graph = graph();
    const int dataset = active_graph.add_node(NodeKind::DatasetInput);
    const int task = active_graph.add_task_node({"cls"});
    active_graph.connect(dataset, 0, task, 0);
    positions()[dataset] = QPointF(-300, -40);
    positions()[task] = QPointF(80, -40);
    project_.mark_active_dirty();
    bindActiveWorkflow();
    statusBar()->showMessage(tr("Example loaded; resource paths remain intentionally unconfigured"));
}

void MainWindow::deleteSelected() {
    if (!scene_->delete_selected_items()) statusBar()->showMessage(tr("Select a node or connection to delete"), 4000);
}

void MainWindow::showProperties() {
    const int id = scene_->selected_node_id();
    if (id != 0 && graph().nodes().count(id) != 0) {
        property_editor_->edit_node(&graph(), id, [this] { markWorkflowChanged(); });
    } else {
        property_editor_->show_workflow(project_.metadata().name, project_.active_workflow().name,
                                        graph().nodes().size(), graph().connections().size());
    }
}

void MainWindow::refreshValidation(bool report_to_log) {
    issues_ = graph().validate_for_execution();
    scene_->refresh_validation(issues_);
    run_action_->setEnabled(issues_.empty());
    statusBar()->clearMessage();
    if (!issues_.empty()) {
        if (status_context_)
            status_context_->setText(tr("Needs review: %1").arg(issue_text(issues_.front())));
        if (report_to_log) {
            log_->appendPlainText(tr("Workflow validation failed:"));
            for (const auto& issue : issues_) log_->appendPlainText("- " + issue_text(issue));
            output_tabs_->setCurrentWidget(log_);
            output_dock_->show();
        }
    }
}

void MainWindow::markWorkflowChanged() {
    project_.mark_active_dirty();
    for (const auto& [id, unused] : graph().nodes()) {
        graph().set_status(id, NodeStatus::Idle);
        scene_->refresh_node(id);
    }
    refreshValidation();
    refreshProjectUi();
    if (scene_->selected_node_id() == 0) showProperties();
}

void MainWindow::setEditingEnabled(bool enabled) {
    for (auto* action : editing_actions_) action->setEnabled(enabled);
    project_dock_->setEnabled(enabled);
    library_dock_->setEnabled(enabled);
    property_dock_->setEnabled(enabled);
    node_workspace_->setEnabled(enabled);
    view_->setInteractive(enabled);
    if (!enabled) run_action_->setEnabled(false);
    setRuntimeStatus(enabled ? tr("Ready") : tr("Running"), enabled ? "ready" : "running");
}

void MainWindow::setRuntimeStatus(const QString& text, const char* state) {
    if (!status_runtime_) return;
    status_runtime_->setText(text);
    status_runtime_->setProperty("status", state);
    status_runtime_->style()->unpolish(status_runtime_);
    status_runtime_->style()->polish(status_runtime_);
}

void MainWindow::runWorkflow() {
    refreshValidation(true);
    if (!issues_.empty()) return;
    const auto task = std::find_if(
        graph().nodes().begin(), graph().nodes().end(),
        [](const auto& value) {
            return value.second.kind == NodeKind::TaskConfig;
        });
    if (task == graph().nodes().end()) return;
    openNodeWorkspace(task->first);
    statusBar()->showMessage(
        tr("Task workspace opened. Configure Training or Inference to run."), 5000);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelected(); event->accept(); return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (!workspace_stack_ || workspace_stack_->currentWidget() != workflow_workspace_ || !property_dock_) return;
    if (event->size().width() < 1200) property_dock_->hide();
    else if (event->oldSize().width() < 1200) property_dock_->show();
    if (event->oldSize().isValid() &&
        (event->size().width() < event->oldSize().width() ||
         event->size().height() < event->oldSize().height())) {
        QTimer::singleShot(0, this, [this] {
            if (workspace_stack_->currentWidget() != workflow_workspace_) return;
            const QRectF bounds = node_bounds(scene_);
            if (bounds.isEmpty()) return;
            const QRectF visible =
                view_->mapToScene(view_->viewport()->rect()).boundingRect();
            if (!visible.contains(bounds))
                fit_graph_down_only(
                    view_, bounds.adjusted(-70, -70, 70, 70));
        });
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (training_job_.is_running() || inference_job_.is_running()) {
        QMessageBox::information(
            this, tr("Task running"),
            tr("Wait for the current training or inference run to finish before closing."));
        event->ignore(); return;
    }
    if (!confirmProjectTransition()) { event->ignore(); return; }
    QSettings settings("Jocelin", "DeepLearningStudio");
    settings.setValue("shell/geometry", saveGeometry());
    settings.setValue("shell/docks", saveState());
    event->accept();
}

} // namespace studio
