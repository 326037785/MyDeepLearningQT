#include "modules/task/ui/annotation/shared/annotation_panels.h"

#include "common/ui/icon_provider.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace studio {

QToolButton* annotation_icon_button(QWidget* parent, QHBoxLayout* layout,
                                    const char* name, const char* icon,
                                    const QString& description) {
    QToolButton* button = new QToolButton(parent);
    button->setObjectName(QString::fromLatin1(name));
    IconProvider::configure(button, QString::fromLatin1(icon), description);
    layout->addWidget(button);
    return button;
}

QIcon annotation_class_icon(const QColor& color) {
    QPixmap image(14, 14);
    image.fill(color);
    return QIcon(image);
}

QListWidget* annotation_class_list(QWidget* parent, const char* name) {
    QListWidget* list = new QListWidget(parent);
    list->setObjectName(QString::fromLatin1(name));
    list->setAccessibleName(QObject::tr("Annotation classes"));
    list->setIconSize(QSize(14, 14));
    return list;
}

QLabel* annotation_results_tab(QTabWidget* tabs, const char* details_name) {
    QWidget* panel = new QWidget(tabs);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    QLabel* details = new QLabel(QObject::tr("No inference result for this image."), panel);
    details->setObjectName(QString::fromLatin1(details_name));
    details->setWordWrap(true);
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(details);
    layout->addStretch();
    tabs->addTab(panel, QObject::tr("Results"));
    return details;
}

QString prompt_annotation_class_name(QWidget* parent, const QString& title,
                                     const QString& previous) {
    bool accepted = false;
    const QString name = QInputDialog::getText(parent, title, QObject::tr("Class name"),
        QLineEdit::Normal, previous, &accepted).trimmed();
    return accepted ? name : QString{};
}

} // namespace studio
