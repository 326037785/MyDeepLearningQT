#pragma once

#include <QColor>
#include <QMenu>
#include <QPair>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QPainter;

namespace studio {

// A transient, task-independent view of one result item. A text-only item can
// leave hit_test empty and be activated directly by its text control.
struct ResultPresentationItem {
    QString text;
    QVector<QPair<QString, QString>> attributes;
    std::function<bool(const QPointF&)> hit_test;
    QRectF outline;
    QColor color;
};

void paint_labeled_box(QPainter& painter, const QRectF& frame, const QRectF& clip,
                       const QString& name, const QString& score, const QColor& color,
                       bool prediction, bool selected = false);

inline int result_item_at(const QVector<ResultPresentationItem>& items, const QPointF& position) {
    for (int index = items.size() - 1; index >= 0; --index) {
        if (items[index].hit_test && items[index].hit_test(position)) {
            return index;
        }
    }
    return -1;
}

inline QMenu* show_result_properties(
    QWidget* parent, const QPoint& global_position, const ResultPresentationItem& item) {
    if (item.attributes.isEmpty()) {
        return nullptr;
    }
    QMenu* menu = new QMenu(parent);
    menu->setObjectName("resultPropertiesPopup");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (const QPair<QString, QString>& attribute : item.attributes) {
        menu->addAction(QStringLiteral("%1: %2").arg(attribute.first, attribute.second));
    }
    menu->popup(global_position);
    return menu;
}

} // namespace studio
