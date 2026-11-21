#pragma once

#include <QToolButton>

namespace studio {

inline QToolButton* make_annotation_panel_toggle(
    QWidget* parent, const QString& text, const QString& object_name)
{
    QToolButton* button = new QToolButton(parent);
    button->setObjectName(object_name);
    button->setText(text);
    button->setCheckable(true);
    button->setChecked(true);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
}

} // namespace studio
