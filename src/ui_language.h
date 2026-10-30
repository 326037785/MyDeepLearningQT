#pragma once

#include <QString>

class QApplication;
class QTranslator;

namespace studio {

QString ui_language();
void save_ui_language(const QString& language);
void install_ui_language(QApplication& app, QTranslator& qt, QTranslator& translator);

} // namespace studio
