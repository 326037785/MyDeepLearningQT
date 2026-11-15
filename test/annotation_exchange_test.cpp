#include "annotation_exchange.h"
#include "segmentation_annotation_state.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc == 2) {
        try {
            const auto imported = studio::AnnotationExchange::import_auto(
                QString::fromLocal8Bit(argv[1]));
            std::cout
                << "format=" << imported.format_id.toStdString()
                << " images=" << imported.dataset.items.size()
                << " samples=" << imported.binding.samples.size()
                << " classes=" << imported.binding.classes.size()
                << " masks=" << std::count_if(
                       imported.binding.samples.begin(),
                       imported.binding.samples.end(),
                       [](const studio::TaskSampleBinding& sample) {
                           return !sample.regions.empty();
                       })
                << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 100;
        }
    }
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;

    QImage image(32, 24, QImage::Format_RGB888);
    image.fill(Qt::red);
    QImage mask(32, 24, QImage::Format_Grayscale8);
    mask.fill(1);
    const QString image_path =
        temporary.filePath("source/input.png");
    const QString mask_path =
        temporary.filePath("source/input-mask.png");
    const QString background_image_path =
        temporary.filePath("source/background.png");
    QDir().mkpath(QFileInfo(image_path).absolutePath());
    if (!image.save(image_path) ||
        !image.save(background_image_path) ||
        !mask.save(mask_path))
        return 2;

    studio::TaskDatasetBinding binding;
    binding.split_settings = {
        75, 25, 0, 1234, "stratified"};
    binding.classes.push_back({
        "vehicle", 1, "vehicle", "#1F8A9E",
        true, false});

    studio::TaskSampleBinding annotated;
    annotated.sample_id = "sample-input";
    annotated.image_paths = {image_path.toStdString()};
    studio::import_segmentation_annotation(binding.classes, annotated, mask_path);
    annotated.split = "validation";
    annotated.annotation_state = "annotated";
    binding.samples.push_back(std::move(annotated));

    studio::TaskSampleBinding background;
    background.sample_id = "sample-background";
    background.image_paths = {background_image_path.toStdString()};
    background.split = "train";
    background.annotation_state = "annotated";
    binding.samples.push_back(std::move(background));

    const QString native_dir =
        temporary.filePath("native");
    const QString native_manifest =
        studio::AnnotationExchange::export_task(
            binding,
            native_dir,
            studio::AnnotationExchangeFormat::StudioNative);
    const auto native =
        studio::AnnotationExchange::import_auto(
            native_manifest);
    if (native.format_id != "studio-native" ||
        native.dataset.items.size() != 2 ||
        native.dataset.items.front().sample_id !=
            "sample-input" ||
        native.binding.classes.size() != 1 ||
        native.binding.classes.front().id != "vehicle" ||
        native.binding.samples.size() != 2 ||
        native.binding.split_settings.train_percent != 75 ||
        native.binding.split_settings.seed != 1234 ||
        native.binding.samples.front().split !=
            "validation" ||
        native.binding.samples.front().regions.empty() ||
        native.binding.samples[1].annotation_state !=
            "annotated" ||
        !native.binding.samples[1].regions.empty())
        return 3;

    QFile native_file(native_manifest);
    if (!native_file.open(QIODevice::ReadOnly))
        return 4;
    auto invalid_native =
        QJsonDocument::fromJson(
            native_file.readAll()).object();
    auto classes =
        invalid_native.value("classes").toArray();
    auto annotation_class =
        classes.at(0).toObject();
    annotation_class.remove("locked");
    classes[0] = annotation_class;
    invalid_native["classes"] = classes;
    const QString invalid_native_path =
        temporary.filePath(
            "native/invalid.dlmeta.json");
    QFile invalid_native_file(invalid_native_path);
    if (!invalid_native_file.open(QIODevice::WriteOnly) ||
        invalid_native_file.write(
            QJsonDocument(invalid_native).toJson()) < 0)
        return 5;
    invalid_native_file.close();
    try {
        (void)studio::AnnotationExchange::import_auto(
            invalid_native_path);
        return 6;
    } catch (const std::runtime_error&) {
    }

    const QString ilabelled_dir =
        temporary.filePath("ilabelled-export");
    studio::AnnotationExchange::export_task(
        binding,
        ilabelled_dir,
        studio::AnnotationExchangeFormat::
            ILabelledSegmentation);
    const auto ilabelled =
        studio::AnnotationExchange::import_auto(
            ilabelled_dir);
    if (ilabelled.format_id != "ilabelled-segmentation" ||
        ilabelled.dataset.items.size() != 2 ||
        ilabelled.binding.classes.size() != 1 ||
        ilabelled.binding.classes.front().mask_value != 1 ||
        ilabelled.binding.samples.size() != 2 ||
        std::count_if(ilabelled.binding.samples.begin(), ilabelled.binding.samples.end(),
            [](const auto& sample) { return !sample.regions.empty() && sample.annotation_state == "annotated"; }) != 1 ||
        std::count_if(ilabelled.binding.samples.begin(), ilabelled.binding.samples.end(),
            [](const auto& sample) { return sample.regions.empty() && sample.annotation_state == "annotated"; }) != 1)
        return 7;

    const QString nested = temporary.filePath(
        "iLabelled-project/.ilabelled/export/"
        "project-segmentation");
    studio::AnnotationExchange::export_task(
        binding,
        nested,
        studio::AnnotationExchangeFormat::
            ILabelledSegmentation);
    const auto detected =
        studio::AnnotationExchange::import_auto(
            temporary.filePath("iLabelled-project"));
    if (detected.binding.samples.size() != 2) return 8;

    // Generic dense-mask data is imported only through the explicit
    // annotation pipeline. Without class metadata, values 1..maximum become
    // default foreground classes and zero remains implicit background.
    const QString dense_root = temporary.filePath("dense-mask");
    if (!QDir().mkpath(QDir(dense_root).filePath("images")) ||
        !QDir().mkpath(QDir(dense_root).filePath("masks")))
        return 9;
    QImage dense_image(12, 10, QImage::Format_RGB888);
    dense_image.fill(Qt::blue);
    QImage dense_mask(12, 10, QImage::Format_Grayscale8);
    dense_mask.fill(0);
    dense_mask.scanLine(4)[5] = 4;
    const QString dense_image_path =
        QDir(dense_root).filePath("images/foreground.png");
    const QString dense_mask_path =
        QDir(dense_root).filePath("masks/foreground.png");
    const QString unannotated_image_path =
        QDir(dense_root).filePath("images/unannotated.png");
    if (!dense_image.save(dense_image_path) ||
        !dense_image.save(unannotated_image_path) ||
        !dense_mask.save(dense_mask_path))
        return 10;

    const auto dense = studio::AnnotationExchange::import_auto(dense_root);
    if (dense.format_id != "dense-mask-directory" ||
        dense.dataset.items.size() != 2 ||
        dense.binding.samples.size() != 2 ||
        dense.binding.classes.size() != 4)
        return 11;
    for (int index = 0; index < 4; ++index) {
        const auto& category = dense.binding.classes[static_cast<size_t>(index)];
        if (category.mask_value != index + 1 ||
            category.id != "class-" + std::to_string(index + 1) ||
            category.name != "Class " + std::to_string(index + 1))
            return 12;
    }
    const auto background_only = std::find_if(
        dense.binding.samples.begin(), dense.binding.samples.end(),
        [](const studio::TaskSampleBinding& sample) {
            return sample.regions.empty();
        });
    if (background_only == dense.binding.samples.end() ||
        background_only->annotation_state != "unannotated")
        return 13;
    const auto dense_stats =
        studio::AnnotationExchange::import_stats(dense);
    if (dense_stats.image_count != 2 ||
        dense_stats.mask_count != 1 ||
        dense_stats.unannotated_count != 1 ||
        dense_stats.class_count != 4 ||
        dense_stats.review_count != 0)
        return 14;
    return 0;
}
