#include "modules/task/ui/common/image_viewport_transform.h"

#include <QtGlobal>

#include <algorithm>

namespace studio {
namespace {

constexpr int kCompactCanvasHeight = 320;
constexpr int kRegularCanvasHeight = 400;

} // namespace

QRect ImageViewportTransform::fit_viewport(const QSize& canvas_size, int regular_margin) {
    const int height = std::clamp(canvas_size.height(), kCompactCanvasHeight, kRegularCanvasHeight);
    const int compact_margin = regular_margin / 2;
    const int margin = compact_margin + (height - kCompactCanvasHeight) *
        (regular_margin - compact_margin) / (kRegularCanvasHeight - kCompactCanvasHeight);
    return {margin, margin, std::max(1, canvas_size.width() - 2 * margin),
            std::max(1, canvas_size.height() - 2 * margin)};
}

void ImageViewportTransform::reset() {
    zoom_ = 1.0;
    pan_offset_ = {};
}

QRectF ImageViewportTransform::target_rect(const QSize& image_size,
                                           const QSize& viewport_size) const {
    if (!image_size.isValid() || image_size.isEmpty() ||
        !viewport_size.isValid() || viewport_size.isEmpty()) {
        return {};
    }

    const QSizeF fitted = image_size.scaled(viewport_size, Qt::KeepAspectRatio);
    const QSizeF scaled(fitted.width() * zoom_, fitted.height() * zoom_);
    const QPointF top_left((viewport_size.width() - scaled.width()) / 2.0,
                           (viewport_size.height() - scaled.height()) / 2.0);
    return QRectF(top_left + pan_offset_, scaled);
}

QPoint ImageViewportTransform::map_to_image(const QPointF& viewport_point,
                                            const QSize& image_size,
                                            const QSize& viewport_size) const {
    const QRectF target = target_rect(image_size, viewport_size);
    if (target.isEmpty() || !target.contains(viewport_point)) return {-1, -1};

    const double normalized_x = (viewport_point.x() - target.left()) / target.width();
    const double normalized_y = (viewport_point.y() - target.top()) / target.height();

    return {
        std::clamp(static_cast<int>(normalized_x * image_size.width()), 0, image_size.width() - 1),
        std::clamp(static_cast<int>(normalized_y * image_size.height()), 0, image_size.height() - 1)};
}

bool ImageViewportTransform::zoom_at(const QPointF& viewport_anchor,
                                     double factor,
                                     const QSize& image_size,
                                     const QSize& viewport_size) {
    const QRectF old_target = target_rect(image_size, viewport_size);
    if (old_target.isEmpty() || factor <= 0.0) return false;

    const double new_zoom = std::clamp(zoom_ * factor, 0.25, 8.0);
    if (qFuzzyCompare(new_zoom, zoom_)) return false;

    const QPointF normalized(
        (viewport_anchor.x() - old_target.left()) / old_target.width(),
        (viewport_anchor.y() - old_target.top()) / old_target.height());

    zoom_ = new_zoom;
    const QRectF new_target = target_rect(image_size, viewport_size);
    const QPointF anchored_point(new_target.left() + normalized.x() * new_target.width(),
                                 new_target.top() + normalized.y() * new_target.height());
    pan_offset_ += viewport_anchor - anchored_point;
    return true;
}

void ImageViewportTransform::pan_by(const QPointF& delta) {
    pan_offset_ += delta;
}

} // namespace studio
