#include "model_list_storage.h"
#include "model_weight_assets.h"
#include "project_storage.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace studio {
namespace {

constexpr auto ModelListFileName = "models/model_list.json";

void require_exact_fields(
    const QJsonObject& object,
    std::initializer_list<const char*> fields,
    const QString& context)
{
    std::set<QString> expected;
    for (const char* field : fields) {
        const QString name = QString::fromLatin1(field);
        expected.insert(name);
        if (!object.contains(name)) {
            throw std::runtime_error(
                (context + ": missing field '" +
                 name + "'").toStdString());
        }
        const QJsonValue value = object.value(name);
        const bool array = name == "models" || name == "weights" || name == "classes" || name == "supported_devices";
        const bool real = name == "training_loss" || name == "metric";
        const bool integer = name == "next_number" || name == "number" || name == "model_index" ||
            name == "image_width" || name == "image_height" || name == "completed_epochs" ||
            name == "best_epoch" || name == "onnx_opset" || name == "mask_value";
        if ((array && !value.isArray()) ||
            ((real || integer) && (!value.isDouble() || !std::isfinite(value.toDouble()))) ||
            (integer && (std::floor(value.toDouble()) != value.toDouble() ||
                value.toDouble() < std::numeric_limits<int>::min() || value.toDouble() > std::numeric_limits<int>::max())) ||
            (!array && !real && !integer && !value.isString())) {
            throw std::runtime_error((context + ": invalid field '" + name + "'").toStdString());
        }
    }
    for (auto iterator = object.begin();
         iterator != object.end(); ++iterator) {
        if (expected.count(iterator.key()) == 0) {
            throw std::runtime_error(
                (context + ": unexpected field '" +
                 iterator.key() + "'").toStdString());
        }
    }
}

} // namespace

std::optional<jocelin::model::WeightNormalization> ModelListStorage::normalization_from_json(
    const QJsonValue& value, jocelin::config::WeightType format) {
    if (value.isUndefined() && format != jocelin::config::WeightType::Onnx) {
        return std::nullopt;
    }
    const QJsonObject normalization = value.toObject();
    const QJsonArray mean = normalization.value("mean").toArray();
    const QJsonArray deviation = normalization.value("std").toArray();
    if (!value.isObject() || normalization.size() != 3 ||
        !normalization.value("normalize").isBool() || mean.size() != 3 || deviation.size() != 3) {
        throw std::runtime_error("model normalization metadata is invalid");
    }
    jocelin::model::WeightNormalization settings;
    settings.normalize = normalization.value("normalize").toBool();
    for (int channel = 0; channel < 3; ++channel) {
        if (!mean[channel].isDouble() || !deviation[channel].isDouble()) {
            throw std::runtime_error("model normalization channel is invalid");
        }
        settings.mean[channel] = static_cast<float>(mean[channel].toDouble());
        settings.std[channel] = static_cast<float>(deviation[channel].toDouble());
        if (!std::isfinite(settings.mean[channel]) || !std::isfinite(settings.std[channel]) ||
            (settings.normalize && settings.std[channel] <= 0.0f)) {
            throw std::runtime_error("model normalization channel is invalid");
        }
    }
    return settings;
}

QJsonValue ModelListStorage::normalization_to_json(
    const std::optional<jocelin::model::WeightNormalization>& normalization,
    jocelin::config::WeightType format) {
    if (!normalization) {
        if (format == jocelin::config::WeightType::Onnx) {
            throw std::runtime_error("ONNX model weight requires normalization metadata");
        }
        return QJsonValue(QJsonValue::Undefined);
    }
    const jocelin::model::WeightNormalization& settings = *normalization;
    return QJsonObject{
        {"normalize", settings.normalize},
        {"mean", QJsonArray{settings.mean[0], settings.mean[1], settings.mean[2]}},
        {"std", QJsonArray{settings.std[0], settings.std[1], settings.std[2]}}};
}

