#pragma once
#include <QCoreApplication>
#include <QDialog>
#include <QFile>
class QLineEdit;
class QMenu;
class QPlainTextEdit;
namespace studio {
class ActivityLog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(studio::ActivityLog)
public:
    explicit ActivityLog(QWidget* parent = nullptr);
    void mount(QMenu& menu);
    void append_log(const QString& text);
    void show_log();
    void show_run(const QString& project_root, const QString& run_id);
    QString file_path() const { return file_.fileName(); }
private:
    void show_file(const QString& title, const QString& path);
    QFile file_;
    QLineEdit* path_;
    QPlainTextEdit* content_;
};
} // namespace studio
