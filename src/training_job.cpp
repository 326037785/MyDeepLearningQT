#include "training_job.h"
#include <common/exception/pipeline_exception.h>

#include <QtConcurrent/QtConcurrentRun>

#include <stdexcept>

namespace studio {

TrainingJob::TrainingJob(QObject* parent) : QObject(parent) {
    connect(
        &watcher_,
        &QFutureWatcher<TrainingOutcome>::finished,
        this,
        [this] {
            const auto outcome = watcher_.result();
            if (completed)
                completed(outcome.result, outcome.error);
        });
}

void TrainingJob::start(
    const jocelin::training::TrainingRequest& request)
{
    if (watcher_.isRunning())
        throw std::runtime_error(
            "training job is already running");
    stop_requested_.store(false);
    pause_requested_.store(false);
    watcher_.setFuture(QtConcurrent::run([this, request] {
        TrainingOutcome outcome;
        try {
            jocelin::training::TrainingCallbacks callbacks;
            callbacks.progress =
                [this](const jocelin::train::TrainingProgress& value) {
                    QMetaObject::invokeMethod(
                        this,
                        [this, value] {
                            if (progress) progress(value);
                        },
                        Qt::QueuedConnection);
                };
            callbacks.log = [this](const std::string& value) {
                const QString message =
                    QString::fromStdString(value);
                QMetaObject::invokeMethod(
                    this,
                    [this, message] {
                        if (log) log(message);
                    },
                    Qt::QueuedConnection);
            };
            callbacks.stop_requested = [this] {
                return stop_requested_.load();
            };
            callbacks.adjustment = [this](const jocelin::train::ConfigAdjustment& value) {
                if (value.field != "device") return;
                const QString message = QObject::tr("Training device fallback: %1 -> %2. %3")
                    .arg(QString::fromStdString(value.requested),
                         QString::fromStdString(value.effective),
                         QString::fromStdString(value.reason));
                QMetaObject::invokeMethod(this, [this, message] {
                    if (warning) warning(message);
                }, Qt::QueuedConnection);
            };
            callbacks.pause_requested = [this] {
                return pause_requested_.load();
            };
            outcome.result =
                jocelin::training::train(request, callbacks);
        } catch (...) {
            outcome.error = QString::fromStdString(jocelin::exception::message(
                std::current_exception(), QObject::tr("Unknown training error").toStdString()));
        }
        return outcome;
    }));
}

void TrainingJob::request_stop() {
    stop_requested_.store(true);
}

void TrainingJob::request_pause() {
    pause_requested_.store(true);
}

} // namespace studio
