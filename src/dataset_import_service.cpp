#include "dataset_import_service.h"
#include "task_dataset_binding.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImageReader>
#include <QSet>

#include <algorithm>

namespace studio {
namespace {

QString canonical_path(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::toNativeSeparators(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QString dataset_directory_for_source(const QString& path) {
    const QFileInfo info(path);
    if (!info.isDir()) return {};

    const QDir directory(info.absoluteFilePath());
    if (directory.dirName().compare(QStringLiteral("images"), Qt::CaseInsensitive) == 0) {
        QDir parent(directory);
        if (parent.cdUp() && parent.exists(QStringLiteral("masks")))
            return canonical_path(parent.absolutePath());
    }
    return canonical_path(info.absoluteFilePath());
}

QStringList expand_paths(const QStringList& paths) {
    QStringList files;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            // A standard dataset root owns sibling images/masks/metadata. Only
            // images are importable samples, while the root remains the
            // authoritative directory used by annotation and training.
            const QDir directory(info.absoluteFilePath());
            const QString image_directory = directory.filePath("images");
            const QString scan_root = QDir(image_directory).exists()
                ? image_directory : info.absoluteFilePath();
            QDirIterator iterator(scan_root, QDir::Files | QDir::Readable,
                                  QDirIterator::Subdirectories);
            while (iterator.hasNext()) files.push_back(iterator.next());
        } else {
            files.push_back(path);
        }
    }
    files.removeDuplicates();
    std::sort(files.begin(), files.end(), [](const QString& left, const QString& right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    return files;
}

QString common_directory(const std::vector<DatasetConfig::Item>& items) {
    QString common;
    for (const auto& item : items) {
        if (!item.valid) continue;
        QString directory = QFileInfo(QString::fromStdString(item.path)).absolutePath();
        if (common.isEmpty()) {
            common = directory;
            continue;
        }
        while (!directory.startsWith(common + QDir::separator(), Qt::CaseInsensitive) &&
               directory.compare(common, Qt::CaseInsensitive) != 0) {
            const QString parent = QFileInfo(common).absolutePath();
            if (parent == common) return {};
            common = parent;
        }
    }
    return QDir::toNativeSeparators(common);
}

} // namespace

DatasetImportResult DatasetImportService::import_paths(
    const DatasetConfig& current, const QStringList& paths, bool append)
{
    DatasetImportResult result;
    result.config = append ? current : DatasetConfig{};
    result.config.bucket_capacity = 1;

    if (paths.isEmpty()) {
        result.error = QObject::tr("No files or folders were selected.");
        return result;
    }

    QSet<QString> known;
    for (const auto& item : result.config.items)
        known.insert(canonical_path(QString::fromStdString(item.path)).toLower());

    const QStringList files = expand_paths(paths);
    if (files.isEmpty()) {
        result.error = QObject::tr("The selected folder contains no files.");
        return result;
    }

    for (const QString& file : files) {
        const QString path = canonical_path(file);
        const QString key = path.toLower();
        if (known.contains(key)) {
            ++result.duplicate;
            continue;
        }
        known.insert(key);

        DatasetConfig::Item item;
        item.path = path.toStdString();
        item.sample_id = stable_sample_id({item.path});
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile()) {
            item.valid = false;
            item.error = QObject::tr("File does not exist").toStdString();
        } else {
            QImageReader reader(path);
            reader.setDecideFormatFromContent(true);
            if (!reader.canRead()) {
                item.valid = false;
                const QString detail = reader.errorString().isEmpty()
                    ? QObject::tr("Unsupported or damaged image") : reader.errorString();
                item.error = detail.toStdString();
            }
        }
        result.config.items.push_back(std::move(item));
        ++result.added;
    }

    if (paths.size() == 1 && QFileInfo(paths.front()).isDir())
        result.config.directory = dataset_directory_for_source(paths.front()).toStdString();
    else
        result.config.directory = common_directory(result.config.items).toStdString();
    return result;
}

DatasetConfig DatasetImportService::cleared(const DatasetConfig& current) {
    DatasetConfig result;
    result.bucket_capacity = 1;
    (void)current;
    return result;
}

QStringList DatasetImportService::valid_paths(const DatasetConfig& config) {
    QStringList result;
    for (const auto& item : config.items)
        if (item.valid) result.push_back(QString::fromStdString(item.path));
    return result;
}

} // namespace studio
