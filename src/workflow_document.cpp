#include "workflow_document.h"

#include "task_dataset_binding.h"
#include "task_module_catalog.h"
#include "project_storage.h"
#include "segmentation_annotation_storage.h"

#include <common/config/ai_config.h>
#include <common/config/augment_catalog.h>

#include <QJsonArray>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

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

QString region_file_name(int node_id, const std::string& sample_id) {
    return QString::number(node_id) + "/annotations/" +
        QString::fromLatin1(QString::fromStdString(sample_id).toUtf8().toHex()) + ".regions.json";
}

QJsonObject binding_to_json(const TaskDatasetBinding& binding, const std::string& task, int node_id,
    std::vector<std::pair<QString, QByteArray>>& region_assets) {
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
        QJsonObject sample{
            {"sample_id", QString::fromStdString(value.sample_id)},
            {"image_paths", strings_to_json(value.image_paths)},
            {"selected", value.selected},
            {"training_mask_path", QString::fromStdString(value.training_mask_path)},
            {"split", QString::fromStdString(value.split)},
            {"split_manual", value.split_manual},
            {"annotation_state", QString::fromStdString(value.annotation_state)}};
        if (task == "cls") sample.insert("class_id", QString::fromStdString(value.class_id));
        else {
            QString file;
            if (!value.regions.empty()) {
                file = region_file_name(node_id, value.sample_id);
                region_assets.emplace_back(file, encode_segmentation_regions(value.regions));
            }
            sample.insert("region_file", file);
        }
        samples.append(sample);
    }
    const auto& split = binding.split_settings;
    return {
        {"bucket_capacity", binding.bucket_capacity},
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

TaskDatasetBinding binding_from_json(const QJsonObject& object, const std::string& task, int node_id, const QString& node_asset_root) {
    const QString context = "Task dataset_binding";
    require_exact_fields(
        object,
        {"bucket_capacity", "preprocessing", "split_settings", "classes", "samples"},
        context);
    TaskDatasetBinding binding;
    binding.bucket_capacity = required_integer(object, "bucket_capacity", context);
    if (binding.bucket_capacity < 0)
        invalid_document(context, "bucket_capacity must be non-negative");
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
            (task == "sg" && (annotation_class.mask_value < 1 ||
            annotation_class.mask_value > 255)))
            invalid_document(
                class_context,
                "identity, name, color, or mask value is invalid");
        if (!class_ids.insert(annotation_class.id).second ||
            (task == "sg" && !mask_values.insert(
                annotation_class.mask_value).second))
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
        auto fields = value;
        const char* target_field = task == "cls" ? "class_id" : "region_file";
        if (!fields.contains(target_field)) invalid_document(sample_context, "missing task target");
        fields.remove(target_field);
        require_exact_fields(fields, {"sample_id", "image_paths", "selected", "training_mask_path", "split",
            "split_manual", "annotation_state"}, sample_context);
        TaskSampleBinding sample;
        sample.image_paths = strings_from_json(required_array(value, "image_paths", sample_context), sample_context, true);
        sample.sample_id =
            required_string(
                value, "sample_id",
                sample_context).toStdString();
        sample.selected =
            required_bool(
                value, "selected", sample_context);
        sample.training_mask_path =
            required_string(
                value, "training_mask_path",
                sample_context).toStdString();
        if (task == "cls") sample.class_id = required_string(value, "class_id", sample_context).toStdString();
        else {
            const QString file = required_string(value, "region_file", sample_context);
            if (!file.isEmpty()) {
                if (node_asset_root.isEmpty() || file != region_file_name(node_id, sample.sample_id))
                    invalid_document(sample_context, "region asset must belong to this Node and sample");
                sample.regions = load_segmentation_regions(QDir(node_asset_root).filePath(file));
            }
        }
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
            sample.annotation_state != "annotated")
            invalid_document(
                sample_context,
                "annotation_state must be unannotated or annotated");
        if (sample.sample_id.empty() ||
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

QJsonObject result_metrics_to_json(const jocelin::result::cls::ClsScopeMetrics& metrics) {
    QJsonObject object{
        {"available", metrics.available},
        {"precision", metrics.precision},
        {"recall", metrics.recall},
        {"f1", metrics.f1}};
    object.insert("accuracy", metrics.accuracy);
    return object;
}

jocelin::result::cls::ClsScopeMetrics result_metrics_from_json(
    const QJsonObject& object, const QString& context)
{
    require_exact_fields(object, {"available", "precision", "recall", "f1", "accuracy"}, context);
    jocelin::result::cls::ClsScopeMetrics metrics;
    metrics.available = required_bool(object, "available", context);
    metrics.precision = required_number(object, "precision", context);
    metrics.recall = required_number(object, "recall", context);
    metrics.f1 = required_number(object, "f1", context);
    metrics.accuracy = required_number(object, "accuracy", context);
    return metrics;
}

QJsonObject runtime_to_json(const TaskRuntimeState& runtime) {
    QJsonArray results;
    for (const auto& set : runtime.results) {
        QJsonArray samples;
        for (const auto& value : set.samples) {
            const auto payload = std::visit([](const auto& result) {
                QJsonObject data{
                    {"image_path", QString::fromStdString(result.image_path)},
                    {"result_name", QString::fromStdString(result.result_name)},
                    {"save_dir", QString::fromStdString(result.save_dir)},
                    {"metadata_path", QString::fromStdString(result.metadata_path)}};
                using Result = std::decay_t<decltype(result)>;
                if constexpr (std::is_same_v<Result, jocelin::result::cls::ClsResult>) {
                    QJsonArray probabilities;
                    for (double probability : result.probabilities) probabilities.append(probability);
                    data.insert("predicted_class", result.predicted_class);
                    data.insert("confidence", result.confidence);
                    data.insert("probabilities", probabilities);
                    data.insert("confidence_threshold", result.confidence_threshold);
                    data.insert("confidence_pass", result.confidence_pass);
                    data.insert("interest", result.interest);
                    data.insert("heatmap_class", result.heatmap_class);
                    data.insert("heatmap_path", QString::fromStdString(result.heatmap_path));
                } else {
                    QJsonArray components;
                    for (const auto& component : result.components) {
                        QJsonArray runs;
                        for (const auto& run : component.region.vRun)
                            runs.append(QJsonArray{run.row, run.start, run.end, run.label});
                        components.append(QJsonObject{
                            {"label", component.label}, {"area", static_cast<double>(component.area)},
                            {"bbox", QJsonArray{component.bbox.min_row, component.bbox.min_col,
                                component.bbox.max_row, component.bbox.max_col}},
                            {"aspect_ratio", component.aspect_ratio}, {"confidence", component.confidence},
                            {"area_pass", component.area_pass}, {"aspect_ratio_pass", component.aspect_ratio_pass},
                            {"confidence_pass", component.confidence_pass}, {"interest", component.interest},
                            {"runs", runs}});
                    }
                    QJsonArray labels;
                    for (int label : result.present_labels) labels.append(label);
                    data.insert("components", components);
                    data.insert("present_labels", labels);
                    data.insert("image_width", result.image_width);
                    data.insert("image_height", result.image_height);
                    data.insert("morph_kernel_size", result.morph_kernel_size);
                    data.insert("morph_iterations", result.morph_iterations);
                    data.insert("min_component_area", result.min_component_area);
                    data.insert("max_component_area", QString::number(static_cast<qlonglong>(result.max_component_area)));
                    data.insert("aspect_ratio_range", QJsonArray{result.aspect_ratio_range.low, result.aspect_ratio_range.high});
                    data.insert("min_component_confidence", result.min_component_confidence);
                    data.insert("confidence_threshold", result.confidence_threshold);
                    data.insert("save_option", static_cast<int>(result.save_option));
                    data.insert("save_color_mask", result.save_color_mask);
                    data.insert("overlay_alpha", result.overlay_alpha);
                    data.insert("mean_confidence", result.mean_confidence);
                    data.insert("predicted_area", static_cast<double>(result.predicted_area));
                    data.insert("metrics", QJsonObject{
                        {"available", result.metrics.available}, {"precision", result.metrics.precision},
                        {"recall", result.metrics.recall}, {"f1", result.metrics.f1}, {"mean_iou", result.metrics.mean_iou}});
                    data.insert("mask_path", QString::fromStdString(result.mask_path));
                    data.insert("visualization_path", QString::fromStdString(result.visualization_path));
                }
                return data;
            }, value.result);
            samples.append(QJsonObject{
                {"source_sample_id", QString::fromStdString(value.source_sample_id)},
                {"task", std::holds_alternative<jocelin::result::cls::ClsResult>(value.result) ? "cls" : "sg"},
                {"result", payload}});
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

TaskRuntimeState runtime_from_json(const QJsonObject& object, const std::string& task_name) {
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
            set.weight_format != "tensorrt" && set.weight_format != "openvino")
            invalid_document(result_context, "unknown weight_format");
        set.result_directory = required_string(
            value, "result_directory", result_context).toStdString();
        set.aggregate_metrics = result_metrics_from_json(
            required_object(value, "aggregate_metrics", result_context),
            result_context + " aggregate_metrics");
        const std::string key = set.model_id + ":" + set.weight_id;
        if (set.model_id.empty() || set.weight_id.empty() ||
            set.result_directory.empty() || !result_keys.insert(key).second)
            invalid_document(
                result_context,
                "model/weight result coordinate must be unique and complete");

        int sample_index = 0;
        for (const auto& sample_entry :
             required_array(value, "samples", result_context)) {
            const QString sample_context =
                QString("%1 sample %2").arg(result_context).arg(sample_index++);
            if (!sample_entry.isObject())
                invalid_document(sample_context, "must be an object");
            const auto sample = sample_entry.toObject();
            require_exact_fields(sample, {"source_sample_id", "task", "result"}, sample_context);
            ResultReference result;
            result.source_sample_id = required_string(sample, "source_sample_id", sample_context).toStdString();
            const auto task = required_string(sample, "task", sample_context);
            if (task.toStdString() != task_name)
                invalid_document(sample_context, "result task does not match the Task node");
            const auto data = required_object(sample, "result", sample_context);
            if (task == "cls") result.result = jocelin::result::cls::ClsResult{};
            else if (task == "sg") result.result = jocelin::result::sg::SgResult{};
            else invalid_document(sample_context, "unknown result task");
            std::visit([&](auto& value) {
                value.image_path = required_string(data, "image_path", sample_context).toStdString();
                value.result_name = required_string(data, "result_name", sample_context).toStdString();
                value.save_dir = required_string(data, "save_dir", sample_context).toStdString();
                value.metadata_path = required_string(data, "metadata_path", sample_context).toStdString();
                using Result = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Result, jocelin::result::cls::ClsResult>) {
                    require_exact_fields(data, {"image_path", "result_name", "save_dir", "metadata_path",
                        "predicted_class", "confidence", "probabilities", "confidence_threshold",
                        "confidence_pass", "interest", "heatmap_class", "heatmap_path"}, sample_context);
                    value.predicted_class = required_integer(data, "predicted_class", sample_context);
                    value.confidence = required_number(data, "confidence", sample_context);
                    for (const auto& probability : required_array(data, "probabilities", sample_context)) {
                        if (!probability.isDouble() || !std::isfinite(probability.toDouble()))
                            invalid_document(sample_context, "probabilities must be finite numbers");
                        value.probabilities.push_back(probability.toDouble());
                    }
                    value.confidence_threshold = required_number(data, "confidence_threshold", sample_context);
                    value.confidence_pass = required_bool(data, "confidence_pass", sample_context);
                    value.interest = required_bool(data, "interest", sample_context);
                    value.heatmap_class = required_integer(data, "heatmap_class", sample_context);
                    value.heatmap_path = required_string(data, "heatmap_path", sample_context).toStdString();
                } else {
                    require_exact_fields(data, {"image_path", "result_name", "save_dir", "metadata_path",
                        "components", "present_labels", "image_width", "image_height", "morph_kernel_size",
                        "morph_iterations", "min_component_area", "max_component_area", "aspect_ratio_range",
                        "min_component_confidence", "confidence_threshold", "save_option", "save_color_mask",
                        "overlay_alpha", "mean_confidence", "predicted_area", "metrics", "mask_path",
                        "visualization_path"}, sample_context);
                    value.image_width = required_integer(data, "image_width", sample_context);
                    value.image_height = required_integer(data, "image_height", sample_context);
                    value.morph_kernel_size = required_integer(data, "morph_kernel_size", sample_context);
                    value.morph_iterations = required_integer(data, "morph_iterations", sample_context);
                    value.min_component_area = required_integer(data, "min_component_area", sample_context);
                    bool area_valid = false;
                    value.max_component_area = required_string(data, "max_component_area", sample_context).toLongLong(&area_valid);
                    if (!area_valid) invalid_document(sample_context, "max_component_area must be an int64 string");
                    const auto range = required_array(data, "aspect_ratio_range", sample_context);
                    if (range.size() != 2 || !range[0].isDouble() || !range[1].isDouble() ||
                        !std::isfinite(range[0].toDouble()) || !std::isfinite(range[1].toDouble()))
                        invalid_document(sample_context, "aspect_ratio_range must contain two finite numbers");
                    value.aspect_ratio_range = {range[0].toDouble(), range[1].toDouble()};
                    value.min_component_confidence = required_number(data, "min_component_confidence", sample_context);
                    value.confidence_threshold = static_cast<float>(required_number(data, "confidence_threshold", sample_context));
                    const int option = required_integer(data, "save_option", sample_context);
                    if (option < 1 || option > 3) invalid_document(sample_context, "unknown SG save_option");
                    value.save_option = static_cast<jocelin::result::sg::SaveOption>(option);
                    value.save_color_mask = required_bool(data, "save_color_mask", sample_context);
                    value.overlay_alpha = static_cast<float>(required_number(data, "overlay_alpha", sample_context));
                    value.mean_confidence = required_number(data, "mean_confidence", sample_context);
                    const double area = required_number(data, "predicted_area", sample_context);
                    if (area < 0 || std::trunc(area) != area || area >= 9223372036854775808.0)
                        invalid_document(sample_context, "predicted_area must be a nonnegative int64");
                    value.predicted_area = static_cast<int64_t>(area);
                    value.mask_path = required_string(data, "mask_path", sample_context).toStdString();
                    value.visualization_path = required_string(data, "visualization_path", sample_context).toStdString();
                    const auto metrics = required_object(data, "metrics", sample_context);
                    require_exact_fields(metrics, {"available", "precision", "recall", "f1", "mean_iou"}, sample_context);
                    value.metrics = {required_bool(metrics, "available", sample_context),
                        required_number(metrics, "precision", sample_context), required_number(metrics, "recall", sample_context),
                        required_number(metrics, "f1", sample_context), required_number(metrics, "mean_iou", sample_context)};
                    for (const auto& label : required_array(data, "present_labels", sample_context)) {
                        if (!label.isDouble() || label.toDouble() < 1 || label.toDouble() > 255 ||
                            std::trunc(label.toDouble()) != label.toDouble())
                            invalid_document(sample_context, "present_labels must contain integer foreground labels");
                        value.present_labels.push_back(label.toInt());
                    }
                    for (const auto& entry : required_array(data, "components", sample_context)) {
                        if (!entry.isObject()) invalid_document(sample_context, "component must be an object");
                        const auto component_data = entry.toObject();
                        require_exact_fields(component_data, {"label", "area", "bbox", "aspect_ratio", "confidence",
                            "area_pass", "aspect_ratio_pass", "confidence_pass", "interest", "runs"}, sample_context);
                        jocelin::result::sg::SgResult::Component component;
                        component.label = required_integer(component_data, "label", sample_context);
                        component.area = required_integer(component_data, "area", sample_context);
                        const auto bbox = required_array(component_data, "bbox", sample_context);
                        if (bbox.size() != 4) invalid_document(sample_context, "bbox must contain four integers");
                        int coordinates[4];
                        for (int index = 0; index < 4; ++index)
                            coordinates[index] = required_integer(QJsonObject{{"coordinate", bbox[index]}}, "coordinate", sample_context);
                        component.bbox = {coordinates[0], coordinates[1], coordinates[2], coordinates[3]};
                        component.aspect_ratio = required_number(component_data, "aspect_ratio", sample_context);
                        component.confidence = required_number(component_data, "confidence", sample_context);
                        component.area_pass = required_bool(component_data, "area_pass", sample_context);
                        component.aspect_ratio_pass = required_bool(component_data, "aspect_ratio_pass", sample_context);
                        component.confidence_pass = required_bool(component_data, "confidence_pass", sample_context);
                        component.interest = required_bool(component_data, "interest", sample_context);
                        for (const auto& run_data : required_array(component_data, "runs", sample_context)) {
                            if (!run_data.isArray() || run_data.toArray().size() != 4)
                                invalid_document(sample_context, "run must contain row, start, end, label");
                            const auto run = run_data.toArray();
                            int fields[4];
                            for (int index = 0; index < 4; ++index)
                                fields[index] = required_integer(QJsonObject{{"coordinate", run[index]}}, "coordinate", sample_context);
                            if (fields[0] < 0 || fields[0] >= value.image_height || fields[1] < 0 ||
                                fields[2] <= fields[1] || fields[2] > value.image_width || fields[3] != component.label ||
                                component.label < 1 || component.label > 255)
                                invalid_document(sample_context, "component run is outside image or class bounds");
                            component.region.vRun.emplace_back(fields[0], fields[1], fields[2], fields[3]);
                        }
                        value.components.push_back(std::move(component));
                    }
                }
            }, result.result);
            if (result.source_sample_id.empty())
                invalid_document(sample_context, "source_sample_id must be present");
            set.samples.push_back(std::move(result));
        }
        runtime.results.push_back(std::move(set));
    }
    return runtime;
}

QJsonObject config_to_json(const Node& node, std::vector<std::pair<QString, QByteArray>>& region_assets,
    std::vector<std::pair<QString, QByteArray>>& inference_assets) {
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
        auto runtime = runtime_to_json(config.runtime);
        QJsonArray references;
        for (const auto& entry : runtime.value("results").toArray()) {
            const auto result = entry.toObject();
            const auto model_id = result.value("model_id").toString();
            const auto weight_id = result.value("weight_id").toString();
            const auto file = inference_result_directory(node.node_id, model_id.toStdString(), weight_id.toStdString()) + "/result.json";
            inference_assets.emplace_back(file, QJsonDocument(result).toJson(QJsonDocument::Indented));
            references.append(QJsonObject{{"model_id", model_id}, {"weight_id", weight_id}});
        }
        runtime["results"] = references;
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
            {"dataset_binding", binding_to_json(config.dataset_binding, config.task, node.node_id, region_assets)},
            {"runtime", runtime},
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

void config_from_json(Node& node, const QJsonObject& object, const QString& node_asset_root) {
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
        const auto* module = find_task_module(config.task);
        if (!module || !module->available) invalid_document(context, "unsupported Task type");
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
                    context), config.task, node.node_id, node_asset_root);
        auto runtime = required_object(object, "runtime", context);
        QJsonArray restored_results;
        for (const auto& entry : required_array(runtime, "results", context)) {
            if (!entry.isObject()) invalid_document(context, "invalid inference reference");
            const auto reference = entry.toObject();
            require_exact_fields(reference, {"model_id", "weight_id"}, "Inference reference");
            const auto model_id = required_string(reference, "model_id", context);
            const auto weight_id = required_string(reference, "weight_id", context);
            if (node_asset_root.isEmpty()) invalid_document(context, "inference requires its Node asset root");
            const auto file = inference_result_directory(node.node_id, model_id.toStdString(), weight_id.toStdString()) + "/result.json";
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(ProjectStorage::read(QDir(node_asset_root).filePath(file)), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject())
                invalid_document(context, "invalid inference result.json");
            const auto result = document.object();
            if (result.value("model_id") != reference.value("model_id") ||
                result.value("weight_id") != reference.value("weight_id"))
                invalid_document(context, "inference asset does not match its Node reference");
            restored_results.append(result);
        }
        runtime["results"] = restored_results;
        config.runtime = runtime_from_json(runtime, config.task);
        node.config = std::move(config);
    } else {
        throw std::runtime_error("unsupported node kind");
    }
}

} // namespace

