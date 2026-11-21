#pragma once

#include <QString>

class QWidget;
class QIcon;
class QColor;
class QHBoxLayout;
class QLabel;
class QListWidget;
class QTabWidget;
class QToolButton;

namespace studio {

QToolButton* annotation_icon_button(QWidget* parent, QHBoxLayout* layout,
                                    const char* name, const char* icon,
                                    const QString& description);
QIcon annotation_class_icon(const QColor& color);
QListWidget* annotation_class_list(QWidget* parent, const char* name);
QLabel* annotation_results_tab(QTabWidget* tabs, const char* details_name);
QString prompt_annotation_class_name(QWidget* parent, const QString& title,
                                     const QString& previous = {});

} // namespace studio
