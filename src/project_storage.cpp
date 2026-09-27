#include "project_storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <stdexcept>

namespace studio {

QByteArray ProjectStorage::read(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(("cannot open " + path).toStdString());
    return file.readAll();
}

void ProjectStorage::write_atomic(const QString& path, const QByteArray& data) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
        !file.commit())
        throw std::runtime_error(("cannot atomically write " + path).toStdString());
}

void ProjectStorage::copy_directory(const QString& source, const QString& destination) {
    const QDir source_dir(source);
    if (!source_dir.exists()) return;
    if (!QDir().mkpath(destination))
        throw std::runtime_error("cannot create Save As staging directory");
    for (const QFileInfo& entry : source_dir.entryInfoList(
             QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
        const QString target = QDir(destination).filePath(entry.fileName());
        if (entry.isDir()) copy_directory(entry.absoluteFilePath(), target);
        else if (!QFile::copy(entry.absoluteFilePath(), target))
            throw std::runtime_error(("cannot copy project resource " + entry.absoluteFilePath()).toStdString());
    }
}

void ProjectStorage::commit_staging(const QString& staging, const QString& destination) {
    const QFileInfo target(destination);
    if (target.exists()) {
        QDir existing(destination);
        if (!existing.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty() ||
            !QDir().rmdir(destination))
            throw std::runtime_error("Save As destination is not empty");
    }
    QDir parent(QFileInfo(destination).absolutePath());
    if (!parent.rename(QFileInfo(staging).fileName(), QFileInfo(destination).fileName()))
        throw std::runtime_error("cannot commit Save As staging directory");
}

void ProjectStorage::remove_directory(const QString& path) {
    if (!path.isEmpty() && QFileInfo::exists(path)) QDir(path).removeRecursively();
}

} // namespace studio
