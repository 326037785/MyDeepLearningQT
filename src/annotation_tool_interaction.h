#pragma once

#include <QList>
#include <QPoint>
#include <QPointF>
#include <QPolygon>
#include <QPolygonF>
#include <QRect>
#include <QRectF>
#include <QTransform>

class QPainter;

namespace studio {

// Describes interaction only. The receiving task decides which target to edit.
enum class AnnotationTool {
    Pan, TransformSelection, Brush, Polygon, Rectangle, Ellipse, ThresholdRoi,
    Eraser, Fill, Pick
};

struct AnnotationGesture {
    enum class Kind {
        None, Point, StrokeStart, StrokeSegment, StrokeEnd,
        Rectangle, Ellipse, ThresholdRoi, Polygon
    };

    Kind kind = Kind::None;
    QPoint point;
    QPoint from;
    QRect rect;
    QPolygon polygon;
};

class AnnotationToolInteraction final {
public:
    void set_tool(AnnotationTool tool);
    AnnotationTool tool() const { return tool_; }
    void set_brush_size(int size) { brush_size_ = size; }
    int brush_size() const { return brush_size_; }
    void reset();

    AnnotationGesture press(const QPoint& point);
    AnnotationGesture move(const QPoint& point);
    AnnotationGesture release();
    AnnotationGesture finish_polygon();
    void cancel_polygon();

    bool painting() const { return painting_; }
    bool polygon_active() const { return !polygon_points_.isEmpty(); }
    const QList<QPoint>& polygon_points() const { return polygon_points_; }
    const QPoint& shape_start() const { return shape_start_; }
    const QPoint& hover_point() const { return hover_point_; }

private:
    AnnotationTool tool_ = AnnotationTool::Brush;
    int brush_size_ = 18;
    bool painting_ = false;
    QPoint stroke_last_point_{-1, -1};
    QPoint shape_start_{-1, -1};
    QPoint hover_point_{-1, -1};
    QList<QPoint> polygon_points_;
};

class ImagePanInteraction final {
public:
    void begin(const QPointF& point) { active_ = true; last_ = point; }
    QPointF move(const QPointF& point) {
        const QPointF delta = point - last_;
        last_ = point;
        return delta;
    }
    void end() { active_ = false; }
    bool active() const { return active_; }

private:
    bool active_ = false;
    QPointF last_;
};

class SelectionTransformInteraction final {
public:
    enum class Handle {
        None, Body, TopLeft, TopRight, BottomRight, BottomLeft,
        Top, Right, Bottom, Left, Rotate
    };

    void reset();
    void begin_drag(Handle handle, const QPointF& point);
    bool update_drag(const QRectF& bounds, const QPointF& point);
    void end_drag();
    bool dragging() const { return dragging_; }
    const QTransform& matrix() const { return matrix_; }
    QPolygonF frame(const QRectF& bounds) const;
    static QPointF rotation_handle_position(const QPolygonF& widget_frame);
    static void paint_controls(QPainter& painter, const QPolygonF& widget_frame);
    static Handle handle_at(const QPointF& widget_point,
                            const QPolygonF& widget_frame, bool body_contains);

private:
    bool dragging_ = false;
    Handle active_handle_ = Handle::None;
    QPointF drag_start_;
    QTransform matrix_;
    QTransform drag_matrix_;
};

} // namespace studio
