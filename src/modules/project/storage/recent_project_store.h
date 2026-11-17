#pragma once

#include <QStringList>

namespace studio {

class RecentProjectStore {
public:
    explicit RecentProjectStore(int maximum_entries = 8);

    QStringList projects() const;
    QStringList add(const QString& project_file) const;
    QStringList remove(const QString& project_file) const;
    QStringList prune() const;

private:
    void write(const QStringList& projects) const;
    int maximum_entries_;
};

} // namespace studio
