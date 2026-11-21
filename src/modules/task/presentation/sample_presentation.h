#pragma once

#include "modules/workflow/model/workflow.h"

#include <QFileInfo>
#include <QImageReader>
#include <QObject>
#include <QString>
#include <QStringList>

#include <stdexcept>

namespace studio {

inline QString split_label(const std::string& value) {
    if (value == "train") {
        return QObject::tr("Train");
    }
    if (value == "validation") {
        return QObject::tr("Validation");
    }
    if (value == "test") {
        return QObject::tr("Test");
    }
    return QObject::tr("Test (default)");
}

inline QString annotation_label(const TaskSampleBinding& sample) {
    if (sample.annotation_state == "annotated") {
        return QObject::tr("Annotated");
    }
    return QObject::tr("Not annotated");
}


inline QString image_display_name(
    int sample_index,
    const TaskSampleBinding& sample)
{
    if (sample_index < 0 || sample.image_paths.empty())
        throw std::invalid_argument(
            "image display name requires a valid sample index and image path");
    const QString suffix = QFileInfo(
        QString::fromStdString(sample.image_paths.front())).suffix();
    return QStringLiteral("img_%1%2")
        .arg(sample_index + 1, 5, 10, QChar('0'))
        .arg(suffix.isEmpty() ? QString{} : QStringLiteral(".") + suffix);
}

inline int task_sample_index(
    const TaskDatasetBinding& binding,
    const std::string& sample_id)
{
    for (int index = 0;
         index < static_cast<int>(binding.samples.size());
         ++index)
        if (binding.samples[static_cast<size_t>(index)].sample_id == sample_id)
            return index;
    return -1;
}

inline const TaskSampleBinding* task_sample_for_path(
    const TaskDatasetBinding& binding,
    const QString& image_path,
    int* sample_index = nullptr,
    int* image_index = nullptr)
{
    const QString absolute = QFileInfo(image_path).absoluteFilePath();
    for (int sample_row = 0;
         sample_row < static_cast<int>(binding.samples.size());
         ++sample_row) {
        const auto& sample = binding.samples[static_cast<size_t>(sample_row)];
        for (int image_row = 0;
             image_row < static_cast<int>(sample.image_paths.size());
             ++image_row) {
            const QString candidate = QFileInfo(QString::fromStdString(
                sample.image_paths[static_cast<size_t>(image_row)]))
                .absoluteFilePath();
            if (candidate != absolute) continue;
            if (sample_index) *sample_index = sample_row;
            if (image_index) *image_index = image_row;
            return &sample;
        }
    }
    return nullptr;
}

inline QString sample_image_tooltip(
    const QString& display_name,
    const TaskSampleBinding& sample,
    const QString& annotation)
{
    QStringList lines;
    lines.push_back(QObject::tr("Display name: %1").arg(display_name));
    if (sample.image_paths.size() > 1)
        lines.push_back(QObject::tr("Images in sample: %1")
                            .arg(sample.image_paths.size()));
    for (int index = 0;
         index < static_cast<int>(sample.image_paths.size());
         ++index) {
        const QFileInfo info(QString::fromStdString(
            sample.image_paths[static_cast<size_t>(index)]));
        const QSize dimensions = QImageReader(info.absoluteFilePath()).size();
        const QString image_suffix = sample.image_paths.size() > 1
            ? QStringLiteral(" %1").arg(index + 1)
            : QString{};
        lines.push_back(QObject::tr("Original name%1: %2")
                            .arg(image_suffix, info.fileName()));
        lines.push_back(QObject::tr("Path%1: %2")
                            .arg(image_suffix, info.absoluteFilePath()));
        lines.push_back(QObject::tr("Resolution%1: %2")
                            .arg(
                                image_suffix,
                                dimensions.isValid()
                                    ? QObject::tr("%1 × %2")
                                          .arg(dimensions.width())
                                          .arg(dimensions.height())
                                    : QObject::tr("Unavailable")));
    }
    lines.push_back(QObject::tr("Sample ID: %1")
                        .arg(QString::fromStdString(sample.sample_id)));
    lines.push_back(QObject::tr("Annotation: %1").arg(annotation));
    return lines.join('\n');
}

} // namespace studio
