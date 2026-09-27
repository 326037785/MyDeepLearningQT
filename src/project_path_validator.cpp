#include "project_path_validator.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QSet>

namespace studio {

namespace {

QString comparable_path(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toCaseFolded();
}

bool is_same_or_child(const QString& path, const QString& parent) {
    const QString candidate = comparable_path(path);
    QString base = comparable_path(parent);
    if (!base.endsWith('/')) base += '/';
    return candidate == base.left(base.size() - 1) || candidate.startsWith(base);
}

} // namespace

QString ProjectPathValidator::validate_name(const QString& name) {
    const QString value = name.trimmed();
    if (value.isEmpty()) return QObject::tr("Please enter a project name.");
    if (value == "." || value == ".." || value.endsWith('.') ||
        value.endsWith(' ') ||
        value.contains(QRegularExpression(R"([<>:"/\\|?*\x00-\x1F])")))
        return QObject::tr("The project name contains characters that are not valid in a Windows directory name.");

    static const QSet<QString> reserved = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
        "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
        "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    if (reserved.contains(value.section('.', 0, 0).toUpper()))
        return QObject::tr("The project name is reserved by Windows.");
    return {};
}

ProjectPathValidation ProjectPathValidator::validate_save_as(
    const QString& name,
    const QString& parent_directory,
    const QString& source_root)
{
    ProjectPathValidation result;
    result.error = validate_name(name);
    if (!result.error.isEmpty()) return result;

    const QString parent_text = parent_directory.trimmed();
    if (parent_text.isEmpty()) {
        result.error = QObject::tr("Please select a project parent directory.");
        return result;
    }
    const QFileInfo parent_info(QDir::cleanPath(parent_text));
    if (parent_info.exists() && !parent_info.isDir()) {
        result.error = QObject::tr("The project parent path is not a directory.");
        return result;
    }
    QFileInfo writable_parent(parent_info.absoluteFilePath());
    while (!writable_parent.exists()) writable_parent.setFile(writable_parent.absolutePath());
    if (!writable_parent.isDir() || !writable_parent.isWritable()) {
        result.error = QObject::tr("The project parent directory is not writable.");
        return result;
    }

    result.final_path = QDir(parent_info.absoluteFilePath()).absoluteFilePath(name.trimmed());
    if (!is_same_or_child(result.final_path, parent_info.absoluteFilePath())) {
        result.error = QObject::tr("The project path escapes the selected parent directory.");
        return result;
    }
    if (!source_root.isEmpty() && is_same_or_child(result.final_path, source_root)) {
        result.error = comparable_path(result.final_path) == comparable_path(source_root)
            ? QObject::tr("Choose a location other than the current project directory.")
            : QObject::tr("A project copy cannot be created inside its source project.");
        return result;
    }

    const QFileInfo target(result.final_path);
    if (target.exists()) {
        if (!target.isDir()) {
            result.error = QObject::tr("The target path is not a directory.");
            return result;
        }
        if (!QDir(target.absoluteFilePath())
                 .entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
            result.error = QObject::tr("The target directory already exists and is not empty.");
            return result;
        }
    }
    return result;
}

} // namespace studio
