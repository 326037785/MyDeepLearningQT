#pragma once

#include <common/geometry/run.h>

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QTransform>

#include <optional>

namespace studio {

struct LabelRegionSelection {
    int label = 0;
    jocelin::geo::Region region;
};

struct AnnotationRegionTransform {
    QPointF anchor;
    QPointF translation;
    double scale_x = 1.0;
    double scale_y = 1.0;
    double rotation_degrees = 0.0;
};

class AnnotationRegionEditService final {
public:
    static std::optional<LabelRegionSelection> select_connected_region(
        const QImage& mask, const QPoint& seed, int label);
    static QRectF region_bounds(const LabelRegionSelection& selection);
    static QTransform transform_matrix(const AnnotationRegionTransform& transform);
    static QImage transformed_mask(
        const QImage& source, const LabelRegionSelection& selection,
        const QTransform& transform);
};

} // namespace studio
