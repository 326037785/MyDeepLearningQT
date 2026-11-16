#pragma once

#include "workflow.h"

#include <Result/sg/sg_result.h>
#include <QColor>
#include <QHash>
#include <QImage>

#include <algorithm>

namespace studio {

inline QImage prediction_overlay_from_result(
    const jocelin::result::sg::SgResult& result,
    const std::vector<jocelin::model::ModelClass>& model_classes = {},
    const std::vector<AnnotationClass>& task_classes = {}) {
    const cv::Mat color = result.render_interest_mask();
    QImage overlay(color.cols, color.rows, QImage::Format_ARGB32);
    for (int y = 0; y < color.rows; ++y) {
        const auto* input = color.ptr<cv::Vec3b>(y);
        auto* output = reinterpret_cast<QRgb*>(overlay.scanLine(y));
        for (int x = 0; x < color.cols; ++x) {
            const auto pixel = input[x];
            output[x] = pixel == cv::Vec3b(0, 0, 0) ? 0 : qRgb(pixel[2], pixel[1], pixel[0]);
        }
    }
    QHash<int, QRgb> colors;
    for (const jocelin::model::ModelClass& model_class : model_classes) {
        const auto found = std::find_if(task_classes.begin(), task_classes.end(),
            [&model_class](const AnnotationClass& task_class) { return task_class.id == model_class.id; });
        if (found == task_classes.end()) {
            continue;
        }
        const QColor selected(QString::fromStdString(found->color));
        if (selected.isValid()) {
            colors.insert(model_class.mask_value, selected.rgba());
        }
    }
    for (const jocelin::result::sg::SgResult::Component& component : result.components) {
        if (!component.interest || !colors.contains(component.label)) {
            continue;
        }
        for (const jocelin::geo::Run& run : component.region.vRun) {
            if (run.row < 0 || run.row >= overlay.height()) {
                continue;
            }
            QRgb* pixels = reinterpret_cast<QRgb*>(overlay.scanLine(run.row));
            const int start = std::clamp(run.start, 0, overlay.width());
            const int end = std::clamp(run.end, 0, overlay.width());
            if (start < end) {
                std::fill(pixels + start, pixels + end, colors.value(component.label));
            }
        }
    }
    return overlay;
}

} // namespace studio
