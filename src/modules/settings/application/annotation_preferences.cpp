#include "modules/settings/application/annotation_preferences.h"

#include <QSettings>

namespace studio {

bool annotation_auto_save() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "Jocelin", "DeepLearningStudio")
        .value("annotation/autoSave", false).toBool();
}

void set_annotation_auto_save(bool enabled) {
    QSettings(QSettings::IniFormat, QSettings::UserScope, "Jocelin", "DeepLearningStudio")
        .setValue("annotation/autoSave", enabled);
}

} // namespace studio
