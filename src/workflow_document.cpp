#include "workflow_document.h"

#include "task_dataset_binding.h"

#include <common/config/ai_config.h>
#include <common/config/augment_catalog.h>

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

[[noreturn]] void invalid_document(
    const QString& context,
    const QString& detail)
{
    throw std::runtime_error(
        (context + ": " + detail).toStdString());
}

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
            invalid_document(
                context, "missing field '" + name + "'");
    }
    for (auto iterator = object.begin();
         iterator != object.end(); ++iterator) {
        if (expected.count(iterator.key()) == 0)
            invalid_document(
                context,
                "unexpected field '" + iterator.key() + "'");
    }
}

QJsonObject required_object(
    const QJsonObject& object,
    const char* field,
    const QString& context)
{
    const auto value = object.value(QString::fromLatin1(field));
    if (!value.isObject())
        invalid_document(
            context,
            "field '" + QString::fromLatin1(field) +
                "' must be an object");
    return value.toObject();
}

QJsonArray required_array(
    const QJsonObject& object,
    const char* field,
    const QString& context)
{
    const auto value = object.value(QString::fromLatin1(field));
    if (!value.isArray())
        invalid_document(
            context,
            "field '" + QString::fromLatin1(field) +
                "' must be an array");
    return value.toArray();
}

QString required_string(
    const QJsonObject& object,
    const char* field,
    const QString& context)
{
    const auto value = object.value(QString::fromLatin1(field));
    if (!value.isString())
        invalid_document(
            context,
            "field '" + QString::fromLatin1(field) +
                "' must be a string");
    return value.toString();
}

bool required_bool(
    const QJsonObject& object,
    const char* field,
    const QString& context)
{
    const auto value = object.value(QString::fromLatin1(field));
    if (!value.isBool())
        invalid_document(
            context,
            "field '" + QString::fromLatin1(field) +
                "' must be a boolean");
    return value.toBool();
}

double required_number(
    const QJsonObject& object,
    const char* field,
    const QString& context)
{
    const auto value = object.value(QString::fromLatin1(field));
    const double result = value.toDouble();
    if (!value.isDouble() || !std::isfinite(result))
        invalid_document(
            context,
            "field '" + QString::fromLatin1(field) +
                "' must be a finite number");
    return result;
}

int required_integer(
    const QJsonObject& object,
    const char* field,
    const QString& context)
{
    const double value =
        required_number(object, field, context);
    if (std::trunc(value) != value ||
        value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max())
        invalid_document(
            context,
            "field '" + QString::fromLatin1(field) +
                "' must be an integer");
    return static_cast<int>(value);
}

QJsonArray strings_to_json(const std::vector<std::string>& values) {
    QJsonArray array;
    for (const auto& value : values)
        array.append(QString::fromStdString(value));
    return array;
}

std::vector<std::string> strings_from_json(
    const QJsonValue& value,
    const QString& context,
    bool require_non_empty)
{
    if (!value.isArray())
        invalid_document(context, "must be an array");
    std::vector<std::string> values;
    int index = 0;
    for (const auto& entry : value.toArray()) {
        if (!entry.isString() ||
            (require_non_empty && entry.toString().isEmpty()))
            invalid_document(
                context,
                QString("entry %1 must be a%2 string")
                    .arg(index)
                    .arg(require_non_empty
                        ? " non-empty"
                        : ""));
        values.push_back(
            entry.toString().toStdString());
        ++index;
    }
    return values;
}

QJsonArray intervals_to_json(
    const std::vector<jocelin::geo::Interval>& intervals)
{
    QJsonArray result;
    for (const auto& interval : intervals)
        result.append(QJsonObject{{"low", interval.low}, {"high", interval.high}});
    return result;
}

std::vector<jocelin::geo::Interval> intervals_from_json(
    const QJsonObject& object,
    const char* field,
    const QString& context)
{
    std::vector<jocelin::geo::Interval> result;
    int index = 0;
    for (const auto& value : required_array(object, field, context)) {
        const QString interval_context = QString("%1 field '%2' entry %3")
            .arg(context, QString::fromLatin1(field)).arg(index++);
        if (!value.isObject()) invalid_document(interval_context, "must be an object");
        const auto interval = value.toObject();
        require_exact_fields(interval, {"low", "high"}, interval_context);
        result.push_back({
            required_number(interval, "low", interval_context),
            required_number(interval, "high", interval_context)});
    }
    return result;
}

