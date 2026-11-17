#pragma once

#include <QAbstractItemView>
#include <QImageReader>
#include <QListWidget>
#include <QMenu>
#include <QSignalBlocker>

#include <functional>
#include <string>
#include <vector>

namespace studio {

struct SampleBrowserEntry {
    std::string sample_id;
    QString image_path;
    QString display_name;
    QString annotation_state;
    QString split_state;
    QString tooltip;
    int source_index = -1;
};

class SampleBrowser final : public QListWidget {
public:
    explicit SampleBrowser(QWidget* parent = nullptr) : QListWidget(parent) {
        setTextElideMode(Qt::ElideMiddle);
        setUniformItemSizes(true);
        setSelectionMode(QAbstractItemView::ExtendedSelection);
        setContextMenuPolicy(Qt::CustomContextMenu);

        connect(this, &QListWidget::currentRowChanged, this, [this](int row) {
            if (changing_current_ || row < 0 || row >= count()) return;
            const int source_index = item(row)->data(Qt::UserRole + 1).toInt();
            const QString sample_id = item(row)->data(Qt::UserRole).toString();
            if (current_requested && !current_requested(source_index, sample_id)) {
                QSignalBlocker blocker(this);
                setCurrentRow(accepted_row_);
                return;
            }
            accepted_row_ = row;
        });

        connect(this, &QListWidget::customContextMenuRequested, this,
                [this](const QPoint& position) {
            QListWidgetItem* clicked = itemAt(position);
            if (!clicked) {
                return;
            }
            setCurrentItem(clicked, clicked->isSelected()
                ? QItemSelectionModel::NoUpdate : QItemSelectionModel::ClearAndSelect);

            const auto ids = selected_sample_ids();
            if (ids.empty()) return;
            QMenu menu(this);
            if (split_requested) {
                auto* split_menu = menu.addMenu(tr("Assign split"));
                for (const auto& option : {
                         std::pair<const char*, const char*>("Train", "train"),
                         {"Validation", "validation"}, {"Test", "test"},
                         {"Unassigned", "unassigned"}}) {
                    auto* action = split_menu->addAction(tr(option.first));
                    connect(action, &QAction::triggered, this,
                            [this, ids, value = std::string(option.second)] {
                        split_requested(ids, value);
                    });
                }
            }
            if (extend_context_menu) extend_context_menu(menu, ids);
            if (!menu.actions().isEmpty())
                menu.exec(viewport()->mapToGlobal(position));
        });
    }

    void set_entries(const std::vector<SampleBrowserEntry>& entries) {
        const QString previous = current_sample_id();
        changing_current_ = true;
        clear();
        accepted_row_ = -1;
        for (const auto& entry : entries) {
            QString second_line = entry.annotation_state;
            if (!entry.split_state.isEmpty()) {
                if (!second_line.isEmpty()) second_line += QStringLiteral(" · ");
                second_line += entry.split_state;
            }
            auto* item = new QListWidgetItem(
                second_line.isEmpty()
                    ? entry.display_name
                    : entry.display_name + QStringLiteral("\n") + second_line,
                this);
            item->setData(Qt::UserRole, QString::fromStdString(entry.sample_id));
            item->setData(Qt::UserRole + 1, entry.source_index);
            item->setToolTip(entry.tooltip);
            item->setSizeHint(QSize(0, 64));
            if (!entry.image_path.isEmpty()) {
                QImageReader reader(entry.image_path);
                reader.setAutoTransform(true);
                reader.setScaledSize(QSize(144, 108));
                const QImage image = reader.read();
                if (!image.isNull()) item->setIcon(QPixmap::fromImage(image));
            }
        }
        changing_current_ = false;
        if (!previous.isEmpty()) set_current_sample_id(previous);
        if (currentRow() < 0 && count() > 0)
            set_current_source_index(item(0)->data(Qt::UserRole + 1).toInt());
    }

    void update_entry_state(int source_index,
                            const QString& display_name,
                            const QString& annotation_state,
                            const QString& split_state,
                            const QString& tooltip = {}) {
        for (int row = 0; row < count(); ++row) {
            auto* value = item(row);
            if (value->data(Qt::UserRole + 1).toInt() != source_index) continue;
            QString second_line = annotation_state;
            if (!split_state.isEmpty()) {
                if (!second_line.isEmpty()) second_line += QStringLiteral(" · ");
                second_line += split_state;
            }
            value->setText(second_line.isEmpty()
                ? display_name
                : display_name + QStringLiteral("\n") + second_line);
            if (!tooltip.isNull()) value->setToolTip(tooltip);
            return;
        }
    }

    std::vector<std::string> selected_sample_ids() const {
        std::vector<std::string> ids;
        ids.reserve(static_cast<size_t>(selectedItems().size()));
        for (auto* item : selectedItems())
            ids.push_back(item->data(Qt::UserRole).toString().toStdString());
        return ids;
    }

    QString current_sample_id() const {
        return currentItem() ? currentItem()->data(Qt::UserRole).toString() : QString{};
    }

    int current_source_index() const {
        return currentItem() ? currentItem()->data(Qt::UserRole + 1).toInt() : -1;
    }

    void set_current_sample_id(const QString& sample_id) {
        for (int row = 0; row < count(); ++row) {
            if (item(row)->data(Qt::UserRole).toString() == sample_id) {
                set_current_row_silently(row);
                return;
            }
        }
    }

    void set_current_source_index(int source_index) {
        for (int row = 0; row < count(); ++row) {
            if (item(row)->data(Qt::UserRole + 1).toInt() == source_index) {
                set_current_row_silently(row);
                return;
            }
        }
    }

    std::function<bool(int, const QString&)> current_requested;
    std::function<void(const std::vector<std::string>&, const std::string&)>
        split_requested;
    std::function<void(QMenu&, const std::vector<std::string>&)>
        extend_context_menu;

private:
    void set_current_row_silently(int row) {
        QSignalBlocker blocker(this);
        setCurrentRow(row);
        accepted_row_ = row;
    }

    bool changing_current_ = false;
    int accepted_row_ = -1;
};

} // namespace studio
