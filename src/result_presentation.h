#pragma once

#include <Result/sg/sg_result.h>
#include <QImage>

namespace studio {

inline QImage prediction_overlay_from_result(const jocelin::result::sg::SgResult& result) {
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
    return overlay;
}

} // namespace studio