QJsonObject augment_to_json(const jocelin::config::Augment& value) {
    const auto& enable = value.enable;
    return {
        {"probability", value.probability},
        {"enable", QJsonObject{
            {"scale", enable.scale},
            {"crop", enable.crop},
            {"brightness", enable.brightness},
            {"contrast", enable.contrast},
            {"saturation", enable.saturation},
            {"hue", enable.hue},
            {"gaussian_noise", enable.gaussian_noise},
            {"flip_horizontal", enable.flip_horizontal},
            {"flip_vertical", enable.flip_vertical},
            {"rotate", enable.rotate},
            {"translate", enable.translate},
            {"shear", enable.shear}}},
        {"scale_ranges", intervals_to_json(value.scale_ranges)},
        {"crop_ratio", value.crop_ratio},
        {"brightness_delta", value.brightness_delta},
        {"contrast_ranges", intervals_to_json(value.contrast_ranges)},
        {"saturation_ranges", intervals_to_json(value.saturation_ranges)},
        {"hue_delta", value.hue_delta},
        {"noise_mean", value.noise_mean},
        {"noise_std", value.noise_std},
        {"rotation_ranges", intervals_to_json(value.rotation_ranges)},
        {"translate_ratio", value.translate_ratio},
        {"shear_degrees", value.shear_degrees}};
}

jocelin::config::Augment augment_from_json(
    const QJsonObject& object,
    jocelin::config::ToolType task)
{
    const QString context = "Task training augment";
    require_exact_fields(
        object,
        {"probability", "enable", "scale_ranges", "crop_ratio",
         "brightness_delta", "contrast_ranges", "saturation_ranges", "hue_delta",
         "noise_mean", "noise_std", "rotation_ranges", "translate_ratio",
         "shear_degrees"},
        context);
    const auto enabled = required_object(object, "enable", context);
    require_exact_fields(
        enabled,
        {"scale", "crop", "brightness", "contrast", "saturation", "hue",
         "gaussian_noise", "flip_horizontal", "flip_vertical", "rotate",
         "translate", "shear"},
        context + " enable");

    jocelin::config::Augment result;
    result.probability = static_cast<float>(required_number(object, "probability", context));
    result.enable.scale = required_bool(enabled, "scale", context);
    result.enable.crop = required_bool(enabled, "crop", context);
    result.enable.brightness = required_bool(enabled, "brightness", context);
    result.enable.contrast = required_bool(enabled, "contrast", context);
    result.enable.saturation = required_bool(enabled, "saturation", context);
    result.enable.hue = required_bool(enabled, "hue", context);
    result.enable.gaussian_noise = required_bool(enabled, "gaussian_noise", context);
    result.enable.flip_horizontal = required_bool(enabled, "flip_horizontal", context);
    result.enable.flip_vertical = required_bool(enabled, "flip_vertical", context);
    result.enable.rotate = required_bool(enabled, "rotate", context);
    result.enable.translate = required_bool(enabled, "translate", context);
    result.enable.shear = required_bool(enabled, "shear", context);
    result.scale_ranges = intervals_from_json(object, "scale_ranges", context);
    result.crop_ratio = static_cast<float>(required_number(object, "crop_ratio", context));
    result.brightness_delta = static_cast<float>(required_number(object, "brightness_delta", context));
    result.contrast_ranges = intervals_from_json(object, "contrast_ranges", context);
    result.saturation_ranges = intervals_from_json(object, "saturation_ranges", context);
    result.hue_delta = static_cast<float>(required_number(object, "hue_delta", context));
    result.noise_mean = static_cast<float>(required_number(object, "noise_mean", context));
    result.noise_std = static_cast<float>(required_number(object, "noise_std", context));
    result.rotation_ranges = intervals_from_json(object, "rotation_ranges", context);
    result.translate_ratio = static_cast<float>(required_number(object, "translate_ratio", context));
    result.shear_degrees = static_cast<float>(required_number(object, "shear_degrees", context));
    try {
        jocelin::config::AugmentCatalog::validate(result, task);
    } catch (const std::exception& error) {
        invalid_document(context, QString::fromUtf8(error.what()));
    }
    return result;
}

