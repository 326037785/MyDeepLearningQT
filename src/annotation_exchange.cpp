#include "annotation_exchange.h"
#include "segmentation_annotation_state.h"
#include "annotation_codec.h"

#include "task_dataset_binding.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace studio {
namespace {

constexpr auto NativeManifest = "dataset.dlmeta.json";

[[noreturn]] void invalid_annotation(
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
            invalid_annotation(
                context, "missing field '" + name + "'");
    }
    for (auto iterator = object.begin();
         iterator != object.end(); ++iterator) {
        if (expected.count(iterator.key()) == 0)
            invalid_annotation(
                context,
                "unexpected field '" + iterator.key() + "'");
    }
}

QString required_string(
    const QJsonObject& object,
    const char* field,
    const QString& context)
{
    const auto value = object.value(QString::fromLatin1(field));
    if (!value.isString())
        invalid_annotation(
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
        invalid_annotation(
            context,
            "field '" + QString::fromLatin1(field) +
                "' must be a boolean");
    return value.toBool();
}

int required_integer(
    const QJsonObject& object,
    const char* field,
    const QString& context)
{
    const auto value = object.value(QString::fromLatin1(field));
    const double number = value.toDouble();
    if (!value.isDouble() || !std::isfinite(number) ||
        std::trunc(number) != number ||
        number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max())
        invalid_annotation(
            context,
            "field '" + QString::fromLatin1(field) +
                "' must be an integer");
    return static_cast<int>(number);
}

QJsonArray required_array(
    const QJsonObject& object,
    const char* field,
    const QString& context)
{
    const auto value = object.value(QString::fromLatin1(field));
    if (!value.isArray())
        invalid_annotation(
            context,
            "field '" + QString::fromLatin1(field) +
                "' must be an array");
    return value.toArray();
}

QJsonObject required_object(
    const QJsonObject& object,
    const char* field,
    const QString& context)
{
    const auto value = object.value(QString::fromLatin1(field));
    if (!value.isObject())
        invalid_annotation(
            context,
            "field '" + QString::fromLatin1(field) +
                "' must be an object");
    return value.toObject();
}

QJsonObject read_object(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(
            ("cannot open annotation metadata: " + path).toStdString());
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error(
            ("invalid annotation JSON: " + error.errorString()).toStdString());
    return document.object();
}

void write_object(const QString& path, const QJsonObject& object) {
    QSaveFile file(path);
    const QByteArray bytes =
        QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error(
            ("cannot write annotation metadata: " + path).toStdString());
}

QString locate_manifest(const QString& source) {
    const QFileInfo info(source);
    if (info.isFile()) return info.absoluteFilePath();
    if (!info.isDir())
        throw std::runtime_error("annotation import source does not exist");
    const QDir directory(info.absoluteFilePath());
    const QString native = directory.filePath(NativeManifest);
    if (QFileInfo(native).isFile()) return native;
    const QString ilabelled = directory.filePath("label_map.json");
    if (QFileInfo(ilabelled).isFile()) return ilabelled;

    QStringList candidates;
    QDirIterator iterator(
        directory.absolutePath(), {NativeManifest, "label_map.json"},
        QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) candidates.push_back(iterator.next());
    if (candidates.size() == 1) return candidates.front();
    if (candidates.empty())
        throw std::runtime_error(
            "no supported annotation manifest was detected");
    throw std::runtime_error(
        "multiple annotation datasets were detected; select one export folder directly");
}

QString unique_destination(
    const QDir& directory,
    const QString& file_name,
    std::set<QString>& used)
{
    const QFileInfo input(file_name);
    const QString stem = input.completeBaseName();
    const QString suffix =
        input.suffix().isEmpty() ? QString{} : "." + input.suffix();
    QString candidate = input.fileName();
    int index = 2;
    while (used.count(candidate.toLower()) != 0 ||
           QFileInfo(directory.filePath(candidate)).exists())
        candidate = stem + "-" + QString::number(index++) + suffix;
    used.insert(candidate.toLower());
    return candidate;
}

void copy_file(const QString& source, const QString& destination) {
    if (!QFileInfo(source).isFile())
        throw std::runtime_error(
            ("annotation resource does not exist: " + source).toStdString());
    if (QFileInfo(destination).exists() && !QFile::remove(destination))
        throw std::runtime_error(
            ("cannot replace annotation resource: " + destination).toStdString());
    if (!QFile::copy(source, destination))
        throw std::runtime_error(
            ("cannot copy annotation resource to: " + destination).toStdString());
}

QJsonArray strings_to_json(const std::vector<std::string>& values) {
    QJsonArray array;
    for (const auto& value : values)
        array.append(QString::fromStdString(value));
    return array;
}

std::vector<std::string> strings_from_json(
    const QJsonValue& value,
    const QDir& base)
{
    if (!value.isArray())
        throw std::runtime_error("annotation image_paths must be an array");
    std::vector<std::string> values;
    for (const auto& entry : value.toArray()) {
        if (!entry.isString() || entry.toString().isEmpty())
            throw std::runtime_error(
                "annotation image_paths contains an invalid path");
        values.push_back(
            QFileInfo(base.absoluteFilePath(entry.toString()))
                .absoluteFilePath().toStdString());
    }
    return values;
}

AnnotationClass class_from_json(const QJsonObject& value) {
    const QString context = "Annotation class";
    require_exact_fields(
        value,
        {"id", "mask_value", "name", "color",
         "visible", "locked"},
        context);
    AnnotationClass result{
        required_string(
            value, "id", context).toStdString(),
        required_integer(
            value, "mask_value", context),
        required_string(
            value, "name", context).toStdString(),
        required_string(
            value, "color", context).toStdString(),
        required_bool(
            value, "visible", context),
        required_bool(
            value, "locked", context)};
    if (result.id.empty() || result.name.empty() ||
        result.color.empty() ||
        result.mask_value < 1 || result.mask_value > 255)
        throw std::runtime_error("annotation class is incomplete");
    return result;
}

AnnotationImportResult import_native(const QString& manifest) {
    const QJsonObject root = read_object(manifest);
    const QString context = "Annotation bundle";
    const bool has_split_settings =
        root.contains("split_settings");
    if (has_split_settings)
        require_exact_fields(
            root,
            {"format", "task", "preprocessing",
             "split_settings", "classes", "samples"},
            context);
    else
        require_exact_fields(
            root,
            {"format", "task", "preprocessing",
             "classes", "samples"},
            context);
    if (required_string(root, "format", context) !=
            "deep_learning_annotation_bundle" ||
        required_string(root, "task", context) != "sg")
        throw std::runtime_error(
            "unsupported DeepLearning annotation bundle");

    AnnotationImportResult result;
    result.format_id = QStringLiteral("studio-native");
    result.source = manifest;
    const QDir base(QFileInfo(manifest).absolutePath());
    result.dataset.directory = base.absolutePath().toStdString();

    std::set<std::string> class_ids;
    std::set<int> mask_values;
    for (const auto& entry :
         required_array(root, "classes", context)) {
        if (!entry.isObject())
            throw std::runtime_error("annotation class must be an object");
        auto value = class_from_json(entry.toObject());
        if (!class_ids.insert(value.id).second ||
            !mask_values.insert(value.mask_value).second)
            throw std::runtime_error(
                "annotation class ids and mask values must be unique");
        result.binding.classes.push_back(std::move(value));
    }

    const auto preprocessing =
        required_object(root, "preprocessing", context);
    require_exact_fields(
        preprocessing, {"normalize", "bgr_to_rgb"},
        "Annotation preprocessing");
    result.binding.preprocessing.normalize =
        required_bool(
            preprocessing, "normalize",
            "Annotation preprocessing");
    result.binding.preprocessing.bgr_to_rgb =
        required_bool(
            preprocessing, "bgr_to_rgb",
            "Annotation preprocessing");
    if (has_split_settings) {
        const auto split = required_object(
            root, "split_settings", context);
        require_exact_fields(
            split,
            {"strategy", "train_percent", "validation_percent",
             "test_percent", "seed"},
            "Annotation split settings");
        result.binding.split_settings.strategy =
            required_string(
                split, "strategy",
                "Annotation split settings").toStdString();
        result.binding.split_settings.train_percent =
            required_integer(
                split, "train_percent",
                "Annotation split settings");
        result.binding.split_settings.validation_percent =
            required_integer(
                split, "validation_percent",
                "Annotation split settings");
        result.binding.split_settings.test_percent =
            required_integer(
                split, "test_percent",
                "Annotation split settings");
        const int seed = required_integer(
            split, "seed", "Annotation split settings");
        if (result.binding.split_settings.strategy != "stratified" ||
            result.binding.split_settings.train_percent < 0 ||
            result.binding.split_settings.validation_percent < 0 ||
            result.binding.split_settings.test_percent < 0 ||
            result.binding.split_settings.train_percent +
                    result.binding.split_settings.validation_percent +
                    result.binding.split_settings.test_percent != 100 ||
            seed < 0)
            throw std::runtime_error(
                "annotation split settings are invalid");
        result.binding.split_settings.seed =
            static_cast<std::uint32_t>(seed);
    }

    int bucket_capacity = 0;
    std::set<std::string> sample_ids;
    for (const auto& entry :
         required_array(root, "samples", context)) {
        if (!entry.isObject())
            throw std::runtime_error("annotation sample must be an object");
        const auto value = entry.toObject();
        const bool has_split_manual =
            value.contains("split_manual");
        if (has_split_manual)
            require_exact_fields(
                value,
                {"sample_id", "image_paths", "selected",
                 "mask_path", "class_id", "split",
                 "split_manual", "annotation_state",
                 "quality_issues"},
                "Annotation sample");
        else
            require_exact_fields(
                value,
                {"sample_id", "image_paths", "selected",
                 "mask_path", "class_id", "split",
                 "annotation_state", "quality_issues"},
                "Annotation sample");
        TaskSampleBinding sample;
        sample.sample_id =
            required_string(
                value, "sample_id",
                "Annotation sample").toStdString();
        sample.image_paths =
            strings_from_json(value.value("image_paths"), base);
        sample.selected =
            required_bool(
                value, "selected", "Annotation sample");
        const QString mask =
            required_string(
                value, "mask_path", "Annotation sample");
        sample.class_id =
            required_string(
                value, "class_id",
                "Annotation sample").toStdString();
        sample.split =
            normalized_split(
                required_string(
                    value, "split",
                    "Annotation sample").toStdString());
        sample.split_manual = has_split_manual
            ? required_bool(
                  value, "split_manual",
                  "Annotation sample")
            : sample.split != "unassigned";
        sample.annotation_state =
            required_string(
                value, "annotation_state",
                "Annotation sample").toStdString();
        if (sample.annotation_state != "unannotated" &&
            sample.annotation_state != "annotated")
            throw std::runtime_error(
                "annotation sample state is invalid");
        for (const auto& issue :
             required_array(
                 value, "quality_issues",
                 "Annotation sample")) {
            if (!issue.isString() ||
                issue.toString().isEmpty())
                throw std::runtime_error(
                    "annotation quality issue must be a non-empty string");
            sample.quality_issues.push_back(
                issue.toString().toStdString());
        }

        if (sample.sample_id.empty() || sample.image_paths.empty() ||
            !sample_ids.insert(sample.sample_id).second)
            throw std::runtime_error(
                "annotation sample identity is invalid or duplicated");
        if (!sample.class_id.empty() &&
            class_ids.count(sample.class_id) == 0)
            throw std::runtime_error(
                "annotation sample class_id does not reference a class");
        bucket_capacity = std::max(
            bucket_capacity,
            static_cast<int>(sample.image_paths.size()));
        for (const auto& image_path : sample.image_paths) {
            DatasetConfig::Item item;
            item.path = image_path;
            item.sample_id = sample.sample_id;
            item.valid =
                QFileInfo(QString::fromStdString(image_path)).isFile();
            if (!item.valid)
                item.error =
                    "image referenced by annotation bundle does not exist";
            result.dataset.items.push_back(std::move(item));
        }
        if (!mask.isEmpty()) {
            const auto requested_state = sample.annotation_state;
            const QString resource = base.absoluteFilePath(mask);
            import_segmentation_annotation(result.binding.classes, sample, resource);
            if (requested_state == "unannotated") sample.annotation_state = "unannotated";
            if (!QFileInfo(resource).isFile())
                result.warnings.push_back("Missing mask for sample " + QString::fromStdString(sample.sample_id));
        }
        result.binding.samples.push_back(std::move(sample));
    }
    if (result.binding.samples.empty())
        throw std::runtime_error(
            "annotation bundle contains no samples");
    result.dataset.bucket_capacity = std::max(1, bucket_capacity);
    return result;
}

AnnotationImportResult import_ilabelled(const QString& label_map_path) {
    const QJsonObject labels = read_object(label_map_path);
    AnnotationImportResult result;
    result.format_id = QStringLiteral("ilabelled-segmentation");
    result.source = label_map_path;
    const QDir root(QFileInfo(label_map_path).absolutePath());
    const QDir images(root.filePath("images"));
    const QDir masks(root.filePath("masks"));
    if (!images.exists() || !masks.exists())
        throw std::runtime_error(
            "iLabelled segmentation export must contain images/ and masks/");

    std::map<int, QString> ordered_labels;
    for (auto iterator = labels.begin(); iterator != labels.end(); ++iterator) {
        bool ok = false;
        const int value = iterator.key().toInt(&ok);
        if (!ok || value < 0 || value > 255 ||
            !iterator.value().isString() ||
            iterator.value().toString().trimmed().isEmpty())
            throw std::runtime_error(
                "iLabelled label_map contains an invalid label");
        if (value > 0)
            ordered_labels[value] =
                iterator.value().toString().trimmed();
    }
    if (ordered_labels.empty())
        throw std::runtime_error(
            "iLabelled label_map contains no foreground classes");
    for (const auto& [mask_value, name] : ordered_labels) {
        result.binding.classes.push_back({
            "class-" + std::to_string(mask_value),
            mask_value,
            name.toStdString(),
            default_class_color(mask_value),
            true,
            false});
    }

    result.dataset.directory = root.absolutePath().toStdString();
    for (const auto& image : images.entryInfoList(QDir::Files, QDir::Name)) {
        const std::vector<std::string> paths{
            image.absoluteFilePath().toStdString()};
        const std::string sample_id = stable_sample_id(paths);
        result.dataset.items.push_back({
            paths.front(), {}, true, sample_id});

        TaskSampleBinding sample;
        sample.sample_id = sample_id;
        sample.image_paths = paths;
        sample.split = "unassigned";
        const QString mask =
            masks.filePath(image.completeBaseName() + ".png");
        import_segmentation_annotation(result.binding.classes, sample,
            QFileInfo(mask).isFile() ? mask : QString{});
        result.binding.samples.push_back(std::move(sample));
    }
    if (result.dataset.items.empty())
        throw std::runtime_error(
            "iLabelled export contains no images");
    return result;
}

QString export_bundle(
    const TaskDatasetBinding& binding,
    const QString& destination,
    bool ilabelled)
{
    if (binding.classes.empty())
        throw std::runtime_error(
            "add classes before exporting annotations");
    QDir output(QFileInfo(destination).absoluteFilePath());
    if (!QDir().mkpath(output.filePath("images")) ||
        !QDir().mkpath(output.filePath("masks")))
        throw std::runtime_error(
            "cannot create annotation export directories");
    QDir images(output.filePath("images"));
    QDir masks(output.filePath("masks"));

    QJsonArray samples;
    std::set<QString> used_images;
    std::set<QString> used_masks;
    bool exported_any = false;
    for (const auto& sample : binding.samples) {
        if (sample.image_paths.empty()) continue;
        if (ilabelled && sample.image_paths.size() != 1)
            throw std::runtime_error(
                "iLabelled export supports one image per sample");

        std::vector<std::string> exported_images;
        for (const auto& source : sample.image_paths) {
            const QString source_path = QString::fromStdString(source);
            const QString image_name = unique_destination(
                images, QFileInfo(source_path).fileName(), used_images);
            copy_file(source_path, images.filePath(image_name));
            exported_images.push_back(
                ("images/" + image_name).toStdString());
        }
        const QString preferred_mask =
            ilabelled
                ? QFileInfo(QString::fromStdString(
                    exported_images.front())).completeBaseName() + ".png"
                : QString::fromStdString(sample.sample_id) + ".png";
        const QString mask_name =
            unique_destination(masks, preferred_mask, used_masks);
        const bool has_mask = !sample.regions.empty();
        if (has_mask || (ilabelled && sample.annotation_state == "annotated")) {
            const auto mask = segmentation_annotation_image(sample);
            if (mask.isNull() || !mask.save(masks.filePath(mask_name), "PNG"))
                throw std::runtime_error("cannot export segmentation target");
        }
        exported_any = true;

        if (ilabelled) continue;
        samples.append(QJsonObject{
            {"sample_id", QString::fromStdString(sample.sample_id)},
            {"image_paths", strings_to_json(exported_images)},
            {"selected", sample.selected},
            {"mask_path", has_mask ? "masks/" + mask_name : QString{}},
            {"class_id", QString::fromStdString(sample.class_id)},
            {"split", QString::fromStdString(sample.split)},
            {"split_manual", sample.split_manual},
            {"annotation_state",
                QString::fromStdString(sample.annotation_state)},
            {"quality_issues",
                strings_to_json(sample.quality_issues)}});
    }
    if (!exported_any)
        throw std::runtime_error(
            "no valid image/mask samples are available for export");
    if (ilabelled) {
        QJsonObject labels{{"0", "background"}};
        for (const auto& value : binding.classes)
            labels[QString::number(value.mask_value)] =
                QString::fromStdString(value.name);
        write_object(output.filePath("label_map.json"), labels);
        return output.filePath("label_map.json");
    }
    QJsonArray classes;
    for (const auto& value : binding.classes) {
        classes.append(QJsonObject{
            {"id", QString::fromStdString(value.id)},
            {"mask_value", value.mask_value},
            {"name", QString::fromStdString(value.name)},
            {"color", QString::fromStdString(value.color)},
            {"visible", value.visible},
            {"locked", value.locked}});
    }
    write_object(output.filePath(NativeManifest), QJsonObject{
        {"format", "deep_learning_annotation_bundle"},
        {"task", "sg"},
        {"preprocessing", QJsonObject{
            {"normalize", binding.preprocessing.normalize},
            {"bgr_to_rgb", binding.preprocessing.bgr_to_rgb}}},
        {"split_settings", QJsonObject{
            {"strategy", QString::fromStdString(
                binding.split_settings.strategy)},
            {"train_percent",
                binding.split_settings.train_percent},
            {"validation_percent",
                binding.split_settings.validation_percent},
            {"test_percent",
                binding.split_settings.test_percent},
            {"seed", static_cast<double>(
                binding.split_settings.seed)}}},
        {"classes", classes},
        {"samples", samples}});
    return output.filePath(NativeManifest);
}

} // namespace

