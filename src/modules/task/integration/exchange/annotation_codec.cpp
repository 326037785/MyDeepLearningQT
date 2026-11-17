#include "modules/task/integration/exchange/annotation_codec.h"
#include "modules/task/application/annotation/segmentation_annotation_state.h"

#include "modules/task/application/task_dataset_binding.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>

#include <algorithm>
#include <set>
#include <stdexcept>

namespace studio {
namespace {

QString canonical_path(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::toNativeSeparators(
        canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QString dense_mask_root(const QString& source) {
    QFileInfo info(source);
    if (!info.isDir()) return {};
    QDir directory(info.absoluteFilePath());
    if (directory.dirName().compare(
            QStringLiteral("images"), Qt::CaseInsensitive) == 0) {
        QDir parent(directory);
        if (parent.cdUp() && parent.exists(QStringLiteral("masks")))
            directory = parent;
    }
    if (!directory.exists(QStringLiteral("images")) ||
        !directory.exists(QStringLiteral("masks")))
        return {};
    return canonical_path(directory.absolutePath());
}

bool readable_image(const QString& path) {
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    return QFileInfo(path).isFile() && reader.canRead();
}

QString matching_mask(
    const QDir& images,
    const QDir& masks,
    const QString& image_path)
{
    const QString relative = images.relativeFilePath(image_path);
    const QFileInfo relative_info(relative);
    const QString exact = masks.filePath(relative);
    if (QFileInfo(exact).isFile()) return canonical_path(exact);

    const QString relative_directory = relative_info.path() == "."
        ? QString{}
        : relative_info.path() + QDir::separator();
    const QString png = masks.filePath(
        relative_directory + relative_info.completeBaseName() + ".png");
    return QFileInfo(png).isFile() ? canonical_path(png) : QString{};
}

std::vector<AnnotationCategoryRecord> read_classes_txt(
    const QString& root)
{
    QFile file(QDir(root).filePath(QStringLiteral("classes.txt")));
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        throw std::runtime_error("cannot read classes.txt");

    const QString text = QString::fromUtf8(file.readAll());
    const QStringList tokens = text.split(
        QRegularExpression(QStringLiteral("[,;\\r\\n]+")),
        Qt::SkipEmptyParts);
    std::vector<AnnotationCategoryRecord> result;
    int next_value = 1;
    bool first = true;
    for (QString token : tokens) {
        token = token.trimmed();
        if (token.isEmpty()) continue;
        if (first && token.compare(
                QStringLiteral("background"), Qt::CaseInsensitive) == 0) {
            first = false;
            continue;
        }
        first = false;
        result.push_back({
            QStringLiteral("class-%1").arg(next_value),
            next_value,
            token});
        ++next_value;
    }
    return result;
}

int maximum_mask_value(const QString& path) {
    const QImage source(path);
    if (source.isNull())
        throw std::runtime_error(
            ("cannot read segmentation mask: " + path).toStdString());
    const QImage labels = source.convertToFormat(QImage::Format_Grayscale8);
    int maximum = 0;
    for (int y = 0; y < labels.height(); ++y) {
        const uchar* row = labels.constScanLine(y);
        for (int x = 0; x < labels.width(); ++x)
            maximum = std::max(maximum, static_cast<int>(row[x]));
    }
    return maximum;
}

class DenseMaskCodec final : public IAnnotationCodec {
public:
    QString format_id() const override {
        return QStringLiteral("dense-mask-directory");
    }

    int probe(const QString& source) const override {
        const QString root = dense_mask_root(source);
        if (root.isEmpty()) return 0;
        return QFileInfo(QDir(root).filePath(QStringLiteral("classes.txt"))).isFile()
            ? 70
            : 60;
    }

    AnnotationDocument read(const QString& source) const override {
        const QString root = dense_mask_root(source);
        if (root.isEmpty())
            throw std::runtime_error(
                "dense mask import requires sibling images/ and masks/ directories");

        AnnotationDocument document;
        document.format_id = format_id();
        document.source = canonical_path(source);
        document.source_root = root;
        document.bucket_capacity = 1;
        document.categories = read_classes_txt(root);

        const QDir images(QDir(root).filePath(QStringLiteral("images")));
        const QDir masks(QDir(root).filePath(QStringLiteral("masks")));
        QDirIterator iterator(
            images.absolutePath(), QDir::Files | QDir::Readable,
            QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString image_path = iterator.next();
            if (!readable_image(image_path)) continue;
            const QString relative = images.relativeFilePath(image_path);
            document.samples.push_back({
                QDir::fromNativeSeparators(relative),
                {canonical_path(image_path)},
                matching_mask(images, masks, image_path),
                QStringLiteral("unassigned")});
        }
        std::sort(
            document.samples.begin(), document.samples.end(),
            [](const AnnotationSampleRecord& left,
               const AnnotationSampleRecord& right) {
                return left.external_id.compare(
                    right.external_id, Qt::CaseInsensitive) < 0;
            });
        if (document.samples.empty())
            throw std::runtime_error(
                "dense mask annotation directory contains no readable images");
        return document;
    }
};

} // namespace

void AnnotationCodecRegistry::add(
    std::unique_ptr<IAnnotationCodec> codec)
{
    if (!codec) throw std::runtime_error("annotation codec is null");
    const QString id = codec->format_id();
    if (id.trimmed().isEmpty())
        throw std::runtime_error("annotation codec id is empty");
    if (std::any_of(
            codecs_.begin(), codecs_.end(),
            [&id](const auto& existing) {
                return existing->format_id() == id;
            }))
        throw std::runtime_error("annotation codec id is already registered");
    codecs_.push_back(std::move(codec));
}

AnnotationDocument AnnotationCodecRegistry::read_auto(
    const QString& source) const
{
    const IAnnotationCodec* selected = nullptr;
    int best_score = 0;
    for (const auto& codec : codecs_) {
        const int score = codec->probe(source);
        if (score > best_score) {
            selected = codec.get();
            best_score = score;
        }
    }
    if (!selected)
        throw std::runtime_error("annotation format is not supported");
    return selected->read(source);
}

std::vector<AnnotationClass> AnnotationClassResolver::resolve(
    const AnnotationDocument& document)
{
    std::vector<AnnotationCategoryRecord> categories = document.categories;
    if (categories.empty()) {
        int maximum = 0;
        for (const auto& sample : document.samples) {
            if (sample.mask_path.isEmpty()) continue;
            maximum = std::max(
                maximum, maximum_mask_value(sample.mask_path));
        }
        categories.reserve(static_cast<size_t>(maximum));
        for (int value = 1; value <= maximum; ++value) {
            categories.push_back({
                QStringLiteral("class-%1").arg(value),
                value,
                QStringLiteral("Class %1").arg(value)});
        }
    }

    std::set<int> mask_values;
    std::set<std::string> class_ids;
    std::vector<AnnotationClass> result;
    result.reserve(categories.size());
    for (const auto& category : categories) {
        if (category.mask_value < 1 || category.mask_value > 255)
            throw std::runtime_error(
                "segmentation class values must be between 1 and 255; zero is reserved for background");
        std::string id = category.external_id.trimmed().toStdString();
        if (id.empty()) id = "class-" + std::to_string(category.mask_value);
        if (!mask_values.insert(category.mask_value).second ||
            !class_ids.insert(id).second)
            throw std::runtime_error(
                "annotation class ids and mask values must be unique");
        const QString name = category.name.trimmed().isEmpty()
            ? QStringLiteral("Class %1").arg(category.mask_value)
            : category.name.trimmed();
        result.push_back({
            id,
            category.mask_value,
            name.toStdString(),
            default_class_color(category.mask_value),
            true,
            false});
    }
    return result;
}

AnnotationImportPackage AnnotationBindingMapper::map(
    AnnotationDocument document)
{
    AnnotationImportPackage result;
    result.format_id = document.format_id;
    result.source = document.source;
    result.warnings = document.warnings;
    result.dataset.directory = document.source_root.toStdString();
    result.dataset.bucket_capacity = std::max(1, document.bucket_capacity);
    result.binding.classes = AnnotationClassResolver::resolve(document);

    for (const auto& imported : document.samples) {
        std::vector<std::string> image_paths;
        image_paths.reserve(static_cast<size_t>(imported.image_paths.size()));
        for (const QString& image_path : imported.image_paths)
            image_paths.push_back(canonical_path(image_path).toStdString());
        if (image_paths.empty()) continue;

        const std::string sample_id = stable_sample_id(image_paths);
        for (const auto& image_path : image_paths) {
            result.dataset.items.push_back({
                image_path, {}, true, sample_id});
        }

        TaskSampleBinding sample;
        sample.sample_id = sample_id;
        sample.image_paths = std::move(image_paths);
        sample.selected = true;
        sample.split = normalized_split(imported.split.toStdString());
        import_segmentation_annotation(result.binding.classes, sample, imported.mask_path.isEmpty() ? QString{} : canonical_path(imported.mask_path));
        result.binding.samples.push_back(std::move(sample));
    }
    if (result.dataset.items.empty())
        throw std::runtime_error("annotation import contains no usable samples");
    return result;
}

std::unique_ptr<IAnnotationCodec> make_dense_mask_codec() {
    return std::make_unique<DenseMaskCodec>();
}

AnnotationCodecRegistry make_default_annotation_codec_registry() {
    AnnotationCodecRegistry registry;
    registry.add(make_dense_mask_codec());
    return registry;
}

} // namespace studio
