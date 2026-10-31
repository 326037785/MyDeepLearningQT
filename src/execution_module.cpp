#include "execution_module.h"
#include "json_file_writer.h"
#include "classification_annotation_state.h"
#include "model_spec_catalog.h"
#include "sample_presentation.h"
#include "task_workspace_service.h"
#include "task_workspace_pages.h"
#include <checkpoint/checkpoint_catalog.h>
#include <common/model/model_weight_matcher.h>
#include <runtime/device_service.h>
#include <runtime/inference_route.h>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QSettings>
#include <algorithm>
#include <stdexcept>

namespace studio {
namespace {
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

} // namespace

ExecutionModule::ExecutionModule(ProjectManager& project, NodeWorkspace& workspace, QWidget* parent)
    : QObject(parent), project_(project), workspace_(workspace), dialog_parent_(parent) {
    workspace_.training_launch_requested = [this](int id, const TrainingLaunchRequest& request) { start_training(id, request); };
    workspace_.infer_requested = [this](int id, const InferenceLaunchRequest& request) { start_inference(id, request); };
    workspace_.stop_requested = [this] { stop(); };
    workspace_.pause_requested = [this] { pause(); };
    training_job_.progress = [this](const jocelin::train::TrainingProgress& progress) { training_progress(progress); };
    training_job_.log = [this](const QString& message) { training_log(message); };
    training_job_.warning = [this](const QString& message) { training_warning(message); };
    training_job_.completed = [this](const jocelin::training::TrainingResult& result, const QString& error) { training_completed(result, error); };
    inference_job_.progress = [this](size_t done, size_t total, const QString& image) { inference_progress(done, total, image); };
    inference_job_.completed = [this](const InferenceOutcome& outcome) { inference_completed(outcome); };
}

WorkflowGraph& ExecutionModule::graph() { return project_.active_workflow().document.graph; }
bool ExecutionModule::is_running() const {
    // Keep the operation locked through startup and the queued completion callback.
    return active_training_node_id_ != 0 || active_inference_node_id_ != 0 ||
        training_job_.is_running() || inference_job_.is_running();
}
void ExecutionModule::start_training(int node_id) {
    TrainingLaunchRequest request;
    request.mode = "new";
    start_training(node_id, request);
}
void ExecutionModule::stop() { training_job_.request_stop(); }
void ExecutionModule::pause() { training_job_.request_pause(); }
void ExecutionModule::message(const QString& text, int timeout) { status(text, timeout); }

void ExecutionModule::training_progress(const jocelin::train::TrainingProgress& progress) {
    if (auto* model = TaskWorkspaceService(graph(), active_training_node_id_).find_model(active_training_model_id_)) {
        model->completed_epochs = progress.completed_epochs;
        model->training_loss = progress.loss;
        model->metric_name = progress.validation_metric_name;
        model->metric = progress.validation_metric;
        project_.mark_active_dirty();
    }
    if (active_training_node_id_ != 0 &&
        graph().nodes().count(active_training_node_id_) != 0)
        workspace_.update_training_progress(active_training_node_id_, progress);
    message(tr("Training epoch %1 / %2").arg(progress.completed_epochs).arg(progress.total_epochs));
}

void ExecutionModule::training_log(const QString& message) {
    log("[train] " + message);
    append_log_file(active_training_log_path_, message);
}

void ExecutionModule::training_warning(const QString& message) {
    auto* warning = new QMessageBox(QMessageBox::Warning, tr("Training device fallback"),
                                   message, QMessageBox::Ok, dialog_parent_);
    warning->setAttribute(Qt::WA_DeleteOnClose);
    warning->open();
}

void ExecutionModule::training_completed(const jocelin::training::TrainingResult& result, const QString& error) {
    auto* model = TaskWorkspaceService(graph(), active_training_node_id_).find_model(active_training_model_id_);
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
        const bool stopped = error.isEmpty() &&
            result.end_state == jocelin::training::TrainingEndState::Stopped;
        const std::string weight_path = paused
            ? result.last_model_path
            : (completed || stopped) ? result.best_model_path : std::string{};
        if ((paused || completed || stopped) && !weight_path.empty() &&
            QFileInfo(QString::fromStdString(weight_path)).isFile()) {
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
        project_.mark_active_dirty();
    }
    workspace_.set_runtime_busy(false);
    if (active_training_node_id_ != 0 &&
        graph().nodes().count(active_training_node_id_) != 0) {
        workspace_.finish_training(
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
        log(completion);
        append_log_file(active_training_log_path_, completion);
    }
    else {
        log(tr("Training failed: %1").arg(error));
        append_log_file(
            active_training_log_path_,
            tr("Training failed: %1").arg(error));
    }
    try {
        if (project_.has_storage()) project_.save_project();
    } catch (const std::exception& catalog_error) {
        const QString message =
            tr("ModelList could not be persisted: %1")
                .arg(QString::fromUtf8(catalog_error.what()));
        log(message);
        append_log_file(active_training_log_path_, message);
        show_log();
    }
    workspace_.refresh_models();
    if (!error.isEmpty()) { show_log(); }
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
    editing_changed(true);
    if (!error.isEmpty()) runtime_status(tr("Error"), "error");
    changed();
}

void ExecutionModule::inference_progress(size_t done, size_t total, const QString& image) {
    workspace_.update_inference_progress(
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
    message(tr("Inference %1 / %2: %3")
        .arg(done)
        .arg(total)
        .arg(display_name));
}

void ExecutionModule::inference_completed(const InferenceOutcome& outcome) {
    if (outcome.fallback.occurred) {
        const QString message = tr(
            "Inference route fallback: requested %1 on %2, effective %3 on %4. %5")
            .arg(
                QString::fromStdString(jocelin::config::to_string(outcome.fallback.requested_weight_type)),
                QString::fromStdString(outcome.fallback.requested_device.id),
                QString::fromStdString(jocelin::config::to_string(outcome.fallback.effective_weight_type)),
                QString::fromStdString(outcome.fallback.effective_device.id),
                QString::fromStdString(outcome.fallback.reason));
        log(message);
        show_log();
        auto* warning = new QMessageBox(QMessageBox::Warning, tr("Inference device fallback"),
                                       message, QMessageBox::Ok, dialog_parent_);
        warning->setAttribute(Qt::WA_DeleteOnClose);
        warning->open();
    }
    QString error = outcome.error;
    if (error.isEmpty()) {
        try {
            project_.commit_inference_result(active_inference_node_id_, active_inference_model_id_,
                active_inference_weight_id_, outcome.report, active_inference_staging_);
        } catch (const std::exception& failure) {
            error = QString::fromUtf8(failure.what());
        }
    }
    if (!error.isEmpty() && graph().nodes().count(active_inference_node_id_)) {
        TaskWorkspaceService(graph(), active_inference_node_id_).task().runtime.inference_status = active_inference_previous_status_;
        project_.active_workflow().dirty = false;
    }
    try {
        project_.discard_inference_directory(active_inference_node_id_, active_inference_model_id_,
            active_inference_weight_id_, active_inference_staging_);
    } catch (const std::exception& failure) {
        log(QString::fromUtf8(failure.what()));
    }
    workspace_.finish_inference(active_inference_node_id_, error);
    workspace_.set_runtime_busy(false);
    if (error.isEmpty()) {
        log(outcome.summary);
        message(tr("Inference completed"));
    }
    else {
        log(tr("Inference failed: %1").arg(error));
        show_log();
    }
    active_inference_node_id_ = 0;
    active_inference_model_id_.clear();
    active_inference_weight_id_.clear();
    active_inference_staging_.clear();
    active_inference_previous_status_.clear();
    editing_changed(true);
    if (!error.isEmpty()) runtime_status(tr("Error"), "error");
    changed();
}

void ExecutionModule::start_training(
    int node_id, const TrainingLaunchRequest& launch)
{
    if (is_running()) return;
    bool created_model_row = false;
    bool resumed_model_row = false;
    try {
        if (graph().nodes().count(node_id) == 0 || graph().node(node_id).kind != NodeKind::TaskConfig)
            throw std::runtime_error("training must start from a Task Config node");
        TaskWorkspaceService service(graph(), node_id);
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
            parent = service.find_model(launch.model_id);
            if (!parent)
                throw std::runtime_error(
                    "selected Task model no longer exists");
            if (parent->training_status != jocelin::model::ModelTrainingStatus::Completed &&
                parent->training_status != jocelin::model::ModelTrainingStatus::Imported)
                throw std::runtime_error(
                    "selected Task model training is not complete");
            parent_training_weight = jocelin::model::find_training_weight(*parent);
            if (!parent_training_weight ||
                !QFileInfo::exists(QString::fromStdString(parent_training_weight->path)))
                throw std::runtime_error(
                    "selected Task model has no available LibTorch training weight");
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
            resume_model = service.find_model(launch.model_id);
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
        if (!select_compute_device(dialog_parent_, &device)) return;
        task.device = device.device;
        if (task.training.output_directory.empty()) {
            if (!project_.has_storage())
                throw std::runtime_error(
                    "save the project or configure a model output directory before training");
            task.training.output_directory =
                project_.node_models_directory(node_id).toStdString();
        }
        if (!QDir().mkpath(QString::fromStdString(task.training.output_directory)))
            throw std::runtime_error("cannot create the training output directory");

        const int run_number = project_.training_runs().allocate();
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
            device.device, std::move(weight_source),
            launch.mode == "resume" ? resume_model : nullptr);
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
            auto* row = service.find_model(launch.model_id);
            row->training_status = jocelin::model::ModelTrainingStatus::Running;
            project_.mark_active_dirty();
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
            if (tool_type == jocelin::config::ToolType::Cls) {
                for (const auto* value : classification_classes(service.binding().classes))
                    model.classes.push_back({value->id, value->name, 0});
            } else {
                for (const auto& value : service.binding().classes) {
                    if (!value.active) continue;
                    model.classes.push_back({value.id, value.name, value.mask_value});
                }
                std::sort(model.classes.begin(), model.classes.end(),
                    [](const auto& left, const auto& right) {
                        return left.mask_value < right.mask_value;
                    });
            }
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
            const auto added = service.add_model(std::move(model));
            active_training_model_id_ = QString::fromStdString(added.id);
            created_model_row = true;
        }
        service.record_training_started(active_training_model_id_.toStdString());
        active_training_node_id_ = node_id;
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
        editing_changed(false);
        workspace_.setEnabled(true);
        workspace_.training_started(node_id);
        workspace_.set_runtime_busy(true);
        const QString start_message =
            tr("Training %1 started on %2 路 source %3")
                .arg(run_id,
                     QString::fromStdString(device.display_name),
                     launch.mode);
        log(start_message);
        append_log_file(active_training_log_path_, start_message);
        training_job_.start(request);
    } catch (const std::exception& error) {
        if (!training_job_.is_running()) {
            if (created_model_row && !active_training_model_id_.isEmpty()) {
                TaskWorkspaceService(graph(), node_id).remove_model(active_training_model_id_);
                if (graph().nodes().count(node_id) != 0) {
                    TaskWorkspaceService service(graph(), node_id);
                    service.task().runtime.training_model_id.clear();
                    project_.mark_active_dirty();
                }
            } else if (resumed_model_row) {
                if (auto* row = TaskWorkspaceService(graph(), node_id).find_model(active_training_model_id_)) {
                    row->training_status = jocelin::model::ModelTrainingStatus::Paused;
                    project_.mark_active_dirty();
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
            editing_changed(true);
            workspace_.set_runtime_busy(false);
        }
        QMessageBox::critical(dialog_parent_, tr("Cannot start training"), QString::fromUtf8(error.what()));
    }
}

void ExecutionModule::start_inference(
    int node_id,
    const InferenceLaunchRequest& launch)
{
    if (is_running()) return;
    try {
        if (graph().nodes().count(node_id) == 0 || graph().node(node_id).kind != NodeKind::TaskConfig)
            throw std::runtime_error("inference must start from a Task Config node");
        if (launch.model_id.isEmpty())
            throw std::runtime_error("select a Task model before starting inference");
        if (launch.weight_id.isEmpty())
            throw std::runtime_error("select an inference weight before starting inference");

        TaskWorkspaceService service(graph(), node_id);
        service.synchronize();
        if (!project_.has_storage()) throw std::runtime_error("save the project before inference");
        project_.save_project();
        auto& task = service.task();
        const auto tool_type = jocelin::config::tool_type_from_string(task.task);
        const QString model_id = launch.model_id;
        const auto* model = service.find_model(model_id);
        if (!model)
            throw std::runtime_error("selected Task model no longer exists");
        if (model->task != tool_type)
            throw std::runtime_error("selected Task model belongs to a different task repository");

        const auto* inference_weight = jocelin::model::find_weight(
            *model, launch.weight_id.toStdString());
        if (!inference_weight)
            throw std::runtime_error("selected inference weight no longer exists");
        active_inference_previous_status_ = task.runtime.inference_status;
        active_inference_node_id_ = node_id;
        active_inference_model_id_ = model_id;
        active_inference_weight_id_ = launch.weight_id;
        active_inference_staging_ = project_.prepare_inference_directory(node_id, model_id, launch.weight_id);
        auto request = service.build_inference_request(
            launch.device, *model, *inference_weight, active_inference_staging_.toStdString());
        service.record_inference_started();
        editing_changed(false);
        workspace_.setEnabled(true);
        active_inference_node_id_ = node_id;
        active_inference_model_id_ = model_id;
        active_inference_weight_id_ = QString::fromStdString(inference_weight->id);
        project_.mark_active_dirty();
        workspace_.inference_started(node_id);
        workspace_.set_runtime_busy(true);
        log(
            tr("Inference started with %1 on %2")
                .arg(QString::fromStdString(model->name),
                     QString::fromStdString(launch.device.id)));
        inference_job_.start(request);
    } catch (const std::exception& error) {
        if (!inference_job_.is_running()) {
            if (!active_inference_staging_.isEmpty()) {
                try {
                    project_.discard_inference_directory(node_id, active_inference_model_id_, active_inference_weight_id_, active_inference_staging_);
                } catch (const std::exception& cleanup_error) {
                    log(QString::fromUtf8(cleanup_error.what()));
                }
                TaskWorkspaceService(graph(), node_id).task().runtime.inference_status = active_inference_previous_status_;
                project_.active_workflow().dirty = false;
            }
            active_inference_node_id_ = 0;
            active_inference_model_id_.clear();
            active_inference_weight_id_.clear();
            active_inference_staging_.clear();
            active_inference_previous_status_.clear();
            editing_changed(true);
            workspace_.set_runtime_busy(false);
        }
        QMessageBox::critical(dialog_parent_, tr("Cannot start inference"), QString::fromUtf8(error.what()));
    }
}

} // namespace studio
