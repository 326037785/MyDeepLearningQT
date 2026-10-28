#pragma once

#include <QByteArray>
#include <QString>

namespace studio {

class TrainingRunStore {
public:
    static TrainingRunStore load(const QString& root_directory);
    QByteArray serialize() const;
    void save(const QString& root_directory, const QByteArray& data);
    int allocate();
    bool is_dirty() const { return dirty_; }

private:
    int next_number_ = 1;
    bool dirty_ = false;
};

} // namespace studio
