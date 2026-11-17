#include "modules/diagnostics/activity_log.h"
#include <QAction>
#include <QDateTime>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDir>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>
namespace studio {
ActivityLog::ActivityLog(QWidget* parent) : QDialog(parent) {
    setObjectName("activityLogDialog");
    setWindowTitle(tr("Activity Log"));
    resize(900, 520);
    auto* layout = new QVBoxLayout(this);
    path_ = new QLineEdit(this);
    path_->setObjectName("activityLogPath");
    path_->setAccessibleName(tr("Log file"));
    path_->setReadOnly(true);
    layout->addWidget(path_);
    content_ = new QPlainTextEdit(this);
    content_->setObjectName("activityLogContent");
    content_->setReadOnly(true);
    layout->addWidget(content_, 1);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::hide);
    layout->addWidget(close);

    const QDir directory(QDir(QCoreApplication::applicationDirPath()).filePath("logs"));
    QDir().mkpath(directory.absolutePath());
    const QString name = "studio-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") +
        "-" + QString::number(QCoreApplication::applicationPid()) + ".log";
    file_.setFileName(directory.filePath(name));
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        qWarning().noquote() << "Cannot open activity log:" << file_.fileName() << file_.errorString();
}
void ActivityLog::mount(QMenu& menu) {
    auto* action = new QAction(tr("Activity Log..."), this);
    action->setObjectName("activityLogAction");
    connect(action, &QAction::triggered, this, &ActivityLog::show_log);
    menu.addAction(action);
}
void ActivityLog::append_log(const QString& text) {
    if (!file_.isOpen()) return;
    const QByteArray line = (QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + "  " + text + "\n").toUtf8();
    if (file_.write(line) != line.size() || !file_.flush())
        qWarning().noquote() << "Cannot write activity log:" << file_.fileName() << file_.errorString();
}
void ActivityLog::show_log() { show_file(tr("Activity Log"), file_.fileName()); }
void ActivityLog::show_run(const QString& root, const QString& run_id) {
    if (run_id.isEmpty() || root.isEmpty()) { show_log(); return; }
    show_file(tr("Training log %1").arg(run_id), QDir(root).filePath("runs/" + run_id + "/training.log"));
}
void ActivityLog::show_file(const QString& title, const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(parentWidget(), tr("Log unavailable"),
            tr("Cannot open log file:\n%1\n\n%2").arg(QDir::toNativeSeparators(path), file.errorString()));
        return;
    }
    setWindowTitle(title);
    path_->setText(QDir::toNativeSeparators(path));
    path_->setToolTip(path_->text());
    content_->setPlainText(QString::fromUtf8(file.readAll()));
    show();
    raise();
    activateWindow();
}
} // namespace studio