QJsonObject binding_to_json(const TaskDatasetBinding& binding) {
    QJsonArray classes;
    for (const auto& value : binding.classes) {
        classes.append(QJsonObject{
            {"id", QString::fromStdString(value.id)},
            {"mask_value", value.mask_value},
            {"name", QString::fromStdString(value.name)},
            {"color", QString::fromStdString(value.color)},
            {"visible", value.visible},
            {"locked", value.locked},
            {"active", value.active}});
    }
    QJsonArray samples;
    for (const auto& value : binding.samples) {
        samples.append(QJsonObject{
            {"sample_id", QString::fromStdString(value.sample_id)},
            {"image_paths", strings_to_json(value.image_paths)},
            {"selected", value.selected},
            {"mask_path", QString::fromStdString(value.mask_path)},
            {"training_mask_path", QString::fromStdString(value.training_mask_path)},
            {"class_id", QString::fromStdString(value.class_id)},
            {"split", QString::fromStdString(value.split)},
            {"split_manual", value.split_manual},
            {"annotation_state", QString::fromStdString(value.annotation_state)},
            {"quality_issues", strings_to_json(value.quality_issues)}});
    }
    const auto& split = binding.split_settings;
    return {
        {"source_dataset_node_id", binding.source_dataset_node_id},
        {"preprocessing", QJsonObject{
            {"normalize", binding.preprocessing.normalize},
            {"bgr_to_rgb", binding.preprocessing.bgr_to_rgb}}},
        {"split_settings", QJsonObject{
            {"strategy", QString::fromStdString(split.strategy)},
            {"train_percent", split.train_percent},
            {"validation_percent", split.validation_percent},
            {"test_percent", split.test_percent},
            {"seed", static_cast<double>(split.seed)}}},
        {"classes", classes},
        {"samples", samples}};
}

