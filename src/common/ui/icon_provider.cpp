#include "common/ui/icon_provider.h"

#include <QAbstractButton>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QVariant>

namespace studio {

QIcon IconProvider::icon(const QString& name) {
    QSvgRenderer renderer(QString(":/studio/icons/%1.svg").arg(name));
    if (!renderer.isValid()) return {};
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter);
    return QIcon(pixmap);
}

void IconProvider::configure(QAbstractButton* button,
                             const QString& name,
                             const QString& accessible_text,
                             int logical_size) {
    if (!button) return;
    button->setIcon(icon(name));
    button->setIconSize(QSize(logical_size, logical_size));
    button->setText({});
    button->setToolTip(accessible_text);
    button->setAccessibleName(accessible_text);
    button->setProperty("iconTool", true);
}

} // namespace studio
