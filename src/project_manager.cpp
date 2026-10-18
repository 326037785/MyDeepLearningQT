#include "project_manager.h"
#include "project_path_validator.h"
#include "project_storage.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace studio {

namespace {

constexpr auto ProjectFileName = "project.json";

void require_exact_fields(
    const QJsonObject& object,
    std::initializer_list<const char*> fields,
    const QString& context)
{
    std::set<QString> expected;
    for (const char* field : fields) {
        const QString name = QString::fromLatin1(field);
        expected.insert(name);
        if (!object.contains(name))
            throw std::runtime_error(
                (context + ": missing field '" +
                 name + "'").toStdString());
    }
    for (auto iterator = object.begin();
         iterator != object.end(); ++iterator) {
        if (expected.count(iterator.key()) == 0)
            throw std::runtime_error(
                (context + ": unexpected field '" +
                 iterator.key() + "'").toStdString());
    }
}

QString validated_relative_path(const QString& value, const char* label) {
    const QString relative = QDir::cleanPath(QDir::fromNativeSeparators(value));
    if (relative.isEmpty() || relative == "." || QFileInfo(relative).isAbsolute() ||
        relative == ".." || relative.startsWith("../"))
        throw std::runtime_error(std::string("invalid ") + label + " path");
    return relative;
}

QString portable_path(const std::string& value, const QString& root_directory) {
    if (value.empty()) return {};
    const QString path = QDir::cleanPath(QString::fromStdString(value));
    if (path.startsWith("project:"))
        throw std::runtime_error("runtime path must not contain a serialized project reference");
    if (!QFileInfo(path).isAbsolute()) return "project:" + validated_relative_path(path, "resource");
    const QString relative = QDir(root_directory).relativeFilePath(path);
    if (relative != ".." && !relative.startsWith("../") && !QFileInfo(relative).isAbsolute())
        return "project:" + QDir::fromNativeSeparators(relative);
    return QDir::toNativeSeparators(path);
}

std::string relocated_path(
    const std::string& value,
    const QString& source_root,
    const QString& destination_root)
{
    if (value.empty() || source_root.isEmpty()) return value;
    const QString path = QDir::cleanPath(QString::fromStdString(value));
    if (!QFileInfo(path).isAbsolute()) return value;
    const QString relative = QDir(source_root).relativeFilePath(path);
    if (relative == ".." || relative.startsWith("../") || QFileInfo(relative).isAbsolute())
        return value;
    return QDir::toNativeSeparators(
               QDir(destination_root).absoluteFilePath(relative)).toStdString();
}

void relocate_graph_paths(
    WorkflowGraph& graph,
    const QString& source_root,
    const QString& destination_root)
{
    for (const auto& [id, node] : graph.nodes()) {
        auto& mutable_node = graph.node(id);
        if (node.kind == NodeKind::DatasetInput) {
            auto& config = std::get<DatasetConfig>(mutable_node.config);
            config.directory = relocated_path(config.directory, source_root, destination_root);
            for (auto& item : config.items)
                item.path = relocated_path(item.path, source_root, destination_root);
        } else {
            auto& config = std::get<TaskNodeConfig>(mutable_node.config);
            config.training.output_directory = relocated_path(config.training.output_directory, source_root, destination_root);
            config.inference_output_directory = relocated_path(config.inference_output_directory, source_root, destination_root);
            for (auto& sample : config.dataset_binding.samples) {
                for (auto& path : sample.image_paths)
                    path = relocated_path(path, source_root, destination_root);
                sample.mask_path = relocated_path(sample.mask_path, source_root, destination_root);
                sample.training_mask_path = relocated_path(
                    sample.training_mask_path, source_root, destination_root);
            }
            for (auto& result_set : config.runtime.results) {
                result_set.result_directory = relocated_path(
                    result_set.result_directory, source_root, destination_root);
                for (auto& result : result_set.samples) {
                    std::visit([&](auto& task_result) {
                        task_result.image_path = relocated_path(task_result.image_path, source_root, destination_root);
                        task_result.save_dir = relocated_path(task_result.save_dir, source_root, destination_root);
                        task_result.metadata_path = relocated_path(task_result.metadata_path, source_root, destination_root);
                        using Result = std::decay_t<decltype(task_result)>;
                        if constexpr (std::is_same_v<Result, jocelin::result::sg::SgResult>) {
                            task_result.mask_path = relocated_path(task_result.mask_path, source_root, destination_root);
                            task_result.visualization_path = relocated_path(task_result.visualization_path, source_root, destination_root);
                        } else {
                            task_result.heatmap_path = relocated_path(task_result.heatmap_path, source_root, destination_root);
                        }
                    }, result.result);
                }
            }
        }
    }
}

std::string runtime_path(const QString& value, const QString& root_directory) {
    if (!value.startsWith("project:")) return QDir::toNativeSeparators(value).toStdString();
    const QString relative = validated_relative_path(value.mid(8), "project-relative resource");
    return QDir::toNativeSeparators(QDir(root_directory).absoluteFilePath(relative)).toStdString();
}

WorkflowGraph portable_graph(const WorkflowGraph& source, const QString& root_directory) {
    WorkflowGraph graph = source;
    for (const auto& [id, node] : graph.nodes()) {
        auto& mutable_node = graph.node(id);
        if (node.kind == NodeKind::DatasetInput) {
            auto& config = std::get<DatasetConfig>(mutable_node.config);
            config.directory = portable_path(config.directory, root_directory).toStdString();
            for (auto& item : config.items)
                item.path = portable_path(item.path, root_directory).toStdString();
        } else {
            auto& config = std::get<TaskNodeConfig>(mutable_node.config);
            config.training.output_directory = portable_path(config.training.output_directory, root_directory).toStdString();
            config.inference_output_directory = portable_path(config.inference_output_directory, root_directory).toStdString();
            for (auto& sample : config.dataset_binding.samples) {
                for (auto& image_path : sample.image_paths)
                    image_path = portable_path(image_path, root_directory).toStdString();
                sample.mask_path = portable_path(sample.mask_path, root_directory).toStdString();
                sample.training_mask_path = portable_path(
                    sample.training_mask_path, root_directory).toStdString();
            }
            for (auto& result_set : config.runtime.results) {
                result_set.result_directory = portable_path(
                    result_set.result_directory, root_directory).toStdString();
                for (auto& result : result_set.samples) {
                    std::visit([&](auto& task_result) {
                        task_result.image_path = portable_path(task_result.image_path, root_directory).toStdString();
                        task_result.save_dir = portable_path(task_result.save_dir, root_directory).toStdString();
                        task_result.metadata_path = portable_path(task_result.metadata_path, root_directory).toStdString();
                        using Result = std::decay_t<decltype(task_result)>;
                        if constexpr (std::is_same_v<Result, jocelin::result::sg::SgResult>) {
                            task_result.mask_path = portable_path(task_result.mask_path, root_directory).toStdString();
                            task_result.visualization_path = portable_path(task_result.visualization_path, root_directory).toStdString();
                        } else {
                            task_result.heatmap_path = portable_path(task_result.heatmap_path, root_directory).toStdString();
                        }
                    }, result.result);
                }
            }
        }
    }
    return graph;
}

void resolve_graph_paths(WorkflowGraph& graph, const QString& root_directory) {
    for (const auto& [id, node] : graph.nodes()) {
        auto& mutable_node = graph.node(id);
        if (node.kind == NodeKind::DatasetInput) {
            auto& config = std::get<DatasetConfig>(mutable_node.config);
            config.directory = runtime_path(QString::fromStdString(config.directory), root_directory);
            for (auto& item : config.items)
                item.path = runtime_path(QString::fromStdString(item.path), root_directory);
        } else {
            auto& config = std::get<TaskNodeConfig>(mutable_node.config);
            config.training.output_directory = runtime_path(QString::fromStdString(config.training.output_directory), root_directory);
            config.inference_output_directory = runtime_path(QString::fromStdString(config.inference_output_directory), root_directory);
            for (auto& sample : config.dataset_binding.samples) {
                for (auto& image_path : sample.image_paths)
                    image_path = runtime_path(QString::fromStdString(image_path), root_directory);
                sample.mask_path = runtime_path(QString::fromStdString(sample.mask_path), root_directory);
                sample.training_mask_path = runtime_path(
                    QString::fromStdString(sample.training_mask_path), root_directory);
            }
            for (auto& result_set : config.runtime.results) {
                result_set.result_directory = runtime_path(
                    QString::fromStdString(result_set.result_directory), root_directory);
                for (auto& result : result_set.samples) {
                    std::visit([&](auto& task_result) {
                        task_result.image_path = runtime_path(QString::fromStdString(task_result.image_path), root_directory);
                        task_result.save_dir = runtime_path(QString::fromStdString(task_result.save_dir), root_directory);
                        task_result.metadata_path = runtime_path(QString::fromStdString(task_result.metadata_path), root_directory);
                        using Result = std::decay_t<decltype(task_result)>;
                        if constexpr (std::is_same_v<Result, jocelin::result::sg::SgResult>) {
                            task_result.mask_path = runtime_path(QString::fromStdString(task_result.mask_path), root_directory);
                            task_result.visualization_path = runtime_path(QString::fromStdString(task_result.visualization_path), root_directory);
                        } else {
                            task_result.heatmap_path = runtime_path(QString::fromStdString(task_result.heatmap_path), root_directory);
                        }
                    }, result.result);
                }
            }
        }
    }
}

} // namespace

