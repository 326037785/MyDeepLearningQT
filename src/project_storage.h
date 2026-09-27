#pragma once

#include <QByteArray>
#include <QString>

namespace studio {

class ProjectStorage {
public:
    static QByteArray read(const QString& path);
    static void write_atomic(const QString& path, const QByteArray& data);
    static void copy_directory(const QString& source, const QString& destination);
    static void commit_staging(const QString& staging, const QString& destination);
    static void remove_directory(const QString& path);
};

} // namespace studio
