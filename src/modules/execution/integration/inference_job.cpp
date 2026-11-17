#include "modules/execution/integration/inference_job.h"
#include <common/exception/pipeline_exception.h>

#include <QtConcurrent/QtConcurrentRun>

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace studio {

InferenceJob::InferenceJob(QObject* parent) : QObject(parent) {
    connect(&watcher_, &QFutureWatcher<InferenceOutcome>::finished, this, [this] {
        if (completed) completed(watcher_.result());
    });
}

void InferenceJob::start(const jocelin::inference::InferenceRequest& request) {
    if (watcher_.isRunning()) throw std::runtime_error("inference job is already running");
    watcher_.setFuture(QtConcurrent::run([this, request] {
        InferenceOutcome outcome;
        try {
            auto response = jocelin::inference::infer(request, [this](size_t done, size_t total, const std::string& path) {
                const QString image = QString::fromStdString(path);
                QMetaObject::invokeMethod(this, [this, done, total, image] { if (progress) progress(done, total, image); }, Qt::QueuedConnection);
            });
            outcome.report = std::move(response.report);
            outcome.fallback = std::move(response.fallback);
            std::visit([&outcome](const auto& report) {
                outcome.summary = QObject::tr("Inference completed for %1 image(s).").arg(report.samples.size());
                for (const auto& record : report.samples) {
                    outcome.summary += "\n" + QString::fromStdString(record.image_path) +
                        " -> " + QString::fromStdString(record.result_name);
                    using Report = std::decay_t<decltype(report)>;
                    if constexpr (std::is_same_v<Report, jocelin::result::cls::ClsInferenceResult>) {
                        outcome.summary += QObject::tr(" class=%1 confidence=%2")
                            .arg(record.predicted_class == -1 ? QObject::tr("Unknown") : QString::number(record.predicted_class))
                            .arg(record.confidence);
                    } else {
                        outcome.summary += QObject::tr(" confidence=%1 area=%2")
                            .arg(record.mean_confidence).arg(record.predicted_area);
                    }
                }
            }, outcome.report);
        } catch (...) {
            outcome.error = QString::fromStdString(jocelin::exception::message(
                std::current_exception(), QObject::tr("Unknown inference error").toStdString()));
        }
        return outcome;
    }));
}

} // namespace studio