TaskDatasetBinding binding_from_json(const QJsonObject& object) {
    const QString context = "Task dataset_binding";
    require_exact_fields(
        object,
        {"source_dataset_node_id", "preprocessing",
         "split_settings", "classes", "samples"},
        context);
    TaskDatasetBinding binding;
    binding.source_dataset_node_id =
        required_integer(
            object, "source_dataset_node_id", context);
    if (binding.source_dataset_node_id < 0)
        invalid_document(
            context,
            "source_dataset_node_id must be non-negative");
    const auto preprocessing =
        required_object(object, "preprocessing", context);
    require_exact_fields(
        preprocessing, {"normalize", "bgr_to_rgb"},
        "Task preprocessing");
    binding.preprocessing.normalize =
        required_bool(
            preprocessing, "normalize",
            "Task preprocessing");
    binding.preprocessing.bgr_to_rgb =
        required_bool(
            preprocessing, "bgr_to_rgb",
            "Task preprocessing");
    const auto split = required_object(
        object, "split_settings", context);
    require_exact_fields(
        split,
        {"strategy", "train_percent", "validation_percent",
         "test_percent", "seed"},
        "Task split settings");
    binding.split_settings.strategy =
        required_string(split, "strategy", "Task split settings").toStdString();
    binding.split_settings.train_percent =
        required_integer(split, "train_percent", "Task split settings");
    binding.split_settings.validation_percent =
        required_integer(split, "validation_percent", "Task split settings");
    binding.split_settings.test_percent =
        required_integer(split, "test_percent", "Task split settings");
    const int seed = required_integer(split, "seed", "Task split settings");
    if (binding.split_settings.strategy != "stratified" ||
        binding.split_settings.train_percent < 0 ||
        binding.split_settings.validation_percent < 0 ||
        binding.split_settings.test_percent < 0 ||
        binding.split_settings.train_percent +
                binding.split_settings.validation_percent +
                binding.split_settings.test_percent != 100 ||
        seed < 0)
        invalid_document(
            "Task split settings",
            "strategy, percentages, or seed are invalid");
    binding.split_settings.seed = static_cast<std::uint32_t>(seed);

    std::set<std::string> class_ids;
    std::set<int> mask_values;
    int class_index = 0;
    for (const auto& entry :
         required_array(object, "classes", context)) {
        const QString class_context =
            QString("Task class %1").arg(class_index++);
        if (!entry.isObject())
            invalid_document(
                class_context, "must be an object");
        const auto value = entry.toObject();
        require_exact_fields(
            value,
            {"id", "mask_value", "name", "color",
             "visible", "locked", "active"},
            class_context);
        AnnotationClass annotation_class;
        annotation_class.id =
            required_string(
                value, "id", class_context).toStdString();
        annotation_class.mask_value =
            required_integer(
                value, "mask_value", class_context);
        annotation_class.name =
            required_string(
                value, "name", class_context).toStdString();
        annotation_class.color =
            required_string(
                value, "color", class_context).toStdString();
        annotation_class.visible =
            required_bool(
                value, "visible", class_context);
        annotation_class.locked =
            required_bool(
                value, "locked", class_context);
        annotation_class.active =
            required_bool(
                value, "active", class_context);
        if (annotation_class.id.empty() ||
            annotation_class.name.empty() ||
            annotation_class.color.empty() ||
            annotation_class.mask_value < 1 ||
            annotation_class.mask_value > 255)
            invalid_document(
                class_context,
                "identity, name, color, or mask value is invalid");
        if (!class_ids.insert(annotation_class.id).second ||
            !mask_values.insert(
                annotation_class.mask_value).second)
            invalid_document(
                class_context,
                "class ids and mask values must be unique");
        binding.classes.push_back(
            std::move(annotation_class));
    }

    std::set<std::string> sample_ids;
    int sample_index = 0;
    for (const auto& entry :
         required_array(object, "samples", context)) {
        const QString sample_context =
            QString("Task sample %1").arg(sample_index++);
        if (!entry.isObject())
            invalid_document(
                sample_context, "must be an object");
        const auto value = entry.toObject();
        require_exact_fields(
            value,
            {"sample_id", "image_paths", "selected",
             "mask_path", "training_mask_path", "class_id", "split",
             "split_manual", "annotation_state", "quality_issues"},
            sample_context);
        TaskSampleBinding sample;
        sample.sample_id =
            required_string(
                value, "sample_id",
                sample_context).toStdString();
        sample.image_paths =
            strings_from_json(
                value.value("image_paths"),
                sample_context + " image_paths", true);
        sample.selected =
            required_bool(
                value, "selected", sample_context);
        sample.mask_path =
            required_string(
                value, "mask_path",
                sample_context).toStdString();
        sample.training_mask_path =
            required_string(
                value, "training_mask_path",
                sample_context).toStdString();
        sample.class_id =
            required_string(
                value, "class_id",
                sample_context).toStdString();
        sample.split =
            normalized_split(
                required_string(
                    value, "split",
                    sample_context).toStdString());
        sample.split_manual =
            required_bool(value, "split_manual", sample_context);
        sample.annotation_state =
            required_string(
                value, "annotation_state",
                sample_context).toStdString();
        if (sample.annotation_state != "unannotated" &&
            sample.annotation_state != "annotated" &&
            sample.annotation_state != "background-only" &&
            sample.annotation_state != "needs-review")
            invalid_document(
                sample_context,
                "annotation_state must be unannotated, background-only, annotated, or needs-review");
        sample.quality_issues =
            strings_from_json(
                value.value("quality_issues"),
                sample_context + " quality_issues", true);
        if (sample.sample_id.empty() ||
            sample.image_paths.empty() ||
            !sample_ids.insert(sample.sample_id).second)
            invalid_document(
                sample_context,
                "sample identity and image paths must be present and unique");
        if (!sample.class_id.empty() &&
            class_ids.count(sample.class_id) == 0)
            invalid_document(
                sample_context,
                "class_id does not reference a Task class");
        binding.samples.push_back(std::move(sample));
    }
    return binding;
}

QJsonObject result_metrics_to_json(const ResultMetrics& metrics) {
    return {
        {"available", metrics.available},
        {"precision", metrics.precision},
        {"recall", metrics.recall},
        {"f1", metrics.f1}};
}

ResultMetrics result_metrics_from_json(const QJsonObject& object, const QString& context) {
    require_exact_fields(object, {"available", "precision", "recall", "f1"}, context);
    ResultMetrics metrics;
    metrics.available = required_bool(object, "available", context);
    metrics.precision = required_number(object, "precision", context);
    metrics.recall = required_number(object, "recall", context);
    metrics.f1 = required_number(object, "f1", context);
    return metrics;
}

