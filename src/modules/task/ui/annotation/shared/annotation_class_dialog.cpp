#include "modules/task/ui/annotation/shared/annotation_class_dialog.h"
#include "common/ui/icon_provider.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace studio {

std::string choose_annotation_class(QWidget* parent, const std::vector<AnnotationClass>& classes,
    const std::function<std::string(const QString&)>& add_class) {
    QDialog board(parent);
    board.setObjectName("annotationClassBoard");
    board.setWindowTitle(QObject::tr("Choose annotation class"));
    QVBoxLayout* layout = new QVBoxLayout(&board);
    QComboBox* existing = new QComboBox(&board);
    existing->setObjectName("annotationExistingClass");
    existing->setAccessibleName(QObject::tr("Existing class"));
    for (const AnnotationClass& schema : classes) {
        if (!schema.locked) {
            existing->addItem(QString::fromStdString(schema.name), QString::fromStdString(schema.id));
        }
    }
    QLineEdit* name = new QLineEdit(&board);
    name->setObjectName("annotationNewClassName");
    name->setPlaceholderText(QObject::tr("Class name"));
    name->setAccessibleName(QObject::tr("New class name"));
    QToolButton* add = new QToolButton(&board);
    add->setObjectName("annotationAddClassButton");
    IconProvider::configure(add, "add", QObject::tr("Add class"));
    add->setFocusPolicy(Qt::StrongFocus);
    add->setCheckable(true);
    const bool has_existing = existing->count() > 0;
    add->setVisible(has_existing);
    add->setChecked(!has_existing);
    QHBoxLayout* input = new QHBoxLayout;
    input->addWidget(existing, 1);
    input->addWidget(name, 1);
    input->addWidget(add);
    layout->addLayout(input);
    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &board);
    layout->addWidget(buttons);
    QPushButton* confirm = buttons->button(QDialogButtonBox::Ok);
    const auto update_confirm = [add, name, confirm] {
        confirm->setEnabled(!add->isChecked() || !name->text().trimmed().isEmpty());
    };
    const auto show_input = [existing, name, update_confirm](bool creating) {
        existing->setVisible(!creating);
        name->setVisible(creating);
        if (creating) {
            name->setFocus();
        } else {
            existing->setFocus();
        }
        update_confirm();
    };
    QObject::connect(add, &QToolButton::toggled, &board, show_input);
    QObject::connect(name, &QLineEdit::textChanged, &board, update_confirm);
    show_input(add->isChecked());
    std::string class_id;
    QObject::connect(buttons, &QDialogButtonBox::accepted, &board,
            [&board, &class_id, &classes, &add_class, add, existing, name] {
        const std::string id = add->isChecked()
            ? add_class(name->text().trimmed())
            : existing->currentData().toString().toStdString();
        const auto found = std::find_if(classes.begin(), classes.end(),
            [&id](const AnnotationClass& value) { return value.id == id; });
        const AnnotationClass* schema = found == classes.end() ? nullptr : &*found;
        if (schema && !schema->locked) {
            class_id = id;
            board.accept();
        }
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &board, &QDialog::reject);
    board.exec();
    return class_id;
}

} // namespace studio