ProjectManager::ProjectManager() { new_project(); }

void ProjectManager::new_project(const QString& name) {
    if (name.trimmed().isEmpty()) throw std::runtime_error("project name cannot be empty");
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    metadata_ = {};
    metadata_.project_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    metadata_.name = name.trimmed();
    metadata_.created_at = now;
    metadata_.modified_at = now;
    workflows_.clear();
    model_list_.restore({}, 1);
    next_training_run_number_ = 1;
    ProjectWorkflow workflow;
    workflow.id = "workflow_1";
    workflow.name = "Workflow 1";
    workflows_.push_back(std::move(workflow));
    active_index_ = 0;
    metadata_dirty_ = false;
}

void ProjectManager::open_project(const QString& project_file_path) {
    QFileInfo info(project_file_path);
    const QString file_path = info.isDir() ? QDir(project_file_path).filePath(ProjectFileName) : info.absoluteFilePath();
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(ProjectStorage::read(file_path), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error("invalid project JSON: " + parse_error.errorString().toStdString());
    const QJsonObject root = document.object();
    require_exact_fields(
        root,
        {"format", "project_id", "name", "description",
         "created_at", "modified_at", "derived_from", "active_workflow",
         "workflows", "models", "next_model_number", "next_training_run_number"},
        "Project document");
    if (root.value("format").toString() != "deep_learning_project")
        throw std::runtime_error("unsupported project format");
    if (!root.value("name").isString() || root.value("name").toString().trimmed().isEmpty() ||
        !root.value("description").isString() || !root.value("created_at").isString() ||
        !root.value("active_workflow").isString() || !root.value("workflows").isArray())
        throw std::runtime_error("project metadata is incomplete");

    ProjectMetadata metadata;
    metadata.project_id = root.value("project_id").toString();
    metadata.name = root.value("name").toString().trimmed();
    metadata.description = root.value("description").toString();
    metadata.created_at = root.value("created_at").toString();
    metadata.modified_at = root.value("modified_at").toString();
    metadata.root_directory = QFileInfo(file_path).absolutePath();
    if (metadata.project_id.isEmpty() ||
        !QDateTime::fromString(metadata.modified_at, Qt::ISODate).isValid() ||
        !root.value("derived_from").isObject())
        throw std::runtime_error("project version metadata is invalid");
    const auto derived = root.value("derived_from").toObject();
    require_exact_fields(derived, {"project_id", "project_path"}, "Project derivation");
    metadata.derived_from_project_id = derived.value("project_id").toString();
    metadata.derived_from_project_path = derived.value("project_path").toString();
    if (!QDateTime::fromString(metadata.created_at, Qt::ISODate).isValid())
        throw std::runtime_error("project creation timestamp is invalid");
    std::vector<ProjectWorkflow> workflows;
    const QString active_id = root.value("active_workflow").toString();
    size_t active_index = static_cast<size_t>(-1);
    for (const auto value : root.value("workflows").toArray()) {
        if (!value.isObject()) throw std::runtime_error("invalid project workflow entry");
        const auto object = value.toObject();
        require_exact_fields(
            object, {"id", "name", "file"},
            "Project workflow entry");
        const QString id = object.value("id").toString();
        const QString name = object.value("name").toString().trimmed();
        const QString relative_file = object.value("file").toString();
        if (!QRegularExpression("^[A-Za-z0-9_-]+$").match(id).hasMatch() || name.isEmpty() || relative_file.isEmpty())
            throw std::runtime_error("invalid project workflow metadata");
        const QString safe_relative_file = validated_relative_path(relative_file, "workflow file");
        const bool duplicate = std::any_of(workflows.begin(), workflows.end(), [&id](const auto& item) { return item.id == id; });
        if (duplicate) throw std::runtime_error("duplicate workflow id in project");
        QFile workflow_file(QDir(metadata.root_directory).filePath(safe_relative_file));
        if (!workflow_file.open(QIODevice::ReadOnly))
            throw std::runtime_error(("cannot open workflow " + name).toStdString());
        ProjectWorkflow workflow{id, name, parse_workflow(workflow_file.readAll()), false};
        resolve_graph_paths(workflow.document.graph, metadata.root_directory);
        if (id == active_id) active_index = workflows.size();
        workflows.push_back(std::move(workflow));
    }
    if (workflows.empty() || active_index == static_cast<size_t>(-1))
        throw std::runtime_error("project has no valid active workflow");

    if (!root.value("next_training_run_number").isDouble())
        throw std::runtime_error("next training run number is invalid");
    const int next_training_run_number = root.value("next_training_run_number").toInt();
    if (next_training_run_number < 1)
        throw std::runtime_error("next training run number must be positive");

    std::vector<ProjectModel> models;
    int next_model_number = 1;
    {
        if (!root.value("models").isArray() ||
            !root.value("next_model_number").isDouble())
            throw std::runtime_error("project model catalog is invalid");
        next_model_number = root.value("next_model_number").toInt();
        if (next_model_number < 1)
            throw std::runtime_error("next model number must be positive");
        std::set<QString> model_ids;
        std::set<int> model_numbers;
        for (const auto value : root.value("models").toArray()) {
            if (!value.isObject())
                throw std::runtime_error("invalid project model entry");
            const auto object = value.toObject();
            require_exact_fields(
                object,
                {"id", "number", "name", "training_run_id", "created_at",
                 "task", "model_index", "model_scale", "classes",
                 "image_width", "image_height", "asset_directory", "weights",
                 "completed_epochs", "best_epoch", "training_loss", "metric_name",
                 "metric", "creation_mode", "training_status", "parent_model_id"},
                "Project model entry");
            ProjectModel model;
            model.id = object.value("id").toString().toStdString();
            model.number = object.value("number").toInt();
            model.name = object.value("name").toString().toStdString();
            model.training_run_id = object.value("training_run_id").toString().toStdString();
            model.created_at = object.value("created_at").toString().toStdString();
            model.task = jocelin::config::tool_type_from_string(
                object.value("task").toString().toStdString());
            model.model_index = object.value("model_index").toInt();
            model.model_scale = jocelin::config::model_scale_from_string(
                object.value("model_scale").toString().toStdString());
            model.image_width = object.value("image_width").toInt();
            model.image_height = object.value("image_height").toInt();
            model.asset_directory = runtime_path(
                object.value("asset_directory").toString(), metadata.root_directory);
            if (!object.value("weights").isArray())
                throw std::runtime_error("project model weights are invalid");
            for (const auto weight_value : object.value("weights").toArray()) {
                if (!weight_value.isObject())
                    throw std::runtime_error("invalid project model weight entry");
                const auto weight_object = weight_value.toObject();
                require_exact_fields(
                    weight_object,
                    {"id", "format", "precision", "role", "path",
                     "parent_weight_id", "created_at", "supported_devices",
                     "runtime_name", "runtime_version", "artifact_version",
                     "onnx_opset", "input_signature", "output_signature",
                     "target_architecture"},
                    "Project model weight");
                jocelin::model::ModelWeight weight;
                weight.id = weight_object.value("id").toString().toStdString();
                weight.format = jocelin::config::weight_type_from_string(
                    weight_object.value("format").toString().toStdString());
                weight.precision = jocelin::model::weight_precision_from_string(
                    weight_object.value("precision").toString().toStdString());
                weight.role = jocelin::model::weight_role_from_string(
                    weight_object.value("role").toString().toStdString());
                weight.path = runtime_path(
                    weight_object.value("path").toString(), metadata.root_directory);
                weight.parent_weight_id =
                    weight_object.value("parent_weight_id").toString().toStdString();
                weight.created_at = weight_object.value("created_at").toString().toStdString();
                if (!weight_object.value("supported_devices").isArray())
                    throw std::runtime_error("project model weight devices are invalid");
                for (const auto value : weight_object.value("supported_devices").toArray())
                    weight.supported_device_types.push_back(
                        jocelin::config::device_type_from_string(value.toString().toStdString()));
                weight.runtime_name = weight_object.value("runtime_name").toString().toStdString();
                weight.runtime_version = weight_object.value("runtime_version").toString().toStdString();
                weight.artifact_version = weight_object.value("artifact_version").toString().toStdString();
                weight.onnx_opset = weight_object.value("onnx_opset").toInt();
                weight.input_signature = weight_object.value("input_signature").toString().toStdString();
                weight.output_signature = weight_object.value("output_signature").toString().toStdString();
                weight.target_architecture = weight_object.value("target_architecture").toString().toStdString();
                model.weights.push_back(std::move(weight));
            }
            model.creation_mode = jocelin::model::model_creation_mode_from_string(
                object.value("creation_mode").toString().toStdString());
            model.training_status = jocelin::model::model_training_status_from_string(
                object.value("training_status").toString().toStdString());
            model.completed_epochs = object.value("completed_epochs").toInt();
            model.best_epoch = object.value("best_epoch").toInt();
            model.training_loss = object.value("training_loss").toDouble();
            model.metric_name = object.value("metric_name").toString().toStdString();
            model.metric = object.value("metric").toDouble();
            model.parent_model_id = object.value("parent_model_id").toString().toStdString();
            if (model.id.empty() || model.number < 1 ||
                model.name.empty() || model.training_run_id.empty() ||
                !QDateTime::fromString(QString::fromStdString(model.created_at), Qt::ISODate).isValid() ||
                model.model_index < 0 || model.image_width <= 0 ||
                model.image_height <= 0 || model.completed_epochs < 0 ||
                model.best_epoch < 0 || model.best_epoch > model.completed_epochs ||
                !std::isfinite(model.training_loss) || model.asset_directory.empty() ||
                !model_ids.insert(QString::fromStdString(model.id)).second ||
                !model_numbers.insert(model.number).second)
                throw std::runtime_error("project model metadata is invalid");
            const bool requires_weight =
                model.training_status == jocelin::model::ModelTrainingStatus::Paused ||
                model.training_status == jocelin::model::ModelTrainingStatus::Completed ||
                model.training_status == jocelin::model::ModelTrainingStatus::Imported;
            if ((requires_weight && !jocelin::model::find_training_weight(model)) ||
                ((model.creation_mode == jocelin::model::ModelCreationMode::Import) !=
                 (model.training_status == jocelin::model::ModelTrainingStatus::Imported)))
                throw std::runtime_error("project model lifecycle metadata is invalid");
            if (!object.value("classes").isArray())
                throw std::runtime_error("project model classes are invalid");
            for (const auto class_value : object.value("classes").toArray()) {
                if (!class_value.isObject())
                    throw std::runtime_error("invalid project model class entry");
                const auto class_object = class_value.toObject();
                require_exact_fields(class_object, {"id", "name", "mask_value"}, "Project model class");
                ProjectModelClass item{
                    class_object.value("id").toString().toStdString(),
                    class_object.value("name").toString().toStdString(),
                    class_object.value("mask_value").toInt()};
                if (item.id.empty() || item.name.empty() || item.mask_value <= 0)
                    throw std::runtime_error("project model class metadata is invalid");
                model.classes.push_back(std::move(item));
            }
            models.push_back(std::move(model));
        }
        if (!model_numbers.empty() &&
            next_model_number <= *model_numbers.rbegin())
            throw std::runtime_error(
                "next model number must exceed all assigned numbers");
    }
    metadata_ = std::move(metadata);
    workflows_ = std::move(workflows);
    model_list_.restore(std::move(models), next_model_number);
    next_training_run_number_ = next_training_run_number;
    active_index_ = active_index;
    metadata_dirty_ = false;
}

void ProjectManager::save_project(const QString& root_directory) {
    QString root = root_directory.isEmpty() ? metadata_.root_directory : QDir(root_directory).absolutePath();
    if (root.isEmpty()) throw std::runtime_error("project root directory is not selected");
    if (!QDir().mkpath(QDir(root).filePath("workflows")))
        throw std::runtime_error("cannot create project workflows directory");

    QJsonArray workflow_entries;
    std::vector<std::pair<QString, QByteArray>> serialized_workflows;
    for (const auto& workflow : workflows_) {
        const QString relative_file = "workflows/" + workflow.id + ".json";
        const auto graph = portable_graph(workflow.document.graph, root);
        serialized_workflows.emplace_back(
            QDir(root).filePath(relative_file), serialize_workflow(graph, workflow.document.positions));
        workflow_entries.append(QJsonObject{{"id", workflow.id}, {"name", workflow.name}, {"file", relative_file}});
    }
    QJsonArray model_entries;
    for (const auto& model : model_list_.entries()) {
        QJsonArray classes;
        for (const auto& value : model.classes)
            classes.append(QJsonObject{
                {"id", QString::fromStdString(value.id)},
                {"name", QString::fromStdString(value.name)},
                {"mask_value", value.mask_value}});
        QJsonArray weights;
        for (const auto& weight : model.weights) {
            QJsonArray supported_devices;
            const auto devices = weight.supported_device_types.empty()
                ? jocelin::model::default_supported_devices(weight.format)
                : weight.supported_device_types;
            for (const auto type : devices)
                supported_devices.append(QString::fromStdString(jocelin::config::to_string(type)));
            weights.append(QJsonObject{
                {"id", QString::fromStdString(weight.id)},
                {"format", QString::fromStdString(jocelin::config::to_string(weight.format))},
                {"precision", QString::fromStdString(jocelin::model::to_string(weight.precision))},
                {"role", QString::fromStdString(jocelin::model::to_string(weight.role))},
                {"path", portable_path(weight.path, root)},
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
        model_entries.append(QJsonObject{
            {"id", QString::fromStdString(model.id)},
            {"number", model.number},
            {"name", QString::fromStdString(model.name)},
            {"training_run_id", QString::fromStdString(model.training_run_id)},
            {"created_at", QString::fromStdString(model.created_at)},
            {"task", QString::fromStdString(jocelin::config::to_string(model.task))},
            {"model_index", model.model_index},
            {"model_scale", QString::fromStdString(jocelin::config::to_string(model.model_scale))},
            {"classes", classes},
            {"image_width", model.image_width},
            {"image_height", model.image_height},
            {"asset_directory", portable_path(model.asset_directory, root)},
            {"weights", weights},
            {"completed_epochs", model.completed_epochs},
            {"best_epoch", model.best_epoch},
            {"training_loss", model.training_loss},
            {"metric_name", QString::fromStdString(model.metric_name)},
            {"metric", model.metric},
            {"creation_mode", QString::fromStdString(jocelin::model::to_string(model.creation_mode))},
            {"training_status", QString::fromStdString(jocelin::model::to_string(model.training_status))},
            {"parent_model_id", QString::fromStdString(model.parent_model_id)}});
    }
    const QString modified_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QJsonObject project{
        {"format", "deep_learning_project"},
        {"project_id", metadata_.project_id},
        {"name", metadata_.name},
        {"description", metadata_.description},
        {"created_at", metadata_.created_at},
        {"modified_at", modified_at},
        {"derived_from", QJsonObject{
             {"project_id", metadata_.derived_from_project_id},
             {"project_path", metadata_.derived_from_project_path}}},
        {"active_workflow", active_workflow().id},
        {"workflows", workflow_entries},
        {"models", model_entries},
        {"next_model_number", model_list_.next_number()},
        {"next_training_run_number", next_training_run_number_}};
    for (const auto& [path, data] : serialized_workflows)
        ProjectStorage::write_atomic(path, data);
    ProjectStorage::write_atomic(
        QDir(root).filePath(ProjectFileName),
        QJsonDocument(project).toJson(QJsonDocument::Indented));
    metadata_.modified_at = modified_at;
    metadata_.root_directory = root;
    metadata_dirty_ = false;
    for (auto& workflow : workflows_) {
        auto normalized = portable_graph(workflow.document.graph, root);
        resolve_graph_paths(normalized, root);
        workflow.document.graph = std::move(normalized);
        workflow.dirty = false;
    }
}

void ProjectManager::save_project_as(
    const QString& name,
    const QString& parent_directory)
{
    const auto validation = ProjectPathValidator::validate_save_as(
        name, parent_directory, metadata_.root_directory);
    if (!validation)
        throw std::runtime_error(validation.error.toStdString());

    const QString destination = validation.final_path;
    const QString staging = QDir(parent_directory).absoluteFilePath(
        "." + QFileInfo(destination).fileName() + ".staging-" +
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    ProjectManager candidate = *this;
    const QString source_root = metadata_.root_directory;
    try {
        if (!source_root.isEmpty())
            ProjectStorage::copy_directory(source_root, staging);
        else if (!QDir().mkpath(staging))
            throw std::runtime_error("cannot create Save As staging directory");

        for (auto& workflow : candidate.workflows_)
            relocate_graph_paths(workflow.document.graph, source_root, staging);
        for (auto& model : candidate.model_list_.entries()) {
            model.asset_directory = relocated_path(
                model.asset_directory, source_root, staging);
            for (auto& weight : model.weights)
                weight.path = relocated_path(weight.path, source_root, staging);
        }

        candidate.metadata_.name = name.trimmed();
        candidate.metadata_.project_id =
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        candidate.metadata_.derived_from_project_id = metadata_.project_id;
        candidate.metadata_.derived_from_project_path = project_file();
        candidate.metadata_.root_directory = staging;
        candidate.metadata_dirty_ = true;
        candidate.save_project();

        ProjectManager verification;
        verification.open_project(candidate.project_file());
        if (verification.metadata().project_id != candidate.metadata_.project_id)
            throw std::runtime_error("Save As staging verification failed");

        ProjectStorage::commit_staging(staging, destination);
        candidate.metadata_.root_directory = destination;
        for (auto& workflow : candidate.workflows_)
            relocate_graph_paths(workflow.document.graph, staging, destination);
        for (auto& model : candidate.model_list_.entries()) {
            model.asset_directory = relocated_path(
                model.asset_directory, staging, destination);
            for (auto& weight : model.weights)
                weight.path = relocated_path(weight.path, staging, destination);
        }
        *this = std::move(candidate);
    } catch (...) {
        ProjectStorage::remove_directory(staging);
        throw;
    }
}

size_t ProjectManager::add_workflow(const QString& name) {
    if (name.trimmed().isEmpty()) throw std::runtime_error("workflow name cannot be empty");
    workflows_.push_back(ProjectWorkflow{next_workflow_id(), name.trimmed(), {}, true});
    active_index_ = workflows_.size() - 1;
    metadata_dirty_ = true;
    return active_index_;
}

void ProjectManager::remove_workflow(size_t index) {
    if (workflows_.size() == 1) throw std::runtime_error("a project must contain at least one workflow");
    if (index >= workflows_.size()) throw std::runtime_error("workflow index is out of range");
    workflows_.erase(workflows_.begin() + static_cast<std::ptrdiff_t>(index));
    if (active_index_ >= workflows_.size()) active_index_ = workflows_.size() - 1;
    else if (index < active_index_) --active_index_;
    metadata_dirty_ = true;
}

void ProjectManager::set_active_index(size_t index) {
    if (index >= workflows_.size()) throw std::runtime_error("workflow index is out of range");
    if (index == active_index_) return;
    active_index_ = index;
    metadata_dirty_ = true;
}

void ProjectManager::rename_active_workflow(const QString& name) {
    const QString normalized = name.trimmed();
    if (normalized.isEmpty()) throw std::runtime_error("workflow name cannot be empty");
    if (normalized == active_workflow().name) return;
    active_workflow().name = normalized;
    active_workflow().dirty = true;
}

void ProjectManager::set_project_name(const QString& name) {
    const QString normalized = name.trimmed();
    if (normalized.isEmpty()) throw std::runtime_error("project name cannot be empty");
    if (normalized == metadata_.name) return;
    metadata_.name = normalized;
    metadata_dirty_ = true;
}

void ProjectManager::set_project_description(const QString& description) {
    if (description == metadata_.description) return;
    metadata_.description = description;
    metadata_dirty_ = true;
}

void ProjectManager::mark_active_dirty() { active_workflow().dirty = true; }

void ProjectManager::mark_models_dirty() { metadata_dirty_ = true; }

ProjectModel ProjectManager::add_model(ProjectModel model) {
    if (model.created_at.empty())
        model.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    const auto added = model_list_.add(std::move(model));
    metadata_dirty_ = true;
    return added;
}

bool ProjectManager::remove_model(const QString& id) {
    const bool removed = model_list_.remove(id.toStdString());
    if (removed) metadata_dirty_ = true;
    return removed;
}

void ProjectManager::clear_models() {
    if (model_list_.empty()) return;
    model_list_.clear();
    metadata_dirty_ = true;
}

int ProjectManager::allocate_training_run_number() {
    const int number = next_training_run_number_++;
    metadata_dirty_ = true;
    return number;
}

const ProjectModel* ProjectManager::find_model(const QString& id) const {
    return model_list_.find(id.toStdString());
}

ProjectModel* ProjectManager::find_model(const QString& id) {
    return model_list_.find(id.toStdString());
}

const ProjectWorkflow& ProjectManager::active_workflow() const {
    if (workflows_.empty() || active_index_ >= workflows_.size()) throw std::runtime_error("project has no active workflow");
    return workflows_[active_index_];
}

ProjectWorkflow& ProjectManager::active_workflow() {
    if (workflows_.empty() || active_index_ >= workflows_.size()) throw std::runtime_error("project has no active workflow");
    return workflows_[active_index_];
}

bool ProjectManager::is_dirty() const {
    return metadata_dirty_ || std::any_of(workflows_.begin(), workflows_.end(), [](const auto& workflow) { return workflow.dirty; });
}

QString ProjectManager::project_file() const {
    return has_storage() ? QDir(metadata_.root_directory).filePath(ProjectFileName) : QString{};
}

size_t ProjectManager::external_reference_count() const {
    if (!has_storage()) return 0;
    std::set<QString> external;
    const auto inspect = [this, &external](const std::string& value) {
        if (value.empty()) return;
        const QString path = QDir::cleanPath(QString::fromStdString(value));
        if (!QFileInfo(path).isAbsolute()) return;
        const QString relative = QDir(metadata_.root_directory).relativeFilePath(path);
        if (relative == ".." || relative.startsWith("../") || QFileInfo(relative).isAbsolute())
            external.insert(path.toCaseFolded());
    };
    for (const auto& workflow : workflows_) {
        for (const auto& [id, node] : workflow.document.graph.nodes()) {
            if (node.kind == NodeKind::DatasetInput) {
                const auto& config = std::get<DatasetConfig>(node.config);
                inspect(config.directory);
                for (const auto& item : config.items) inspect(item.path);
            } else {
                const auto& config = std::get<TaskNodeConfig>(node.config);
                inspect(config.training.output_directory);
                inspect(config.inference_output_directory);
                for (const auto& sample : config.dataset_binding.samples) {
                    for (const auto& path : sample.image_paths) inspect(path);
                    inspect(sample.mask_path);
                }
                for (const auto& result_set : config.runtime.results) {
                    inspect(result_set.result_directory);
                    for (const auto& result : result_set.samples) {
                        std::visit([&](const auto& task_result) {
                            inspect(task_result.image_path);
                            inspect(task_result.save_dir);
                            inspect(task_result.metadata_path);
                            using Result = std::decay_t<decltype(task_result)>;
                            if constexpr (std::is_same_v<Result, jocelin::result::sg::SgResult>) {
                                inspect(task_result.mask_path);
                                inspect(task_result.visualization_path);
                            } else {
                                inspect(task_result.heatmap_path);
                            }
                        }, result.result);
                    }
                }
            }
        }
    }
    for (const auto& model : model_list_.entries())
        for (const auto& weight : model.weights) inspect(weight.path);
    return external.size();
}

QString ProjectManager::next_workflow_id() const {
    int suffix = 1;
    while (true) {
        const QString candidate = "workflow_" + QString::number(suffix++);
        const bool exists = std::any_of(workflows_.begin(), workflows_.end(), [&candidate](const auto& workflow) {
            return workflow.id == candidate;
        });
        if (!exists) return candidate;
    }
}

} // namespace studio