QString inference_result_directory(int node_id, const std::string& model_id, const std::string& weight_id) {
    for (const auto& id : {model_id, weight_id}) {
        const auto value = QString::fromStdString(id);
        if (value.contains('/') || value.contains('\\') || value.contains(':') ||
            ProjectStorage::validated_relative_path(value, "inference identity") != value)
            throw std::runtime_error("inference model/weight id must be one directory component");
    }
    return QString::number(node_id) + "/inference/" + QString::fromStdString(model_id) + "/" + QString::fromStdString(weight_id);
}

SerializedWorkflow serialize_workflow(
    const WorkflowGraph& graph,
    const std::map<int, QPointF>& positions)
{
    SerializedWorkflow serialized;
    QJsonObject root;
    root["format"] = "deep_learning_workflow";
    root["next_node_id"] = graph.next_node_id();
    QJsonArray nodes;
    for (const auto& [node_id, node] : graph.nodes()) {
        const auto position = positions.find(node_id);
        if (position == positions.end())
            throw std::runtime_error("node position is missing for node " + std::to_string(node_id));
        nodes.append(QJsonObject{
            {"id", node_id},
            {"kind", static_cast<int>(node.kind)},
            {"x", position->second.x()},
            {"y", position->second.y()},
            {"config", config_to_json(node, serialized.region_assets, serialized.inference_assets)}});
    }
    QJsonArray connections;
    for (const auto& edge : graph.connections()) {
        connections.append(QJsonObject{
            {"from_node", edge.from_node_id},
            {"from_port", static_cast<int>(edge.from_port)},
            {"to_node", edge.to_node_id},
            {"to_port", static_cast<int>(edge.to_port)}});
    }
    root["nodes"] = nodes;
    root["connections"] = connections;
    serialized.json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    return serialized;
}

