#include "modules/settings/application/ui_language.h"

#include <QApplication>
#include <QSettings>
#include <QTranslator>

namespace studio {

QString ui_language() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "Jocelin", "DeepLearningStudio")
        .value("ui/language", "en").toString();
}

void save_ui_language(const QString& language) {
    QSettings(QSettings::IniFormat, QSettings::UserScope, "Jocelin", "DeepLearningStudio")
        .setValue("ui/language", language);
}

void install_ui_language(QApplication& app, QTranslator& qt, QTranslator& translator) {
    if (ui_language() != "zh_CN") return;
    if (qt.load(":/studio/translations/qt_zh_CN.qm")) app.installTranslator(&qt);
    if (translator.load(":/studio/translations/studio_zh_CN.qm")) app.installTranslator(&translator);
}

} // namespace studio
