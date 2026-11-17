#include "modules/project/storage/recent_project_store.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace studio {

namespace {
QString normalized(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
}

RecentProjectStore::RecentProjectStore(int maximum_entries)
    : maximum_entries_(qMax(1, maximum_entries)) {}

QStringList RecentProjectStore::projects() const {
    return QSettings(
               QSettings::IniFormat, QSettings::UserScope,
               "Jocelin", "DeepLearningStudio")
        .value("recentProjects")
        .toStringList();
}

void RecentProjectStore::write(const QStringList& values) const {
    QSettings settings(
        QSettings::IniFormat, QSettings::UserScope,
        "Jocelin", "DeepLearningStudio");
    settings.setValue("recentProjects", values);
    settings.sync();
}

QStringList RecentProjectStore::add(const QString& project_file) const {
    QStringList values = projects();
    const QString value = normalized(project_file);
    for (auto it = values.begin(); it != values.end();) {
        if (normalized(*it).compare(value, Qt::CaseInsensitive) == 0) it = values.erase(it);
        else ++it;
    }
    if (!value.isEmpty()) values.prepend(value);
    while (values.size() > maximum_entries_) values.removeLast();
    write(values);
    return values;
}

QStringList RecentProjectStore::remove(const QString& project_file) const {
    QStringList values = projects();
    const QString value = normalized(project_file);
    for (auto it = values.begin(); it != values.end();) {
        if (normalized(*it).compare(value, Qt::CaseInsensitive) == 0) it = values.erase(it);
        else ++it;
    }
    write(values);
    return values;
}

QStringList RecentProjectStore::prune() const {
    QStringList values;
    for (const QString& path : projects()) {
        const QString value = normalized(path);
        if (!QFileInfo::exists(value)) continue;
        if (!values.contains(value, Qt::CaseInsensitive)) values.append(value);
        if (values.size() == maximum_entries_) break;
    }
    write(values);
    return values;
}

} // namespace studio
