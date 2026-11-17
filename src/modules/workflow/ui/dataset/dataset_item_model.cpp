#include "modules/workflow/ui/dataset/dataset_item_model.h"

#include <QFileIconProvider>
#include <QFileInfo>
#include <QIcon>
#include <QColor>

namespace studio {

DatasetItemModel::DatasetItemModel(QObject* parent) : QAbstractListModel(parent) {}

void DatasetItemModel::set_config(const DatasetConfig& config) {
    beginResetModel();
    items_ = config.items;
    endResetModel();
}

int DatasetItemModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

QVariant DatasetItemModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& item = items_.at(static_cast<size_t>(index.row()));
    const QString path = QString::fromStdString(item.path);
    const QFileInfo info(path);
    const QString suffix = info.suffix();
    const QString display_name = QStringLiteral("img_%1%2")
        .arg(index.row() + 1, 5, 10, QChar('0'))
        .arg(suffix.isEmpty() ? QString{} : QStringLiteral(".") + suffix);
    if (role == Qt::DisplayRole) return display_name;
    if (role == Qt::ToolTipRole) {
        QString tooltip = display_name + "\n" + info.fileName() + "\n" + path;
        if (!item.valid) tooltip += "\n" + QString::fromStdString(item.error);
        return tooltip;
    }
    if (role == Qt::ForegroundRole && !item.valid) return QColor("#A74720");
    if (role == Qt::DecorationRole)
        return item.valid ? QIcon(path) : QIcon::fromTheme("dialog-error");
    if (role == PathRole) return path;
    if (role == ValidRole) return item.valid;
    if (role == ErrorRole) return QString::fromStdString(item.error);
    return {};
}

QHash<int, QByteArray> DatasetItemModel::roleNames() const {
    return {{PathRole, "path"}, {ValidRole, "valid"}, {ErrorRole, "error"}};
}

} // namespace studio
