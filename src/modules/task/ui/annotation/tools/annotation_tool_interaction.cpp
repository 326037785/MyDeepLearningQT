#include "modules/task/ui/annotation/tools/annotation_tool_interaction.h"

#include <QLineF>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace studio {
namespace {

constexpr qreal kMinimumTransformScale = 0.05;
constexpr qreal kMaximumTransformScale = 32.0;
constexpr double kDegreesPerRadian = 57.29577951308232;
constexpr qreal kTransformHandleRadius = 5.0;
constexpr qreal kRotationHandleDistance = 24.0;

QTransform transform_about(const QPointF& anchor, qreal rotation_degrees,
                           qreal scale_x, qreal scale_y) {
    QTransform transform;
    transform.translate(anchor.x(), anchor.y());
    transform.rotate(rotation_degrees);
    transform.scale(scale_x, scale_y);
    transform.translate(-anchor.x(), -anchor.y());
    return transform;
}

qreal dot_product(const QPointF& left, const QPointF& right) {
    return left.x() * right.x() + left.y() * right.y();
}

qreal vector_length(const QPointF& vector) {
    return std::hypot(vector.x(), vector.y());
}

qreal normalized_angle_delta(qreal degrees) {
    while (degrees > 180.0) {
        degrees -= 360.0;
    }
    while (degrees < -180.0) {
        degrees += 360.0;
    }
    return degrees;
}

} // namespace

void AnnotationToolInteraction::set_tool(AnnotationTool tool) {
    if (tool != AnnotationTool::Polygon) {
        cancel_polygon();
    }
    tool_ = tool;
    painting_ = false;
    stroke_last_point_ = {-1, -1};
    shape_start_ = {-1, -1};
}

void AnnotationToolInteraction::reset() {
    painting_ = false;
    stroke_last_point_ = {-1, -1};
    shape_start_ = {-1, -1};
    hover_point_ = {-1, -1};
    polygon_points_.clear();
}

AnnotationGesture AnnotationToolInteraction::press(const QPoint& point) {
    if (point.x() < 0) {
        return {};
    }
    AnnotationGesture gesture;
    gesture.point = point;
    if (tool_ == AnnotationTool::Pick || tool_ == AnnotationTool::Fill) {
        gesture.kind = AnnotationGesture::Kind::Point;
    } else if (tool_ == AnnotationTool::Polygon) {
        polygon_points_.append(point);
        hover_point_ = point;
    } else if (tool_ == AnnotationTool::Brush || tool_ == AnnotationTool::Eraser) {
        painting_ = true;
        stroke_last_point_ = point;
        hover_point_ = point;
        gesture.kind = AnnotationGesture::Kind::StrokeStart;
    } else if (tool_ == AnnotationTool::Rectangle || tool_ == AnnotationTool::Ellipse ||
               tool_ == AnnotationTool::ThresholdRoi) {
        painting_ = true;
        shape_start_ = point;
        hover_point_ = point;
    }
    return gesture;
}

AnnotationGesture AnnotationToolInteraction::move(const QPoint& point) {
    hover_point_ = point;
    if (!painting_ || (tool_ != AnnotationTool::Brush && tool_ != AnnotationTool::Eraser) ||
        point.x() < 0) {
        return {};
    }
    AnnotationGesture gesture;
    gesture.kind = AnnotationGesture::Kind::StrokeSegment;
    gesture.from = stroke_last_point_;
    gesture.point = point;
    stroke_last_point_ = point;
    return gesture;
}

AnnotationGesture AnnotationToolInteraction::release() {
    if (!painting_) {
        return {};
    }
    painting_ = false;
    stroke_last_point_ = {-1, -1};
    AnnotationGesture gesture;
    if (tool_ == AnnotationTool::Brush || tool_ == AnnotationTool::Eraser) {
        gesture.kind = AnnotationGesture::Kind::StrokeEnd;
    } else if (shape_start_.x() >= 0 && hover_point_.x() >= 0) {
        gesture.rect = QRect(shape_start_, hover_point_).normalized();
        if (tool_ == AnnotationTool::Rectangle) {
            gesture.kind = AnnotationGesture::Kind::Rectangle;
        } else if (tool_ == AnnotationTool::Ellipse) {
            gesture.kind = AnnotationGesture::Kind::Ellipse;
        } else if (tool_ == AnnotationTool::ThresholdRoi) {
            gesture.kind = AnnotationGesture::Kind::ThresholdRoi;
        }
    }
    shape_start_ = {-1, -1};
    return gesture;
}

AnnotationGesture AnnotationToolInteraction::finish_polygon() {
    AnnotationGesture gesture;
    if (polygon_points_.size() < 3) {
        return gesture;
    }
    gesture.kind = AnnotationGesture::Kind::Polygon;
    for (const QPoint& point : polygon_points_) {
        gesture.polygon << point;
    }
    polygon_points_.clear();
    return gesture;
}

