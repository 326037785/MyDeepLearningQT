#include "annotation_region_edit_service.h"

#include <QPainter>
#include <QTransform>

#include <algorithm>
#include <vector>

namespace studio {

QTransform AnnotationRegionEditService::transform_matrix(
    const AnnotationRegionTransform& transform)
{
    QTransform result;
    result.translate(transform.translation.x(), transform.translation.y());
    result.translate(transform.anchor.x(), transform.anchor.y());
    result.rotate(transform.rotation_degrees);
    result.scale(std::max(0.01, transform.scale_x),
                 std::max(0.01, transform.scale_y));
    result.translate(-transform.anchor.x(), -transform.anchor.y());
    return result;
}

std::optional<LabelRegionSelection>
AnnotationRegionEditService::select_connected_region(
    const QImage& source, const QPoint& seed, int label)
{
    if (source.isNull() || label <= 0 || !source.rect().contains(seed))
        return std::nullopt;
    const QImage mask = source.convertToFormat(QImage::Format_Grayscale8);
    if (mask.constScanLine(seed.y())[seed.x()] != label)
        return std::nullopt;

    auto component = jocelin::geo::image_connected_component_simd_omp({mask.constBits(),
        mask.height(), mask.width(), static_cast<size_t>(mask.bytesPerLine())}, seed.y(), seed.x());
    if (component.empty()) return std::nullopt;
    return LabelRegionSelection{label, std::move(component)};
}

QRectF AnnotationRegionEditService::region_bounds(
    const LabelRegionSelection& selection)
{
    const auto bounds = selection.region.bounding_box_omp();
    return QRectF(bounds.min_col, bounds.min_row,
                  bounds.max_col - bounds.min_col,
                  bounds.max_row - bounds.min_row);
}

QImage AnnotationRegionEditService::transformed_mask(
    const QImage& source, const LabelRegionSelection& selection,
    const QTransform& transform)
{
    QImage result = source.convertToFormat(QImage::Format_Grayscale8);
    if (result.isNull() || selection.label <= 0) return result;

    const QRectF bounds = region_bounds(selection);
    if (bounds.isEmpty()) return result;
    const QPoint origin = bounds.topLeft().toPoint();
    QImage component(bounds.size().toSize(), QImage::Format_ARGB32_Premultiplied);
    component.fill(Qt::transparent);
    for (const auto& run : selection.region.vRun) {
        if (run.row < 0 || run.row >= result.height()) continue;
        const int start = std::clamp(run.start, 0, result.width());
        const int end = std::clamp(run.end, 0, result.width());
        const int component_row_index = run.row - origin.y();
        if (component_row_index < 0 || component_row_index >= component.height()) continue;
        const int component_start = std::clamp(
            start - origin.x(), 0, component.width());
        const int component_end = std::clamp(
            end - origin.x(), 0, component.width());
        auto* component_row = reinterpret_cast<QRgb*>(
            component.scanLine(component_row_index));
        std::fill(component_row + component_start, component_row + component_end,
                  qRgba(255, 255, 255, 255));
        uchar* result_row = result.scanLine(run.row);
        std::fill(result_row + start, result_row + end, uchar{0});
    }

    QImage transformed(result.size(), QImage::Format_ARGB32_Premultiplied);
    transformed.fill(Qt::transparent);
    {
        QPainter painter(&transformed);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setWorldTransform(transform);
        painter.drawImage(origin, component);
    }
    for (int y = 0; y < result.height(); ++y) {
        const auto* transformed_row =
            reinterpret_cast<const QRgb*>(transformed.constScanLine(y));
        uchar* result_row = result.scanLine(y);
        for (int x = 0; x < result.width(); ++x) {
            if (qAlpha(transformed_row[x]) != 0)
                result_row[x] = static_cast<uchar>(selection.label);
        }
    }
    return result;
}

} // namespace studio
