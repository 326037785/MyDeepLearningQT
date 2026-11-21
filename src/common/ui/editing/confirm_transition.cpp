#include "common/ui/editing/confirm_transition.h"

#include <QMessageBox>

namespace studio {

bool confirm_transition(QWidget* parent, bool dirty, bool auto_save,
                        const std::function<bool()>& save,
                        const std::function<void()>& discard) {
    if (!dirty) {
        return true;
    }
    if (auto_save) {
        return save();
    }
    const QMessageBox::StandardButton answer = QMessageBox::warning(
        parent, QObject::tr("Unsaved annotations"),
        QObject::tr("Save the current annotation before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Save) {
        return save();
    }
    if (answer == QMessageBox::Discard) {
        discard();
        return true;
    }
    return false;
}

} // namespace studio
