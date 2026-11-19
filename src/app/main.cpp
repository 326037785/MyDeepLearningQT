#include "app/main_window.h"
#include "modules/settings/application/ui_language.h"

#include <QApplication>
#include <QIcon>
#include <QTranslator>

int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/studio/studio.ico"));
    QTranslator qt_translator;
    QTranslator studio_translator;
    studio::install_ui_language(app, qt_translator, studio_translator);
    studio::MainWindow window;
    window.show();
    return app.exec();
}