QJsonObject runtime_to_json(const TaskRuntimeState& runtime) {
    QJsonArray results;
    for (const auto& set : runtime.results) {
        QJsonArray samples;
        for (const auto& value : set.samples) {
            samples.append(QJsonObject{
                {"result_id", QString::fromStdString(value.result_id)},
                {"source_sample_id", QString::fromStdString(value.source_sample_id)},
                {"source_image_path", QString::fromStdString(value.source_image_path)},
                {"predicted_class", value.predicted_class},
                {"confidence", value.confidence},
                {"predicted_area", static_cast<double>(value.predicted_area)},
                {"metrics", result_metrics_to_json(value.metrics)},
                {"mask_path", QString::fromStdString(value.mask_path)},
                {"visualization_path", QString::fromStdString(value.visualization_path)},
                {"metadata_path", QString::fromStdString(value.metadata_path)}});
        }
        results.append(QJsonObject{
            {"model_id", QString::fromStdString(set.model_id)},
            {"weight_id", QString::fromStdString(set.weight_id)},
            {"weight_format", QString::fromStdString(set.weight_format)},
            {"result_directory", QString::fromStdString(set.result_directory)},
            {"aggregate_metrics", result_metrics_to_json(set.aggregate_metrics)},
            {"samples", samples}});
    }
    return {
        {"training_model_id", QString::fromStdString(runtime.training_model_id)},
        {"inference_status", QString::fromStdString(runtime.inference_status)},
        {"results", results}};
}

TaskRuntimeState runtime_from_json(const QJsonObject& object) {
    const QString context = "Task runtime";
    require_exact_fields(
        object,
        {"training_model_id", "inference_status", "results"},
        context);
    TaskRuntimeState runtime;
    runtime.training_model_id =
        required_string(object, "training_model_id", context).toStdString();
    runtime.inference_status =
        required_string(
            object, "inference_status",
            context).toStdString();
    if (runtime.inference_status.empty())
        invalid_document(
            context, "inference status cannot be empty");

    const auto result_array = required_array(object, "results", context);
    std::set<std::string> result_keys;
    int result_index = 0;
    for (const auto& entry : result_array) {
        const QString result_context =
            QString("Task result set %1").arg(result_index++);
        if (!entry.isObject())
            invalid_document(result_context, "must be an object");
        const auto value = entry.toObject();
        require_exact_fields(
            value,
            {"model_id", "weight_id", "weight_format", "result_directory",
             "aggregate_metrics", "samples"},
            result_context);

        InferenceResultSet set;
        set.model_id = required_string(value, "model_id", result_context).toStdString();
        set.weight_id = required_string(value, "weight_id", result_context).toStdString();
        set.weight_format = required_string(
            value, "weight_format", result_context).toStdString();
        if (set.weight_format != "libtorch" && set.weight_format != "onnx" &&
            set.weight_format != "tensorrt")
            invalid_document(result_context, "unknown weight_format");
        set.result_directory = required_string(
            value, "result_directory", result_context).toStdString();
        set.aggregate_metrics = result_metrics_from_json(
            required_object(value, "aggregate_metrics", result_context),
            result_context + " aggregate_metrics");
        const std::string key = set.model_id + ":" + set.weight_format;
        if (set.model_id.empty() || set.weight_id.empty() ||
            set.result_directory.empty() || !result_keys.insert(key).second)
            invalid_document(
                result_context,
                "model/weight-format result identity must be unique and complete");

        std::set<std::string> sample_ids;
        int sample_index = 0;
        for (const auto& sample_entry :
             required_array(value, "samples", result_context)) {
            const QString sample_context =
                QString("%1 sample %2").arg(result_context).arg(sample_index++);
            if (!sample_entry.isObject())
                invalid_document(sample_context, "must be an object");
            const auto sample = sample_entry.toObject();
            require_exact_fields(
                sample,
                {"result_id", "source_sample_id", "source_image_path",
                 "predicted_class", "confidence", "predicted_area", "metrics",
                 "mask_path", "visualization_path", "metadata_path"},
                sample_context);
            ResultReference result;
            result.result_id = required_string(
                sample, "result_id", sample_context).toStdString();
            result.source_sample_id = required_string(
                sample, "source_sample_id", sample_context).toStdString();
            result.source_image_path = required_string(
                sample, "source_image_path", sample_context).toStdString();
            result.predicted_class = required_integer(
                sample, "predicted_class", sample_context);
            result.confidence = required_number(
                sample, "confidence", sample_context);
            result.predicted_area = static_cast<std::int64_t>(
                required_number(sample, "predicted_area", sample_context));
            result.metrics = result_metrics_from_json(
                required_object(sample, "metrics", sample_context),
                sample_context + " metrics");
            result.mask_path = required_string(
                sample, "mask_path", sample_context).toStdString();
            result.visualization_path = required_string(
                sample, "visualization_path", sample_context).toStdString();
            result.metadata_path = required_string(
                sample, "metadata_path", sample_context).toStdString();
            if (result.result_id.empty() ||
                !sample_ids.insert(result.result_id).second)
                invalid_document(
                    sample_context,
                    "result_id must be present and unique within a result set");
            set.samples.push_back(std::move(result));
        }
        runtime.results.push_back(std::move(set));
    }
    return runtime;
}

