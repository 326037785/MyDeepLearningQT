#include "model_actions.h"
#include "json_file_writer.h"
#include "node_workspace.h"
#include "classification_annotation_state.h"
#include "model_spec_catalog.h"
#include "task_workspace_service.h"
#include <common/model/model_weight_matcher.h>
#include <checkpoint/checkpoint_catalog.h>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>

namespace studio {

ModelActions::ModelActions(ProjectManager& project, QWidget* parent)
    : QObject(parent), project_(project), dialog_parent_(parent) {}

void ModelActions::mount(NodeWorkspace& workspace) {
    workspace.import_model_requested = [this](int id) { import_model(id); };
    workspace.export_model_requested = [this](int id, const QString& model) { export_model(id, model); };
    workspace.delete_models_requested = [this](int id, const QStringList& models) { delete_models(id, models); };
    workspace.delete_model_weight_requested = [this](int id, const QString& model, const QString& weight) {
        delete_models(id, {model}, weight);
    };
    workspace.clear_models_requested = [this](int id) { clear_models(id); };
}

void ModelActions::import_model(int node_id) {
    if (!project_.has_storage()) {
        QMessageBox::information(
            dialog_parent_, tr("Save Project first"),
            tr("Save the Project before importing model assets."));
        return;
    }
    const QString manifest_path = QFileDialog::getOpenFileName(
        dialog_parent_, tr("Import model weights"), {},
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
        TaskWorkspaceService task_service(project_.active_workflow().document.graph, node_id);
        const auto& task = task_service.task();
        jocelin::config::AiConfig config;
        config.tool_type = jocelin::config::tool_type_from_string(task.task);
        config.model_index = task.model_index;
        config.model_scale = jocelin::config::model_scale_from_string(task.model_scale);
        config.model_embed_dim = 768;
        config.model_depth = 12;
        config.model_num_heads = 12;
        config.image_width = task.training.image_width;
        config.image_height = task.training.image_height;
        config.normalize = task_service.binding().preprocessing.normalize;
        std::vector<jocelin::model::ModelClass> classes;
        for (const auto& entry : object.value("classes").toArray()) {
            const auto value = entry.toObject();
            classes.push_back({value.value("id").toString().toStdString(),
                value.value("name").toString().toStdString(), value.value("mask_value").toInt()});
        }
        if (classes.empty()) {
            if (config.tool_type == jocelin::config::ToolType::Cls) {
                for (const auto* value : classification_classes(task_service.binding().classes))
                    classes.push_back({value->id, value->name, 0});
            } else {
                for (const auto& value : task_service.binding().classes)
                    classes.push_back({value.id, value.name, value.mask_value});
            }
        }
        config.num_classes = static_cast<int>(classes.size());
        if (config.tool_type == jocelin::config::ToolType::Sg) {
            config.num_classes = 1;
            for (const auto& value : classes)
                config.num_classes = std::max(config.num_classes, value.mask_value + 1);
        }
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
            QDialog dialog(dialog_parent_);
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
        const QString asset_directory = QDir(project_.node_models_directory(node_id))
            .filePath("imported/" + stamp);
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
            const auto found = config.tool_type == jocelin::config::ToolType::Cls
                ? (label <= static_cast<int>(classes.size()) ? classes.begin() + label - 1 : classes.end())
                : std::find_if(classes.begin(), classes.end(),
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
                log(tr("[import] %1: %2").arg(QString::fromStdString(weight.id),
                    QString::fromStdString(report.summary())));
                log(tr("Load report: %1").arg(report_file.fileName()));
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
        const auto added = task_service.add_model(std::move(model));
        import_added = true;
        project_.mark_active_dirty();
        project_.save_project();
        if (models_changed) models_changed();
        status(
            tr("%1 imported into Task Model List").arg(QString::fromStdString(added.name)), 5000);
    } catch (const std::exception& error) {
        if (!import_added && !imported_asset_directory.isEmpty())
            QDir(imported_asset_directory).removeRecursively();
        QMessageBox::warning(
            dialog_parent_, tr("Cannot import model"), QString::fromUtf8(error.what()));
    }
}

void ModelActions::export_model(int node_id, const QString& model_id) {
    TaskWorkspaceService service(project_.active_workflow().document.graph, node_id);
    const auto* model = service.find_model(model_id);
    if (!model) return;
    const QString parent = QFileDialog::getExistingDirectory(
        dialog_parent_, tr("Export Task model"), project_.root_directory());
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
        status(
            tr("%1 exported to %2").arg(QString::fromStdString(model->name), destination), 5000);
    } catch (const std::exception& error) {
        QMessageBox::warning(
            dialog_parent_, tr("Cannot export model"), QString::fromUtf8(error.what()));
    }
}

void ModelActions::delete_models(int node_id, const QStringList& model_ids, const QString& weight_id) {
    if (!project_.has_storage() || model_ids.isEmpty()) return;

    TaskWorkspaceService service(project_.active_workflow().document.graph, node_id, QFileInfo(project_.node_models_directory(node_id)).absolutePath());
    QStringList valid_model_ids;
    QStringList model_names;
    for (const auto& model_id : model_ids) {
        const auto* model = service.find_model(model_id);
        if (!model || (!weight_id.isEmpty() && !jocelin::model::find_weight(*model, weight_id.toStdString()))) {
            continue;
        }
        valid_model_ids.push_back(model_id);
        model_names.push_back(QString::fromStdString(model->name));
    }
    if (valid_model_ids.isEmpty()) {
        return;
    }

    const QString prompt = weight_id.isEmpty()
        ? tr("Permanently delete %1 selected model(s)?\n\n%2\n\nTheir model assets and all associated inference results will be deleted.")
            .arg(valid_model_ids.size()).arg(model_names.join("\n"))
        : tr("Permanently delete weight %1 from %2?\n\nOnly this weight's assets and inference results will be deleted.")
            .arg(weight_id, model_names.join("\n"));
    if (QMessageBox::question(
            dialog_parent_, tr("Delete selected models"),
            prompt,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    try {
        project_.remove_models(node_id, valid_model_ids, weight_id);
        if (models_changed) {
            models_changed();
        }
        status(
            weight_id.isEmpty() ? tr("%1 model(s) deleted").arg(valid_model_ids.size())
                                : tr("Weight deleted: %1").arg(weight_id), 5000);
    } catch (const std::exception& error) {
        if (models_changed) {
            models_changed();
        }
        QMessageBox::warning(
            dialog_parent_, tr("Cannot save model deletion"), QString::fromUtf8(error.what()));
    }
}

void ModelActions::clear_models(int node_id) {
    if (!project_.has_storage()) return;
    TaskWorkspaceService service(project_.active_workflow().document.graph, node_id, QFileInfo(project_.node_models_directory(node_id)).absolutePath());
    if (service.models().empty()) return;

    if (QMessageBox::question(
            dialog_parent_, tr("Clear Task models"),
            tr("Permanently delete all %1 Task models?\n\nAll model assets and inference results will be deleted.")
                .arg(service.models().size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    try {
        project_.remove_models(node_id);
        if (models_changed) {
            models_changed();
        }
        status(tr("Task ModelList cleared"), 5000);
    } catch (const std::exception& error) {
        if (models_changed) {
            models_changed();
        }
        QMessageBox::warning(
            dialog_parent_, tr("Cannot save ModelList clear"), QString::fromUtf8(error.what()));
    }
}

} // namespace studio