WorkflowDocument parse_workflow(const QByteArray& data, const QString& node_asset_root) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error("invalid workflow JSON: " + parseError.errorString().toStdString());
    const auto root = document.object();
    require_exact_fields(
        root, {"format", "next_node_id", "nodes", "connections"},
        "Workflow document");
    if (required_string(
            root, "format", "Workflow document") !=
        "deep_learning_workflow")
        throw std::runtime_error("unsupported workflow format");

    WorkflowDocument result;
    std::vector<Node> restored_nodes;
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
        const int node_id =
            required_integer(object, "id", context);
        const int kind_value =
            required_integer(object, "kind", context);
        if (node_id < 1 ||
            kind_value < static_cast<int>(NodeKind::DatasetInput) ||
            kind_value > static_cast<int>(NodeKind::TaskConfig))
            throw std::runtime_error("invalid node id or kind");
        const auto config_object =
            required_object(object, "config", context);
        const auto kind = static_cast<NodeKind>(kind_value);
        Node restored;
        restored.node_id = node_id;
        restored.kind = kind;
        config_from_json(restored, config_object, node_asset_root);
        restored_nodes.push_back(std::move(restored));
        result.positions[node_id] = QPointF(
            required_number(object, "x", context),
            required_number(object, "y", context));
    }
    result.graph.restore_nodes(std::move(restored_nodes),
        required_integer(root, "next_node_id", "Workflow document"));
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
        const int from_node_id =
            required_integer(
                object, "from_node", context);
        const int from_port =
            required_integer(
                object, "from_port", context);
        const int to_node_id =
            required_integer(
                object, "to_node", context);
        const int to_port =
            required_integer(
                object, "to_port", context);
        if (result.graph.nodes().count(from_node_id) == 0 || result.graph.nodes().count(to_node_id) == 0 || from_port < 0 || to_port < 0)
            throw std::runtime_error("connection references an invalid node or port");
        result.graph.connect(
            from_node_id, static_cast<size_t>(from_port),
            to_node_id, static_cast<size_t>(to_port));
    }
    return result;
}

} // namespace studio