QJsonObject config_to_json(const Node& node) {
    if (node.kind == NodeKind::DatasetInput) {
        const auto& config = std::get<DatasetConfig>(node.config);
        QJsonArray items;
        for (const auto& item : config.items) {
            items.append(QJsonObject{
                {"path", QString::fromStdString(item.path)},
                {"valid", item.valid},
                {"error", QString::fromStdString(item.error)},
                {"sample_id", QString::fromStdString(item.sample_id)}});
        }
        return {
            {"directory", QString::fromStdString(config.directory)},
            {"items", items},
            {"bucket_capacity", config.bucket_capacity}};
    }
    if (node.kind == NodeKind::TaskConfig) {
        const auto& config = std::get<TaskNodeConfig>(node.config);
        const auto& training = config.training;
        QJsonArray inference_images;
        for (const auto& name : config.inference_images)
            inference_images.append(QString::fromStdString(name));
        return {
            {"task", QString::fromStdString(config.task)},
            {"model_index", config.model_index},
            {"model_scale", QString::fromStdString(config.model_scale)},
            {"device", QJsonObject{
                {"type", QString::fromStdString(jocelin::config::to_string(config.device.type))},
                {"index", config.device.index},
                {"id", QString::fromStdString(config.device.id)}}},
            {"inference_batch_size", config.inference_batch_size},
            {"inference_output_directory", QString::fromStdString(config.inference_output_directory)},
            {"inference_use_all_images", config.inference_use_all_images},
            {"inference_images", inference_images},
            {"dataset_binding", binding_to_json(config.dataset_binding)},
            {"runtime", runtime_to_json(config.runtime)},
            {"training", QJsonObject{
                {"batch_size", training.batch_size},
                {"epochs", training.epochs},
                {"learning_rate", training.learning_rate},
                {"weight_decay", training.weight_decay},
                {"image_width", training.image_width},
                {"image_height", training.image_height},
                {"workers", training.workers},
                {"optimizer", QString::fromStdString(training.optimizer)},
                {"scheduler", QString::fromStdString(training.scheduler)},
                {"augment", augment_to_json(training.augment)},
                {"output_directory", QString::fromStdString(training.output_directory)}}}};
    }
    throw std::runtime_error("unsupported node kind");
}

