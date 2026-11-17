#include "modules/project/storage/project_storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <stdexcept>

namespace studio {

QString ProjectStorage::validated_relative_path(const QString& value, const char* label) {
    const QString relative = QDir::cleanPath(QDir::fromNativeSeparators(value));
    if (relative.isEmpty() || relative == "." || QFileInfo(relative).isAbsolute() ||
        relative == ".." || relative.startsWith("../"))
        throw std::runtime_error(std::string("invalid ") + label + " path");
    return relative;
}

QString ProjectStorage::portable_path(const std::string& value, const QString& root_directory) {
    if (value.empty()) return {};
    const QString path = QDir::cleanPath(QString::fromStdString(value));
    if (path.startsWith("project:"))
        throw std::runtime_error("runtime path must not contain a serialized project reference");
    if (!QFileInfo(path).isAbsolute()) return "project:" + validated_relative_path(path, "resource");
    const QString relative = QDir(root_directory).relativeFilePath(path);
    if (relative != ".." && !relative.startsWith("../") && !QFileInfo(relative).isAbsolute())
        return "project:" + QDir::fromNativeSeparators(relative);
    return QDir::toNativeSeparators(path);
}

std::string ProjectStorage::runtime_path(const QString& value, const QString& root_directory) {
    if (!value.startsWith("project:")) return QDir::toNativeSeparators(value).toStdString();
    const QString relative = validated_relative_path(value.mid(8), "project-relative resource");
    return QDir::toNativeSeparators(QDir(root_directory).absoluteFilePath(relative)).toStdString();
}

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