void AnnotationToolInteraction::cancel_polygon() {
    polygon_points_.clear();
    hover_point_ = {-1, -1};
}

void SelectionTransformInteraction::reset() {
    dragging_ = false;
    active_handle_ = Handle::None;
    matrix_ = {};
    drag_matrix_ = {};
}

void SelectionTransformInteraction::begin_drag(Handle handle, const QPointF& point) {
    active_handle_ = handle;
    drag_start_ = point;
    drag_matrix_ = matrix_;
    dragging_ = true;
}

void SelectionTransformInteraction::end_drag() {
    dragging_ = false;
    active_handle_ = Handle::None;
}

QPolygonF SelectionTransformInteraction::frame(const QRectF& bounds) const {
    if (bounds.isEmpty()) {
        return {};
    }
    QPolygonF frame;
    for (const QPointF& point :
         {bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()}) {
        frame << matrix_.map(point);
    }
    return frame;
}

QPointF SelectionTransformInteraction::rotation_handle_position(
    const QPolygonF& widget_frame) {
    if (widget_frame.size() != 4) {
        return {};
    }
    const QPointF top_right = widget_frame[1];
    const QPointF edge = top_right - widget_frame[0];
    const qreal length = vector_length(edge);
    if (length <= 0.0) {
        return top_right;
    }
    const QPointF outward(edge.y() / length, -edge.x() / length);
    return top_right + outward * kRotationHandleDistance;
}

void SelectionTransformInteraction::paint_controls(
    QPainter& painter, const QPolygonF& widget_frame) {
    if (widget_frame.size() != 4) {
        return;
    }
    const QPointF rotation = rotation_handle_position(widget_frame);
    const QPointF top = (widget_frame[0] + widget_frame[1]) / 2.0;
    const QPointF right = (widget_frame[1] + widget_frame[2]) / 2.0;
    const QPointF bottom = (widget_frame[2] + widget_frame[3]) / 2.0;
    const QPointF left = (widget_frame[3] + widget_frame[0]) / 2.0;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor("#6BD0E0"), 1.5, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(widget_frame);
    painter.setPen(QPen(QColor("#6BD0E0"), 1.5));
    painter.drawLine(widget_frame[1], rotation);
    painter.setBrush(QColor("#F4C66A"));
    for (const QPointF& point : {widget_frame[0], widget_frame[1],
                                 widget_frame[2], widget_frame[3],
                                 top, right, bottom, left}) {
        painter.drawRect(QRectF(point.x() - kTransformHandleRadius,
                                point.y() - kTransformHandleRadius,
                                kTransformHandleRadius * 2.0,
                                kTransformHandleRadius * 2.0));
    }
    painter.setBrush(QColor("#6BD0E0"));
    painter.drawEllipse(rotation, kTransformHandleRadius + 1.0,
                        kTransformHandleRadius + 1.0);
    painter.restore();
}

SelectionTransformInteraction::Handle SelectionTransformInteraction::handle_at(
    const QPointF& widget_point, const QPolygonF& widget_frame, bool body_contains) {
    if (widget_frame.size() != 4) {
        return Handle::None;
    }
    const auto hit = [&widget_point](const QPointF& point, qreal radius) {
        return QLineF(widget_point, point).length() <= radius;
    };
    const QPointF top = (widget_frame[0] + widget_frame[1]) / 2.0;
    const QPointF right = (widget_frame[1] + widget_frame[2]) / 2.0;
    const QPointF bottom = (widget_frame[2] + widget_frame[3]) / 2.0;
    const QPointF left = (widget_frame[3] + widget_frame[0]) / 2.0;

    if (hit(rotation_handle_position(widget_frame), kTransformHandleRadius + 3.0)) {
        return Handle::Rotate;
    }
    if (hit(widget_frame[0], kTransformHandleRadius + 2.0)) {
        return Handle::TopLeft;
    }
    if (hit(widget_frame[1], kTransformHandleRadius + 2.0)) {
        return Handle::TopRight;
    }
    if (hit(widget_frame[2], kTransformHandleRadius + 2.0)) {
        return Handle::BottomRight;
    }
    if (hit(widget_frame[3], kTransformHandleRadius + 2.0)) {
        return Handle::BottomLeft;
    }
    if (hit(top, kTransformHandleRadius + 2.0)) {
        return Handle::Top;
    }
    if (hit(right, kTransformHandleRadius + 2.0)) {
        return Handle::Right;
    }
    if (hit(bottom, kTransformHandleRadius + 2.0)) {
        return Handle::Bottom;
    }
    if (hit(left, kTransformHandleRadius + 2.0)) {
        return Handle::Left;
    }
    return body_contains ? Handle::Body : Handle::None;
}

