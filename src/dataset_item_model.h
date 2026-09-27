#pragma once

#include "workflow.h"

#include <QAbstractListModel>

namespace studio {

class DatasetItemModel final : public QAbstractListModel {
public:
    enum Role { PathRole = Qt::UserRole + 1, ValidRole, ErrorRole };

    explicit DatasetItemModel(QObject* parent = nullptr);
    void set_config(const DatasetConfig& config);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    std::vector<DatasetConfig::Item> items_;
};

} // namespace studio
