#pragma once

#include <training/training_service.h>

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <atomic>
#include <functional>

namespace studio {

struct TrainingOutcome {
    jocelin::training::TrainingResult result;
    QString error;
};

// Runs the currently supported training service off the Qt UI thread and
// marshals callbacks back to the owning thread.
class TrainingJob final : public QObject {
public:
    explicit TrainingJob(QObject* parent = nullptr);

    void start(const jocelin::training::TrainingRequest& request);
    void request_stop();
    void request_pause();
    bool is_running() const { return watcher_.isRunning(); }

    std::function<void(const jocelin::train::TrainingProgress&)> progress;
    std::function<void(const QString&)> log;
    std::function<void(
        const jocelin::training::TrainingResult&,
        const QString&)> completed;

private:
    QFutureWatcher<TrainingOutcome> watcher_;
    std::atomic_bool stop_requested_{false};
    std::atomic_bool pause_requested_{false};
};

} // namespace studio
