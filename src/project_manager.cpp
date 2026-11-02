#include "project_manager.h"
#include "model_list_storage.h"
#include "project_path_validator.h"
#include "project_storage.h"
#include "task_workspace_service.h"
#include "task_dataset_binding.h"

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
    for (const auto& [node_id, node] : graph.nodes()) {
        auto& mutable_node = graph.node(node_id);
        if (node.kind == NodeKind::DatasetInput) {
            auto& config = std::get<DatasetConfig>(mutable_node.config);
            config.directory = relocated_path(config.directory, source_root, destination_root);
            for (auto& item : config.items)
                item.path = relocated_path(item.path, source_root, destination_root);
        } else {
            auto& config = std::get<TaskNodeConfig>(mutable_node.config);
            for (auto& model : config.model_list.entries()) {
                model.asset_directory = relocated_path(model.asset_directory, source_root, destination_root);
                for (auto& weight : model.weights)
                    weight.path = relocated_path(weight.path, source_root, destination_root);
            }
            config.training.output_directory = relocated_path(config.training.output_directory, source_root, destination_root);
            config.inference_output_directory = relocated_path(config.inference_output_directory, source_root, destination_root);
            for (auto& sample : config.dataset_binding.samples) {
                for (auto& path : sample.image_paths)
                    path = relocated_path(path, source_root, destination_root);
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

WorkflowGraph portable_graph(const WorkflowGraph& source, const QString& root_directory) {
    WorkflowGraph graph = source;
    for (const auto& [node_id, node] : graph.nodes()) {
        auto& mutable_node = graph.node(node_id);
        if (node.kind == NodeKind::DatasetInput) {
            auto& config = std::get<DatasetConfig>(mutable_node.config);
            config.directory = ProjectStorage::portable_path(config.directory, root_directory).toStdString();
            for (auto& item : config.items)
                item.path = ProjectStorage::portable_path(item.path, root_directory).toStdString();
        } else {
            auto& config = std::get<TaskNodeConfig>(mutable_node.config);
            for (auto& model : config.model_list.entries()) {
                model.asset_directory = ProjectStorage::portable_path(model.asset_directory, root_directory).toStdString();
                for (auto& weight : model.weights)
                    weight.path = ProjectStorage::portable_path(weight.path, root_directory).toStdString();
            }
            config.training.output_directory = ProjectStorage::portable_path(config.training.output_directory, root_directory).toStdString();
            config.inference_output_directory = ProjectStorage::portable_path(config.inference_output_directory, root_directory).toStdString();
            for (auto& sample : config.dataset_binding.samples) {
                for (auto& image_path : sample.image_paths)
                    image_path = ProjectStorage::portable_path(image_path, root_directory).toStdString();
                sample.training_mask_path = ProjectStorage::portable_path(
                    sample.training_mask_path, root_directory).toStdString();
            }
            for (auto& result_set : config.runtime.results) {
                result_set.result_directory = ProjectStorage::portable_path(
                    result_set.result_directory, root_directory).toStdString();
                for (auto& result : result_set.samples) {
                    std::visit([&](auto& task_result) {
                        task_result.image_path = ProjectStorage::portable_path(task_result.image_path, root_directory).toStdString();
                        task_result.save_dir = ProjectStorage::portable_path(task_result.save_dir, root_directory).toStdString();
                        task_result.metadata_path = ProjectStorage::portable_path(task_result.metadata_path, root_directory).toStdString();
                        using Result = std::decay_t<decltype(task_result)>;
                        if constexpr (std::is_same_v<Result, jocelin::result::sg::SgResult>) {
                            task_result.mask_path = ProjectStorage::portable_path(task_result.mask_path, root_directory).toStdString();
                            task_result.visualization_path = ProjectStorage::portable_path(task_result.visualization_path, root_directory).toStdString();
                        } else {
                            task_result.heatmap_path = ProjectStorage::portable_path(task_result.heatmap_path, root_directory).toStdString();
                        }
                    }, result.result);
                }
            }
        }
    }
    return graph;
}

void resolve_graph_paths(WorkflowGraph& graph, const QString& root_directory) {
    for (const auto& [node_id, node] : graph.nodes()) {
        auto& mutable_node = graph.node(node_id);
        if (node.kind == NodeKind::DatasetInput) {
            auto& config = std::get<DatasetConfig>(mutable_node.config);
            config.directory = ProjectStorage::runtime_path(QString::fromStdString(config.directory), root_directory);
            for (auto& item : config.items)
                item.path = ProjectStorage::runtime_path(QString::fromStdString(item.path), root_directory);
        } else {
            auto& config = std::get<TaskNodeConfig>(mutable_node.config);
            for (auto& model : config.model_list.entries()) {
                model.asset_directory = ProjectStorage::runtime_path(QString::fromStdString(model.asset_directory), root_directory);
                for (auto& weight : model.weights)
                    weight.path = ProjectStorage::runtime_path(QString::fromStdString(weight.path), root_directory);
            }
            config.training.output_directory = ProjectStorage::runtime_path(QString::fromStdString(config.training.output_directory), root_directory);
            config.inference_output_directory = ProjectStorage::runtime_path(QString::fromStdString(config.inference_output_directory), root_directory);
            for (auto& sample : config.dataset_binding.samples) {
                for (auto& image_path : sample.image_paths)
                    image_path = ProjectStorage::runtime_path(QString::fromStdString(image_path), root_directory);
                sample.training_mask_path = ProjectStorage::runtime_path(
                    QString::fromStdString(sample.training_mask_path), root_directory);
            }
            for (auto& result_set : config.runtime.results) {
                result_set.result_directory = ProjectStorage::runtime_path(
                    QString::fromStdString(result_set.result_directory), root_directory);
                for (auto& result : result_set.samples) {
                    std::visit([&](auto& task_result) {
                        task_result.image_path = ProjectStorage::runtime_path(QString::fromStdString(task_result.image_path), root_directory);
                        task_result.save_dir = ProjectStorage::runtime_path(QString::fromStdString(task_result.save_dir), root_directory);
                        task_result.metadata_path = ProjectStorage::runtime_path(QString::fromStdString(task_result.metadata_path), root_directory);
                        using Result = std::decay_t<decltype(task_result)>;
                        if constexpr (std::is_same_v<Result, jocelin::result::sg::SgResult>) {
                            task_result.mask_path = ProjectStorage::runtime_path(QString::fromStdString(task_result.mask_path), root_directory);
                            task_result.visualization_path = ProjectStorage::runtime_path(QString::fromStdString(task_result.visualization_path), root_directory);
                        } else {
                            task_result.heatmap_path = ProjectStorage::runtime_path(QString::fromStdString(task_result.heatmap_path), root_directory);
                        }
                    }, result.result);
                }
            }
        }
    }
}

void validate_inference_results(const WorkflowGraph& graph, const QString& workflow_id, const QString& root) {
    for (const auto& [node_id, node] : graph.nodes()) {
        if (node.kind != NodeKind::TaskConfig) continue;
        const auto& task = std::get<TaskNodeConfig>(node.config);
        const bool missing_model_list = task.model_list.entries().empty() &&
            !QFileInfo::exists(QDir(root).filePath("nodes/" + workflow_id + "/" +
                QString::number(node_id) + "/models/model_list.json"));
        std::set<std::pair<std::string, std::string>> keys;
        for (const auto& result : task.runtime.results) {
            const auto* model = task.model_list.find(result.model_id);
            const auto* weight = model ? jocelin::model::find_weight(*model, result.weight_id) : nullptr;
            if ((!weight && !missing_model_list) ||
                (weight && jocelin::config::to_string(weight->format) != result.weight_format) ||
                !keys.emplace(result.model_id, result.weight_id).second) {
                throw std::runtime_error("inference result must reference a unique model/weight in its TaskNode");
            }
            const auto expected = QDir(root).filePath("nodes/" + workflow_id + "/" +
                inference_result_directory(node_id, result.model_id, result.weight_id));
            if (QDir::cleanPath(QString::fromStdString(result.result_directory)) != QDir::cleanPath(expected))
                throw std::runtime_error("inference result directory must belong to its Node/model/weight");
            const auto inspect = [&expected](const std::string& value) {
                if (value.empty()) return;
                const auto relative = QDir(expected).relativeFilePath(QString::fromStdString(value));
                if (relative == ".." || relative.startsWith("../") || QFileInfo(relative).isAbsolute())
                    throw std::runtime_error("inference artifact must stay in its branch directory");
            };
            for (const auto& sample : result.samples) {
                if (sample.source_sample_id.empty()) throw std::runtime_error("inference result requires source_sample_id");
                std::visit([&](const auto& value) {
                    inspect(value.save_dir);
                    inspect(value.metadata_path);
                    using Result = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Result, jocelin::result::sg::SgResult>) {
                        inspect(value.mask_path); inspect(value.visualization_path);
                    } else inspect(value.heatmap_path);
                }, sample.result);
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
    training_runs_ = {};
    root_directory_.clear();
    ProjectWorkflow workflow;
    workflow.id = "workflow_1";
    workflow.name = "Workflow 1";
    workflows_.push_back(std::move(workflow));
    active_index_ = 0;
    metadata_dirty_ = false;
}

std::vector<MissingModelAssets> ProjectManager::open_project(const QString& project_file_path) {
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
         "created_at", "modified_at", "workflows"},
        "Project document");
    if (root.value("format").toString() != "deep_learning_project")
        throw std::runtime_error("unsupported project format");
    if (!root.value("name").isString() || root.value("name").toString().trimmed().isEmpty() ||
        !root.value("description").isString() || !root.value("created_at").isString() ||
        !root.value("workflows").isArray())
        throw std::runtime_error("project metadata is incomplete");

    ProjectMetadata metadata;
    metadata.project_id = root.value("project_id").toString();
    metadata.name = root.value("name").toString().trimmed();
    metadata.description = root.value("description").toString();
    metadata.created_at = root.value("created_at").toString();
    metadata.modified_at = root.value("modified_at").toString();
    const QString root_directory = QFileInfo(file_path).absolutePath();
    if (metadata.project_id.isEmpty() ||
        !QDateTime::fromString(metadata.modified_at, Qt::ISODate).isValid())
        throw std::runtime_error("project version metadata is invalid");
    if (!QDateTime::fromString(metadata.created_at, Qt::ISODate).isValid())
        throw std::runtime_error("project creation timestamp is invalid");
    std::vector<ProjectWorkflow> workflows;
    std::vector<MissingModelAssets> missing_assets;
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
        const QString safe_relative_file = ProjectStorage::validated_relative_path(relative_file, "workflow file");
        const bool duplicate = std::any_of(workflows.begin(), workflows.end(), [&id](const auto& item) { return item.id == id; });
        if (duplicate) throw std::runtime_error("duplicate workflow id in project");
        QFile workflow_file(QDir(root_directory).filePath(safe_relative_file));
        if (!workflow_file.open(QIODevice::ReadOnly))
            throw std::runtime_error(("cannot open workflow " + name).toStdString());
        ProjectWorkflow workflow{id, name, parse_workflow(workflow_file.readAll(), QDir(root_directory).filePath("nodes/" + id)), false};
        resolve_graph_paths(workflow.document.graph, root_directory);
        for (const auto& [node_id, node] : workflow.document.graph.nodes()) {
            if (node.kind != NodeKind::TaskConfig) continue;
            auto& task = std::get<TaskNodeConfig>(workflow.document.graph.node(node_id).config);
            const QString node_directory = QDir(root_directory).filePath(
                "nodes/" + id + "/" + QString::number(node_id));
            const QString model_list_path = QDir(node_directory).filePath("models/model_list.json");
            if (QFileInfo::exists(model_list_path)) {
                task.model_list = ModelListStorage::load(node_directory, root_directory);
            } else {
                task.model_list = {};
                missing_assets.push_back({id, node_id, {}, {}, {model_list_path}, false});
            }
            const auto task_type = jocelin::config::tool_type_from_string(task.task);
            for (const auto& model : task.model_list.entries()) {
                if (model.task != task_type) {
                    throw std::runtime_error("model task does not match the owning TaskNode");
                }
                MissingModelAssets missing{id, node_id, QString::fromStdString(model.id), {}, {}, false};
                for (const auto& weight : model.weights) {
                    const QString path = QString::fromStdString(weight.path);
                    if (!QFileInfo(path).isFile()) {
                        missing.weight_ids.push_back(QString::fromStdString(weight.id));
                        missing.paths.push_back(path);
                    }
                }
                if (!missing.weight_ids.isEmpty()) {
                    missing.all_weights_missing = missing.weight_ids.size() == static_cast<int>(model.weights.size());
                    missing_assets.push_back(std::move(missing));
                }
            }
            TaskWorkspaceService(workflow.document.graph, node_id).refresh_quality();
        }
        validate_inference_results(workflow.document.graph, id, root_directory);
        workflows.push_back(std::move(workflow));
    }
    if (workflows.empty())
        throw std::runtime_error("project has no workflows");

    auto training_runs = TrainingRunStore::load(root_directory);
    metadata_ = std::move(metadata);
    workflows_ = std::move(workflows);
    training_runs_ = std::move(training_runs);
    root_directory_ = root_directory;
    active_index_ = 0;
    metadata_dirty_ = false;
    return missing_assets;
}

void ProjectManager::remove_models(int node_id, const QStringList& model_ids, const QString& weight_id,
                                   const ModelWeightSelection& weights) {
    auto& graph = active_workflow().document.graph;
    TaskWorkspaceService service(graph, node_id,
        has_storage() ? QFileInfo(node_models_directory(node_id)).absolutePath() : QString{});
    if (!weights.empty()) {
        for (const auto& [model_id, selected_weight_id] : weights) {
            service.remove_model_weight(model_id, selected_weight_id);
        }
    } else if (!weight_id.isEmpty()) {
        if (model_ids.size() != 1) {
            throw std::runtime_error("weight deletion requires exactly one model");
        }
        service.remove_model_weight(model_ids.front(), weight_id);
    } else if (model_ids.isEmpty()) {
        service.clear_models();
    } else {
        for (const QString& id : model_ids) {
            service.remove_model(id);
        }
    }
    mark_active_dirty();
    if (has_storage()) {
        save_project();
    }
}

void ProjectManager::clear_dataset_dependents(const std::vector<int>& dataset_node_ids) {
    auto& graph = active_workflow().document.graph;
    std::set<int> owners;
    for (int source : dataset_node_ids) {
        if (graph.node(source).kind != NodeKind::DatasetInput)
            throw std::runtime_error("Dataset cleanup requires a DataNode");
        for (const auto& edge : graph.connections())
            if (edge.from_node_id == source && graph.node(edge.to_node_id).kind == NodeKind::TaskConfig)
                owners.insert(edge.to_node_id);
    }
    if (owners.empty()) return;
    for (int owner : owners)
        clear_task_dataset_state(std::get<TaskNodeConfig>(graph.node(owner).config));
    mark_active_dirty();
    if (!has_storage()) return;
    // No files are removed until all persisted references have been cleared.
    save_project();
    QStringList failures;
    for (int owner : owners) {
        const auto directory = QFileInfo(node_models_directory(owner)).absolutePath();
        for (const auto& leaf : {QStringLiteral("annotations"), QStringLiteral("inference")}) {
            const auto path = QDir(directory).filePath(leaf);
            if (QDir(path).exists() && !QDir(path).removeRecursively()) failures.append(path);
        }
    }
    if (!failures.empty())
        throw std::runtime_error(("Dataset references cleared, but cannot remove:\n" + failures.join('\n')).toStdString());
}

void ProjectManager::save_project(const QString& root_directory) {
    QString root = root_directory.isEmpty() ? root_directory_ : QDir(root_directory).absolutePath();
    if (root.isEmpty()) throw std::runtime_error("project root directory is not selected");
    QJsonArray workflow_entries;
    std::vector<std::pair<QString, QByteArray>> serialized_workflows;
    std::vector<std::pair<QString, QByteArray>> serialized_regions;
    std::vector<std::pair<QString, QByteArray>> serialized_models;
    for (const auto& workflow : workflows_) {
        const QString relative_file = "workflows/" + workflow.id + ".json";
        validate_inference_results(workflow.document.graph, workflow.id, root);
        const auto graph = portable_graph(workflow.document.graph, root);
        auto serialized = serialize_workflow(graph, workflow.document.positions);
        for (const auto& [node_id, node] : workflow.document.graph.nodes()) {
            if (node.kind != NodeKind::TaskConfig) continue;
            const auto& task = std::get<TaskNodeConfig>(node.config);
            const auto task_type = jocelin::config::tool_type_from_string(task.task);
            for (const auto& model : task.model_list.entries())
                if (model.task != task_type)
                    throw std::runtime_error("model task does not match the owning TaskNode");
            const QString node_directory = QDir(root).filePath(
                "nodes/" + workflow.id + "/" + QString::number(node_id));
            // Preserve unresolved references after the user declines cleanup.
            // Writing an empty catalog here would make the next open reject them.
            if (!task.model_list.entries().empty() || task.runtime.results.empty() ||
                QFileInfo::exists(QDir(node_directory).filePath("models/model_list.json"))) {
                serialized_models.emplace_back(node_directory,
                    ModelListStorage::serialize(task.model_list, root));
            }
        }
        for (auto& [file, data] : serialized.inference_assets)
            serialized_regions.emplace_back(
                QDir(root).filePath("nodes/" + workflow.id + "/" + file), std::move(data));
        for (auto& [file, data] : serialized.region_assets)
            serialized_regions.emplace_back(
                QDir(root).filePath("nodes/" + workflow.id + "/" + file), std::move(data));
        serialized_workflows.emplace_back(
            QDir(root).filePath(relative_file), std::move(serialized.json));
        workflow_entries.append(QJsonObject{{"id", workflow.id}, {"name", workflow.name}, {"file", relative_file}});
    }
    const QString modified_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QJsonObject project{
        {"format", "deep_learning_project"},
        {"project_id", metadata_.project_id},
        {"name", metadata_.name},
        {"description", metadata_.description},
        {"created_at", metadata_.created_at},
        {"modified_at", modified_at},
        {"workflows", workflow_entries}};
    const auto serialized_project = QJsonDocument(project).toJson(QJsonDocument::Indented);
    const auto serialized_runs = training_runs_.serialize();

    if (!QDir().mkpath(QDir(root).filePath("workflows")))
        throw std::runtime_error("cannot create project workflows directory");
    for (const auto& [path, data] : serialized_regions) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            throw std::runtime_error("cannot create Node asset directory");
        ProjectStorage::write_atomic(path, data);
    }
    for (const auto& [node_directory, data] : serialized_models)
        ModelListStorage::save(data, node_directory);
    training_runs_.save(root, serialized_runs);
    for (const auto& [path, data] : serialized_workflows)
        ProjectStorage::write_atomic(path, data);
    ProjectStorage::write_atomic(
        QDir(root).filePath(ProjectFileName),
        serialized_project);
    metadata_.modified_at = modified_at;
    root_directory_ = root;
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
        name, parent_directory, root_directory_);
    if (!validation)
        throw std::runtime_error(validation.error.toStdString());

    const QString destination = validation.final_path;
    const QString staging = QDir(parent_directory).absoluteFilePath(
        "." + QFileInfo(destination).fileName() + ".staging-" +
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    ProjectManager candidate = *this;
    const QString source_root = root_directory_;
    try {
        if (!source_root.isEmpty())
            ProjectStorage::copy_directory(source_root, staging);
        else if (!QDir().mkpath(staging))
            throw std::runtime_error("cannot create Save As staging directory");

        for (auto& workflow : candidate.workflows_)
            relocate_graph_paths(workflow.document.graph, source_root, staging);

        candidate.metadata_.name = name.trimmed();
        candidate.metadata_.project_id =
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        candidate.root_directory_ = staging;
        candidate.metadata_dirty_ = true;
        candidate.save_project();

        ProjectManager verification;
        verification.open_project(candidate.project_file());
        if (verification.metadata().project_id != candidate.metadata_.project_id)
            throw std::runtime_error("Save As staging verification failed");

        ProjectStorage::commit_staging(staging, destination);
        candidate.root_directory_ = destination;
        for (auto& workflow : candidate.workflows_)
            relocate_graph_paths(workflow.document.graph, staging, destination);
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

QString ProjectManager::node_models_directory(int node_id) const {
    if (!has_storage()) throw std::runtime_error("save the project before storing model assets");
    if (active_workflow().document.graph.node(node_id).kind != NodeKind::TaskConfig)
        throw std::runtime_error("models require a TaskNode owner");
    return QDir(root_directory_).filePath(
        "nodes/" + active_workflow().id + "/" + QString::number(node_id) + "/models");
}

QString ProjectManager::prepare_inference_directory(int node_id, const QString& model_id, const QString& weight_id) const {
    const auto& task = std::get<TaskNodeConfig>(active_workflow().document.graph.node(node_id).config);
    const auto* model = task.model_list.find(model_id.toStdString());
    if (!has_storage() || !model || !jocelin::model::find_weight(*model, weight_id.toStdString()))
        throw std::runtime_error("inference requires a stored TaskNode model/weight");
    const auto final = QDir(root_directory_).filePath("nodes/" + active_workflow().id + "/" +
        inference_result_directory(node_id, model_id.toStdString(), weight_id.toStdString()));
    const auto staging = QDir(QFileInfo(final).absolutePath()).filePath(
        "." + weight_id + ".pending-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(staging)) throw std::runtime_error("cannot prepare inference branch directory");
    return staging;
}

void ProjectManager::discard_inference_directory(int node_id, const QString& model_id, const QString& weight_id,
                                                const QString& staging) const {
    if (staging.isEmpty()) return;
    if (!has_storage()) throw std::runtime_error("inference cleanup requires a stored Node");
    const auto final = QDir(root_directory_).filePath("nodes/" + active_workflow().id + "/" +
        inference_result_directory(node_id, model_id.toStdString(), weight_id.toStdString()));
    if (QFileInfo(staging).absolutePath() != QFileInfo(final).absolutePath() ||
        !QFileInfo(staging).fileName().startsWith("." + weight_id + ".pending-") ||
        QUuid(QFileInfo(staging).fileName().mid(weight_id.size() + 10)).isNull())
        throw std::runtime_error("temporary inference directory must belong to its Node/model/weight");
    if (QFileInfo::exists(staging) && !QDir(staging).removeRecursively())
        throw std::runtime_error("cannot remove temporary inference directory");
}

void ProjectManager::commit_inference_result(int node_id, const QString& model_id, const QString& weight_id,
                                            const jocelin::inference::InferenceReport& report, const QString& staging) {
    if (!has_storage()) throw std::runtime_error("inference commit requires a stored Node");
    const auto final = QDir(root_directory_).filePath("nodes/" + active_workflow().id + "/" +
        inference_result_directory(node_id, model_id.toStdString(), weight_id.toStdString()));
    if (QFileInfo(staging).absolutePath() != QFileInfo(final).absolutePath() ||
        !QFileInfo(staging).fileName().startsWith("." + weight_id + ".pending-") ||
        QUuid(QFileInfo(staging).fileName().mid(weight_id.size() + 10)).isNull() || !QDir(staging).exists())
        throw std::runtime_error("invalid inference branch staging directory");
    auto candidate = active_workflow().document.graph;
    TaskWorkspaceService service(candidate, node_id);
    const auto* model = service.find_model(model_id);
    const auto* weight = model ? jocelin::model::find_weight(*model, weight_id.toStdString()) : nullptr;
    if (!weight) throw std::runtime_error("inference model/weight no longer exists in its TaskNode");
    auto relocated = report;
    const auto relocate = [&staging, &final](std::string& path) {
        if (path.empty()) return;
        const auto relative = QDir(staging).relativeFilePath(QString::fromStdString(path));
        if (relative == ".." || relative.startsWith("../") || QFileInfo(relative).isAbsolute())
            throw std::runtime_error("new inference artifact is outside its temporary branch");
        path = QDir(final).absoluteFilePath(relative).toStdString();
    };
    std::visit([&](auto& batch) {
        for (auto& result : batch.samples) {
            if (!result.metadata_path.empty()) {
                const auto file = QString::fromStdString(result.metadata_path);
                auto checked = result.metadata_path;
                relocate(checked);
                // Core's per-image metadata accompanies the same branch artifacts.
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(ProjectStorage::read(file), &error);
                if (error.error != QJsonParseError::NoError || !document.isObject()) {
                    throw std::runtime_error(QStringLiteral("invalid staged inference metadata: %1\n%2")
                        .arg(file, error.error != QJsonParseError::NoError
                            ? error.errorString() : QStringLiteral("JSON root must be an object")).toStdString());
                }
                auto metadata = document.object();
                for (auto it = metadata.begin(); it != metadata.end(); ++it) {
                    if (!it.value().isString()) continue;
                    auto value = QDir::fromNativeSeparators(it.value().toString());
                    const auto prefix = QDir::fromNativeSeparators(staging) + "/";
                    if (value.startsWith(prefix)) it.value() = QDir(final).filePath(value.mid(prefix.size()));
                }
                ProjectStorage::write_atomic(file, QJsonDocument(metadata).toJson());
            }
            relocate(result.save_dir); relocate(result.metadata_path);
            using Result = std::decay_t<decltype(result)>;
            if constexpr (std::is_same_v<Result, jocelin::result::sg::SgResult>) {
                relocate(result.mask_path); relocate(result.visualization_path);
            } else relocate(result.heatmap_path);
        }
    }, relocated);
    service.record_inference_result(relocated, model_id.toStdString(), weight_id.toStdString(), weight->format,
        final.toStdString(), {});
    service.task().inference_output_directory = final.toStdString();
    validate_inference_results(candidate, active_workflow().id, root_directory_);
    const auto serialized = serialize_workflow(portable_graph(candidate, root_directory_), active_workflow().document.positions);
    const auto relative = inference_result_directory(node_id, model_id.toStdString(), weight_id.toStdString()) + "/result.json";
    const auto asset = std::find_if(serialized.inference_assets.begin(), serialized.inference_assets.end(),
        [&relative](const auto& value) { return value.first == relative; });
    if (asset == serialized.inference_assets.end()) throw std::runtime_error("inference result was not serialized");
    ProjectStorage::write_atomic(QDir(staging).filePath("result.json"), asset->second);
    QSaveFile workflow_file(QDir(root_directory_).filePath("workflows/" + active_workflow().id + ".json"));
    if (!workflow_file.open(QIODevice::WriteOnly) || workflow_file.write(serialized.json) != serialized.json.size())
        throw std::runtime_error("cannot prepare inference Workflow reference");
    QDir parent(QFileInfo(final).absolutePath());
    const auto backup = staging + ".previous";
    const bool had_result = QDir(final).exists();
    if (had_result && !parent.rename(final, backup)) throw std::runtime_error("cannot retain previous inference branch");
    if (!parent.rename(staging, final)) {
        if (had_result && !parent.rename(backup, final))
            throw std::runtime_error(("previous inference branch remains at " + backup).toStdString());
        throw std::runtime_error("cannot publish inference branch");
    }
    if (!workflow_file.commit()) {
        const bool undone = parent.rename(final, staging);
        if (!undone || (had_result && !parent.rename(backup, final)))
            throw std::runtime_error(("cannot restore previous inference branch at " + backup).toStdString());
        throw std::runtime_error("cannot commit inference Workflow reference; previous branch restored");
    }
    active_workflow().document.graph = std::move(candidate);
    active_workflow().dirty = false;
    if (had_result) QDir(backup).removeRecursively();
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
    return metadata_dirty_ || training_runs_.is_dirty() || std::any_of(workflows_.begin(), workflows_.end(), [](const auto& workflow) { return workflow.dirty; });
}

QString ProjectManager::project_file() const {
    return has_storage() ? QDir(root_directory_).filePath(ProjectFileName) : QString{};
}

size_t ProjectManager::external_reference_count() const {
    if (!has_storage()) return 0;
    std::set<QString> external;
    const auto inspect = [this, &external](const std::string& value) {
        if (value.empty()) return;
        const QString path = QDir::cleanPath(QString::fromStdString(value));
        if (!QFileInfo(path).isAbsolute()) return;
        const QString relative = QDir(root_directory_).relativeFilePath(path);
        if (relative == ".." || relative.startsWith("../") || QFileInfo(relative).isAbsolute())
            external.insert(path.toCaseFolded());
    };
    for (const auto& workflow : workflows_) {
        for (const auto& [node_id, node] : workflow.document.graph.nodes()) {
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
    for (const auto& workflow : workflows_)
        for (const auto& [node_id, node] : workflow.document.graph.nodes())
            if (node.kind == NodeKind::TaskConfig)
                for (const auto& model : std::get<TaskNodeConfig>(node.config).model_list.entries())
                    for (const auto& weight : model.weights) inspect(weight.path);
    return external.size();
}

QString ProjectManager::next_workflow_id() const {
    return "workflow_" + QUuid::createUuid().toString(QUuid::WithoutBraces);

}

} // namespace studio
