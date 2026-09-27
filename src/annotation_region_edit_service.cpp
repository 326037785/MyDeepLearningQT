#include "annotation_region_edit_service.h"

#include <QHash>
#include <QPainter>
#include <QSet>
#include <QTransform>
#include <QVector>

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

    std::vector<jocelin::geo::Run> runs;
    QHash<int, QVector<int>> row_runs;
    int seed_run = -1;
    for (int y = 0; y < mask.height(); ++y) {
        const uchar* row = mask.constScanLine(y);
        int x = 0;
        while (x < mask.width()) {
            while (x < mask.width() && row[x] != label) ++x;
            if (x >= mask.width()) break;
            const int start = x;
            while (x < mask.width() && row[x] == label) ++x;
            const int index = static_cast<int>(runs.size());
            runs.emplace_back(y, start, x, label);
            row_runs[y].append(index);
            if (y == seed.y() && seed.x() >= start && seed.x() < x)
                seed_run = index;
        }
    }
    if (seed_run < 0) return std::nullopt;

    QSet<int> selected;
    QVector<int> pending{seed_run};
    selected.insert(seed_run);
    while (!pending.isEmpty()) {
        const int current_index = pending.takeLast();
        const auto& current = runs[static_cast<size_t>(current_index)];
        for (const int neighbour_row : {current.row - 1, current.row + 1}) {
            for (int candidate_index : row_runs.value(neighbour_row)) {
                if (selected.contains(candidate_index)) continue;
                const auto& candidate = runs[static_cast<size_t>(candidate_index)];
                if (current.start >= candidate.end ||
                    candidate.start >= current.end)
                    continue;
                selected.insert(candidate_index);
                pending.append(candidate_index);
            }
        }
    }

    std::vector<jocelin::geo::Run> component;
    component.reserve(static_cast<size_t>(selected.size()));
    for (int index : selected)
        component.push_back(runs[static_cast<size_t>(index)]);
    return LabelRegionSelection{label, jocelin::geo::Region(std::move(component))};
}

QRectF AnnotationRegionEditService::region_bounds(
    const LabelRegionSelection& selection)
{
    const auto bounds = selection.region.bounding_box();
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
