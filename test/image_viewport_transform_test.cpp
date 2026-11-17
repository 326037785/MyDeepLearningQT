#include "modules/task/ui/common/image_viewport_transform.h"

#include <QtGlobal>

namespace {

bool close_enough(qreal lhs, qreal rhs) {
    return qAbs(lhs - rhs) < 0.001;
}

bool rect_matches(const QRectF& rect, qreal x, qreal y, qreal width, qreal height) {
    return close_enough(rect.x(), x) && close_enough(rect.y(), y) &&
           close_enough(rect.width(), width) && close_enough(rect.height(), height);
}

} // namespace

int main() {
    studio::ImageViewportTransform viewport;
    const QSize image_size(100, 50);
    const QSize viewport_size(200, 200);

    if (!rect_matches(viewport.target_rect(image_size, viewport_size), 0, 50, 200, 100)) return 1;
    if (viewport.map_to_image(QPointF(100, 100), image_size, viewport_size) != QPoint(50, 25)) return 2;
    if (viewport.map_to_image(QPointF(100, 20), image_size, viewport_size) != QPoint(-1, -1)) return 3;

    if (!viewport.zoom_at(QPointF(100, 100), 2.0, image_size, viewport_size)) return 4;
    if (!rect_matches(viewport.target_rect(image_size, viewport_size), -100, 0, 400, 200)) return 5;
    if (viewport.map_to_image(QPointF(100, 100), image_size, viewport_size) != QPoint(50, 25)) return 6;

    viewport.pan_by(QPointF(10, -20));
    if (!rect_matches(viewport.target_rect(image_size, viewport_size), -90, -20, 400, 200)) return 7;

    viewport.reset();
    if (!rect_matches(viewport.target_rect(image_size, viewport_size), 0, 50, 200, 100)) return 8;
    return 0;
}
