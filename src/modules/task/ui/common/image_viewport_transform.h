#pragma once

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>

namespace studio {

// Owns viewport geometry only. It has no QWidget dependency and does not
// know anything about masks, painting tools, or mouse-button state.
class ImageViewportTransform final {
public:
    static QRect fit_viewport(const QSize& canvas_size, int regular_margin = 24);

    void reset();

    QRectF target_rect(const QSize& image_size, const QSize& viewport_size) const;
    QPoint map_to_image(const QPointF& viewport_point,
                        const QSize& image_size,
                        const QSize& viewport_size) const;

    bool zoom_at(const QPointF& viewport_anchor,
                 double factor,
                 const QSize& image_size,
                 const QSize& viewport_size);
    void pan_by(const QPointF& delta);

    double zoom() const { return zoom_; }
    const QPointF& pan_offset() const { return pan_offset_; }

private:
    double zoom_ = 1.0;
    QPointF pan_offset_;
};

} // namespace studio