jocelin::model::ModelList ModelListStorage::load(
    const QString& node_directory, const QString& root_directory) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(
        ProjectStorage::read(QDir(node_directory).filePath(ModelListFileName)), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error("invalid ModelList JSON");
    const auto root = document.object();
    require_exact_fields(root, {"models", "next_number"}, "ModelList document");
    std::vector<jocelin::model::ModelEntry> models;
    int next_number = 1;
    {
        if (!root.value("models").isArray() ||
            !root.value("next_number").isDouble())
            throw std::runtime_error("Task model catalog is invalid");
        next_number = root.value("next_number").toInt();
        if (next_number < 1)
            throw std::runtime_error("next model number must be positive");
        std::set<QString> model_ids;
        std::set<int> model_numbers;
        for (const auto value : root.value("models").toArray()) {
            if (!value.isObject())
                throw std::runtime_error("invalid Task model entry");
            const auto object = value.toObject();
            require_exact_fields(
                object,
                {"id", "number", "name", "training_run_id", "created_at",
                 "task", "model_index", "model_scale", "classes",
                 "image_width", "image_height", "asset_directory", "weights",
                 "completed_epochs", "best_epoch", "training_loss", "metric_name",
                 "metric", "creation_mode", "training_status", "parent_model_id"},
                "Task model entry");
            jocelin::model::ModelEntry model;
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
            model.asset_directory = ProjectStorage::runtime_path(
                object.value("asset_directory").toString(), root_directory);
            if (!object.value("weights").isArray())
                throw std::runtime_error("Task model weights are invalid");
            for (const auto weight_value : object.value("weights").toArray()) {
                if (!weight_value.isObject())
                    throw std::runtime_error("invalid Task model weight entry");
                QJsonObject weight_object = weight_value.toObject();
                const QJsonValue normalization_value = weight_object.take("normalization");
                const QJsonValue associated_files = weight_object.take("associated_files");
                require_exact_fields(
                    weight_object,
                    {"id", "format", "precision", "role", "path",
                     "parent_weight_id", "created_at", "supported_devices",
                     "runtime_name", "runtime_version", "artifact_version",
                     "onnx_opset", "input_signature", "output_signature",
                     "target_architecture"},
                    "Task model weight");
                jocelin::model::ModelWeight weight;
                weight.id = weight_object.value("id").toString().toStdString();
                weight.format = jocelin::config::weight_type_from_string(
                    weight_object.value("format").toString().toStdString());
                weight.precision = jocelin::model::weight_precision_from_string(
                    weight_object.value("precision").toString().toStdString());
                weight.role = jocelin::model::weight_role_from_string(
                    weight_object.value("role").toString().toStdString());
                weight.path = ProjectStorage::runtime_path(
                    weight_object.value("path").toString(), root_directory);
                ModelWeightAssets::validate_associated_files(
                    associated_files, weight.format, QString::fromStdString(weight.path));
                weight.parent_weight_id =
                    weight_object.value("parent_weight_id").toString().toStdString();
                weight.created_at = weight_object.value("created_at").toString().toStdString();
                if (!weight_object.value("supported_devices").isArray())
                    throw std::runtime_error("Task model weight devices are invalid");
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
                weight.normalization = normalization_from_json(normalization_value, weight.format);
                if (!QDateTime::fromString(QString::fromStdString(weight.created_at), Qt::ISODate).isValid() ||
                    weight.onnx_opset < 0) {
                    throw std::runtime_error("Task model weight metadata is invalid");
                }
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
                throw std::runtime_error("Task model metadata is invalid");
            if ((model.creation_mode == jocelin::model::ModelCreationMode::Import) !=
                (model.training_status == jocelin::model::ModelTrainingStatus::Imported)) {
                throw std::runtime_error("Task model lifecycle metadata is invalid");
            }
            if (!object.value("classes").isArray())
                throw std::runtime_error("Task model classes are invalid");
            for (const auto class_value : object.value("classes").toArray()) {
                if (!class_value.isObject())
                    throw std::runtime_error("invalid Task model class entry");
                const auto class_object = class_value.toObject();
                require_exact_fields(class_object, {"id", "name", "mask_value"}, "Task model class");
                jocelin::model::ModelClass item{
                    class_object.value("id").toString().toStdString(),
                    class_object.value("name").toString().toStdString(),
                    class_object.value("mask_value").toInt()};
                if (item.id.empty() || item.name.empty() ||
                    (model.task == jocelin::config::ToolType::Sg && item.mask_value <= 0))
                    throw std::runtime_error("Task model class metadata is invalid");
                model.classes.push_back(std::move(item));
            }
            // Validate branch identities, roles and lineage without probing files.
            // Missing assets are diagnosed by ProjectManager after structural load.
            jocelin::model::ModelList validation;
            validation.add(model);
            models.push_back(std::move(model));
        }
        if (!model_numbers.empty() &&
            next_number <= *model_numbers.rbegin())
            throw std::runtime_error(
                "next model number must exceed all assigned numbers");
    }
    jocelin::model::ModelList result;
    result.restore(std::move(models), next_number);
    return result;
}

QByteArray ModelListStorage::serialize(
    const jocelin::model::ModelList& models,
    const QString& root_directory)
{
    QJsonArray model_entries;
    for (const auto& model : models.entries()) {
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
            QJsonObject weight_object{
                {"id", QString::fromStdString(weight.id)},
                {"format", QString::fromStdString(jocelin::config::to_string(weight.format))},
                {"precision", QString::fromStdString(jocelin::model::to_string(weight.precision))},
                {"role", QString::fromStdString(jocelin::model::to_string(weight.role))},
                {"path", ProjectStorage::portable_path(weight.path, root_directory)},
                {"parent_weight_id", QString::fromStdString(weight.parent_weight_id)},
                {"created_at", QString::fromStdString(weight.created_at)},
                {"supported_devices", supported_devices},
                {"runtime_name", QString::fromStdString(weight.runtime_name)},
                {"runtime_version", QString::fromStdString(weight.runtime_version)},
                {"artifact_version", QString::fromStdString(weight.artifact_version)},
                {"onnx_opset", weight.onnx_opset},
                {"input_signature", QString::fromStdString(weight.input_signature)},
                {"output_signature", QString::fromStdString(weight.output_signature)},
                {"target_architecture", QString::fromStdString(weight.target_architecture)}};
            weight_object.insert("normalization", normalization_to_json(weight.normalization, weight.format));
            weight_object.insert("associated_files",
                ModelWeightAssets::associated_files(weight.format, QString::fromStdString(weight.path)));
            weights.append(weight_object);
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
            {"asset_directory", ProjectStorage::portable_path(model.asset_directory, root_directory)},
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
    const QJsonObject document{
        {"models", model_entries},
        {"next_number", models.next_number()}};
    return QJsonDocument(document).toJson(QJsonDocument::Indented);
}

void ModelListStorage::save(const QByteArray& data, const QString& node_directory) {
    if (!QDir().mkpath(QDir(node_directory).filePath("models")))
        throw std::runtime_error("cannot create ModelList directory");
    ProjectStorage::write_atomic(
        QDir(node_directory).filePath(ModelListFileName),
        data);
}

} // namespace studio
