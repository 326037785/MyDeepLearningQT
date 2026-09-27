#include "main_window.h"

#include <QApplication>
#include <QFontDatabase>

namespace {

QFont application_font() {
    const QStringList installed = QFontDatabase().families();
    for (const QString& preferred : {"Segoe UI Variable Text", "Segoe UI", "Microsoft YaHei UI", "Noto Sans", "DejaVu Sans"}) {
        if (installed.contains(preferred, Qt::CaseInsensitive)) {
            QFont font(preferred);
            font.setPointSizeF(8.5);
            font.setStyleStrategy(QFont::PreferAntialias);
            return font;
        }
    }
    return QFontDatabase::systemFont(QFontDatabase::GeneralFont);
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    app.setFont(application_font());
    studio::MainWindow window;
    window.show();
    return app.exec();
}
