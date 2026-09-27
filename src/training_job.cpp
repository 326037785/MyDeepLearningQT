#include "training_job.h"

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
            callbacks.pause_requested = [this] {
                return pause_requested_.load();
            };
            outcome.result =
                jocelin::training::train(request, callbacks);
        } catch (const std::exception& error) {
            outcome.error = QString::fromUtf8(error.what());
        } catch (...) {
            outcome.error =
                QObject::tr("Unknown training error");
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