void config_from_json(Node& node, const QJsonObject& object) {
    if (node.kind == NodeKind::DatasetInput) {
        const QString context = "Dataset config";
        require_exact_fields(
            object,
            {"directory", "items", "bucket_capacity"},
            context);
        DatasetConfig config;
        config.directory =
            required_string(
                object, "directory",
                context).toStdString();
        int item_index = 0;
        for (const auto value :
             required_array(object, "items", context)) {
            const QString item_context =
                QString("Dataset item %1").arg(item_index++);
            if (!value.isObject())
                invalid_document(
                    item_context, "must be an object");
            const auto item = value.toObject();
            require_exact_fields(
                item,
                {"path", "valid", "error", "sample_id"},
                item_context);
            const std::string path =
                required_string(
                    item, "path",
                    item_context).toStdString();
            if (path.empty())
                invalid_document(
                    item_context, "path cannot be empty");
            config.items.push_back(DatasetConfig::Item{
                path,
                required_string(
                    item, "error",
                    item_context).toStdString(),
                required_bool(
                    item, "valid", item_context),
                required_string(
                    item, "sample_id",
                    item_context).toStdString()});
        }
        config.bucket_capacity =
            required_integer(
                object, "bucket_capacity", context);
        if (config.bucket_capacity < 1)
            invalid_document(
                context,
                "bucket_capacity must be at least one");
        node.config = std::move(config);
    } else if (node.kind == NodeKind::TaskConfig) {
        const QString context = "Task config";
        require_exact_fields(
            object,
            {"task", "model_index", "model_scale",
             "device", "inference_batch_size",
             "inference_output_directory",
             "inference_use_all_images",
             "inference_images", "dataset_binding",
             "runtime", "training"},
            context);
        const auto training =
            required_object(object, "training", context);
        require_exact_fields(
            training,
            {"batch_size", "epochs", "learning_rate",
             "weight_decay", "image_width",
             "image_height", "workers", "optimizer",
             "scheduler", "augment", "output_directory"},
            "Task training");
        TaskNodeConfig config;
        config.task =
            required_string(
                object, "task", context).toStdString();
        config.model_index =
            required_integer(
                object, "model_index", context);
        config.model_scale =
            required_string(
                object, "model_scale",
                context).toStdString();
        const auto device = required_object(object, "device", context);
        require_exact_fields(device, {"type", "index", "id"}, "Task device");
        config.device.type = jocelin::config::device_type_from_string(
            required_string(device, "type", "Task device").toStdString());
        config.device.index = required_integer(device, "index", "Task device");
        config.device.id = required_string(device, "id", "Task device").toStdString();
        if (config.device.id.empty() ||
            (config.device.type == jocelin::config::DeviceType::Cpu && config.device.index != -1) ||
            (config.device.type == jocelin::config::DeviceType::Gpu && config.device.index < 0))
            invalid_document(context, "device identity is invalid");
        config.training.batch_size =
            required_integer(
                training, "batch_size", "Task training");
        config.training.epochs =
            required_integer(
                training, "epochs", "Task training");
        const double learning_rate =
            required_number(
                training, "learning_rate",
                "Task training");
        const double weight_decay =
            required_number(
                training, "weight_decay",
                "Task training");
        if (std::abs(learning_rate) >
                std::numeric_limits<float>::max() ||
            std::abs(weight_decay) >
                std::numeric_limits<float>::max())
            invalid_document(
                "Task training",
                "floating-point settings exceed the supported range");
        config.training.learning_rate =
            static_cast<float>(learning_rate);
        config.training.weight_decay =
            static_cast<float>(weight_decay);
        config.training.image_width =
            required_integer(
                training, "image_width",
                "Task training");
        config.training.image_height =
            required_integer(
                training, "image_height",
                "Task training");
        config.training.workers =
            required_integer(
                training, "workers",
                "Task training");
        config.training.optimizer =
            required_string(
                training, "optimizer",
                "Task training").toStdString();
        config.training.scheduler =
            required_string(
                training, "scheduler",
                "Task training").toStdString();
        config.training.augment = augment_from_json(
            required_object(training, "augment", "Task training"),
            jocelin::config::tool_type_from_string(config.task));
        config.training.output_directory =
            required_string(
                training, "output_directory",
                "Task training").toStdString();
        config.inference_batch_size =
            required_integer(
                object, "inference_batch_size",
                context);
        config.inference_output_directory =
            required_string(
                object, "inference_output_directory",
                context).toStdString();
        config.inference_use_all_images =
            required_bool(
                object, "inference_use_all_images",
                context);
        config.inference_images =
            strings_from_json(
                object.value("inference_images"),
                "Task inference_images", true);
        config.dataset_binding =
            binding_from_json(
                required_object(
                    object, "dataset_binding",
                    context));
        config.runtime =
            runtime_from_json(
                required_object(
                    object, "runtime", context));
        node.config = std::move(config);
    } else {
        throw std::runtime_error("unsupported node kind");
    }
}

} // namespace