AnnotationImportStats AnnotationExchange::import_stats(
    const AnnotationImportResult& imported)
{
    AnnotationImportStats result;
    for (const auto& sample : imported.binding.samples) {
        result.image_count += static_cast<int>(sample.image_paths.size());
        if (!sample.regions.empty()) ++result.mask_count;
        if (sample.annotation_state == "unannotated")
            ++result.unannotated_count;
        if (!sample.quality_issues.empty()) ++result.review_count;
    }
    if (result.image_count == 0)
        result.image_count = static_cast<int>(imported.dataset.items.size());
    result.class_count = static_cast<int>(imported.binding.classes.size());
    result.review_count += imported.warnings.size();
    return result;
}

AnnotationImportResult AnnotationExchange::import_auto(
    const QString& source)
{
    QString manifest;
    try {
        manifest = locate_manifest(source);
    } catch (const std::runtime_error&) {
        auto imported = AnnotationBindingMapper::map(
            make_default_annotation_codec_registry().read_auto(source));
        AnnotationImportResult result;
        result.dataset = std::move(imported.dataset);
        result.binding = std::move(imported.binding);
        result.format_id = std::move(imported.format_id);
        result.source = std::move(imported.source);
        result.warnings = std::move(imported.warnings);
        return result;
    }
    const QJsonObject object = read_object(manifest);
    if (object.value("format").toString() ==
        "deep_learning_annotation_bundle")
        return import_native(manifest);
    if (QFileInfo(manifest).fileName().compare(
            "label_map.json", Qt::CaseInsensitive) == 0)
        return import_ilabelled(manifest);
    throw std::runtime_error(
        "annotation format is not supported");
}

QString AnnotationExchange::export_task(
    const TaskDatasetBinding& binding,
    const QString& destination,
    AnnotationExchangeFormat format)
{
    return export_bundle(
        binding, destination,
        format ==
            AnnotationExchangeFormat::ILabelledSegmentation);
}

QString AnnotationExchange::format_name(
    AnnotationExchangeFormat format)
{
    return format == AnnotationExchangeFormat::StudioNative
        ? QStringLiteral("DeepLearning Studio bundle")
        : QStringLiteral("iLabelled segmentation export");
}

} // namespace studio
