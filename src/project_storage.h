#pragma once

#include <QByteArray>
#include <QString>
#include <string>

namespace studio {

class ProjectStorage {
public:
    static QString validated_relative_path(const QString& value, const char* label);
    static QString portable_path(const std::string& value, const QString& root_directory);
    static std::string runtime_path(const QString& value, const QString& root_directory);
    static QByteArray read(const QString& path);
    static void write_atomic(const QString& path, const QByteArray& data);
    static void copy_directory(const QString& source, const QString& destination);
    static void commit_staging(const QString& staging, const QString& destination);
    static void remove_directory(const QString& path);
};

} // namespace studio