QByteArray serialize_workflow(
    const WorkflowGraph& graph,
    const std::map<int, QPointF>& positions)
{
    QJsonObject root;
    root["format"] = "deep_learning_workflow";
    QJsonArray nodes;
    for (const auto& [id, node] : graph.nodes()) {
        const auto position = positions.find(id);
        if (position == positions.end())
            throw std::runtime_error("node position is missing for node " + std::to_string(id));
        nodes.append(QJsonObject{
            {"id", id},
            {"kind", static_cast<int>(node.kind)},
            {"x", position->second.x()},
            {"y", position->second.y()},
            {"config", config_to_json(node)}});
    }
    QJsonArray connections;
    for (const auto& edge : graph.connections()) {
        connections.append(QJsonObject{
            {"from_node", edge.from_node},
            {"from_port", static_cast<int>(edge.from_port)},
            {"to_node", edge.to_node},
            {"to_port", static_cast<int>(edge.to_port)}});
    }
    root["nodes"] = nodes;
    root["connections"] = connections;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

WorkflowDocument parse_workflow(const QByteArray& data) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error("invalid workflow JSON: " + parseError.errorString().toStdString());
    const auto root = document.object();
    require_exact_fields(
        root, {"format", "nodes", "connections"},
        "Workflow document");
    if (required_string(
            root, "format", "Workflow document") !=
        "deep_learning_workflow")
        throw std::runtime_error("unsupported workflow format");

    WorkflowDocument result;
    std::map<int, int> id_map;
    std::map<int, int> serialized_binding_sources;
    int node_index = 0;
    for (const auto value :
         required_array(
             root, "nodes", "Workflow document")) {
        const QString context =
            QString("Workflow node %1").arg(node_index++);
        if (!value.isObject()) throw std::runtime_error("invalid node entry");
        const auto object = value.toObject();
        require_exact_fields(
            object, {"id", "kind", "x", "y", "config"},
            context);
        const int old_id =
            required_integer(object, "id", context);
        const int kind_value =
            required_integer(object, "kind", context);
        if (old_id < 1 || id_map.count(old_id) != 0 ||
            kind_value < static_cast<int>(NodeKind::DatasetInput) ||
            kind_value > static_cast<int>(NodeKind::TaskConfig))
            throw std::runtime_error("invalid node id or kind");
        const auto config_object =
            required_object(object, "config", context);
        const auto kind = static_cast<NodeKind>(kind_value);
        const int new_id = kind == NodeKind::TaskConfig
            ? result.graph.add_task_node({
                  required_string(config_object, "task", "Task config").toStdString()})
            : result.graph.add_node(kind);
        config_from_json(result.graph.node(new_id), config_object);
        if (static_cast<NodeKind>(kind_value) ==
            NodeKind::TaskConfig)
            serialized_binding_sources.emplace(
                new_id,
                std::get<TaskNodeConfig>(
                    result.graph.node(new_id).config)
                    .dataset_binding.source_dataset_node_id);
        id_map.emplace(old_id, new_id);
        result.positions[new_id] = QPointF(
            required_number(object, "x", context),
            required_number(object, "y", context));
    }
    std::set<int> connected_tasks;
    int connection_index = 0;
    for (const auto value :
         required_array(
             root, "connections",
             "Workflow document")) {
        const QString context =
            QString("Workflow connection %1")
                .arg(connection_index++);
        if (!value.isObject()) throw std::runtime_error("invalid connection entry");
        const auto object = value.toObject();
        require_exact_fields(
            object,
            {"from_node", "from_port",
             "to_node", "to_port"},
            context);
        const int from_node =
            required_integer(
                object, "from_node", context);
        const int from_port =
            required_integer(
                object, "from_port", context);
        const int to_node =
            required_integer(
                object, "to_node", context);
        const int to_port =
            required_integer(
                object, "to_port", context);
        if (id_map.count(from_node) == 0 || id_map.count(to_node) == 0 || from_port < 0 || to_port < 0)
            throw std::runtime_error("connection references an invalid node or port");
        const int mapped_task = id_map.at(to_node);
        const auto binding_source =
            serialized_binding_sources.find(mapped_task);
        if (binding_source ==
                serialized_binding_sources.end() ||
            binding_source->second != from_node)
            invalid_document(
                context,
                "Task dataset_binding does not match its Dataset connection");
        result.graph.connect(
            id_map.at(from_node), static_cast<size_t>(from_port),
            id_map.at(to_node), static_cast<size_t>(to_port));
        connected_tasks.insert(mapped_task);
    }
    for (const auto& [task_id, source_id] :
         serialized_binding_sources) {
        const bool connected =
            connected_tasks.count(task_id) != 0;
        if ((source_id == 0) != !connected)
            invalid_document(
                "Task dataset_binding",
                "source_dataset_node_id does not match the workflow connection");
    }
    return result;
}

} // namespace studio
