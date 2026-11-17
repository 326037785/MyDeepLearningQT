#pragma once

#include <QIcon>
#include <QString>

class QAbstractButton;

namespace studio {

class IconProvider final {
public:
    static QIcon icon(const QString& name);
    static void configure(QAbstractButton* button,
                          const QString& name,
                          const QString& accessible_text,
                          int logical_size = 20);
};

} // namespace studio