bool SelectionTransformInteraction::update_drag(const QRectF& bounds,
                                                const QPointF& point) {
    if (!dragging_ || bounds.isEmpty()) {
        return false;
    }
    const QPointF top_left = bounds.topLeft();
    const QPointF top_right = bounds.topRight();
    const QPointF bottom_right = bounds.bottomRight();
    const QPointF bottom_left = bounds.bottomLeft();
    const QPointF top_center = (top_left + top_right) / 2.0;
    const QPointF right_center = (top_right + bottom_right) / 2.0;
    const QPointF bottom_center = (bottom_left + bottom_right) / 2.0;
    const QPointF left_center = (top_left + bottom_left) / 2.0;
    const auto map = [this](const QPointF& source) {
        return drag_matrix_.map(source);
    };

    QTransform next = drag_matrix_;
    switch (active_handle_) {
    case Handle::Body: {
        QTransform translation;
        translation.translate(point.x() - drag_start_.x(),
                              point.y() - drag_start_.y());
        next = translation * drag_matrix_;
        break;
    }
    case Handle::TopLeft:
    case Handle::TopRight:
    case Handle::BottomRight:
    case Handle::BottomLeft: {
        QPointF handle;
        QPointF anchor;
        if (active_handle_ == Handle::TopLeft) {
            handle = top_left; anchor = bottom_right;
        } else if (active_handle_ == Handle::TopRight) {
            handle = top_right; anchor = bottom_left;
        } else if (active_handle_ == Handle::BottomRight) {
            handle = bottom_right; anchor = top_left;
        } else {
            handle = bottom_left; anchor = top_right;
        }
        const QPointF mapped_anchor = map(anchor);
        const qreal initial_distance = vector_length(map(handle) - mapped_anchor);
        if (initial_distance <= 0.0) {
            return false;
        }
        const qreal factor = std::clamp(
            vector_length(point - mapped_anchor) / initial_distance,
            kMinimumTransformScale, kMaximumTransformScale);
        next = transform_about(mapped_anchor, 0.0, factor, factor) *
            drag_matrix_;
        break;
    }
    case Handle::Top:
    case Handle::Right:
    case Handle::Bottom:
    case Handle::Left: {
        const bool horizontal = active_handle_ == Handle::Left ||
            active_handle_ == Handle::Right;
        QPointF handle;
        QPointF anchor;
        if (active_handle_ == Handle::Top) {
            handle = top_center; anchor = bottom_center;
        } else if (active_handle_ == Handle::Right) {
            handle = right_center; anchor = left_center;
        } else if (active_handle_ == Handle::Bottom) {
            handle = bottom_center; anchor = top_center;
        } else {
            handle = left_center; anchor = right_center;
        }
        const QPointF mapped_anchor = map(anchor);
        const QPointF axis = horizontal
            ? map(top_right) - map(top_left)
            : map(bottom_left) - map(top_left);
        const qreal axis_length = vector_length(axis);
        if (axis_length <= 0.0) {
            return false;
        }
        const QPointF unit_axis(axis.x() / axis_length, axis.y() / axis_length);
        const qreal denominator = dot_product(map(handle) - mapped_anchor, unit_axis);
        if (std::abs(denominator) <= 0.0001) {
            return false;
        }
        const qreal factor = std::clamp(
            dot_product(point - mapped_anchor, unit_axis) / denominator,
            kMinimumTransformScale, kMaximumTransformScale);
        const qreal axis_degrees = std::atan2(axis.y(), axis.x()) * kDegreesPerRadian;
        QTransform scale;
        scale.translate(mapped_anchor.x(), mapped_anchor.y());
        scale.rotate(axis_degrees);
        scale.scale(factor, 1.0);
        scale.rotate(-axis_degrees);
        scale.translate(-mapped_anchor.x(), -mapped_anchor.y());
        next = scale * drag_matrix_;
        break;
    }
    case Handle::Rotate: {
        const QPointF center = map(bounds.center());
        const QPointF start_vector = drag_start_ - center;
        const QPointF current_vector = point - center;
        if (vector_length(start_vector) <= 0.0 || vector_length(current_vector) <= 0.0) {
            return false;
        }
        const qreal start_degrees = std::atan2(start_vector.y(), start_vector.x()) *
            kDegreesPerRadian;
        const qreal current_degrees = std::atan2(current_vector.y(), current_vector.x()) *
            kDegreesPerRadian;
        next = transform_about(center,
                               normalized_angle_delta(current_degrees - start_degrees),
                               1.0, 1.0) * drag_matrix_;
        break;
    }
    case Handle::None:
        return false;
    }

    matrix_ = next;
    return true;
}

} // namespace studio
