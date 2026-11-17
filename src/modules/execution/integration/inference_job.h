#pragma once

#include <inference/inference_service.h>

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <functional>

namespace studio {

struct InferenceOutcome {
    QString summary;
    QString error;
    jocelin::inference::InferenceReport report;
    jocelin::runtime::FallbackNotice fallback;
};

class InferenceJob final : public QObject {
public:
    explicit InferenceJob(QObject* parent = nullptr);
    void start(const jocelin::inference::InferenceRequest& request);
    bool is_running() const { return watcher_.isRunning(); }
    std::function<void(size_t, size_t, const QString&)> progress;
    std::function<void(const InferenceOutcome&)> completed;

private:
    QFutureWatcher<InferenceOutcome> watcher_;
};

} // namespace studio
