#pragma once

#include "modules/task/ui/results/result_inspection.h"
#include "modules/workflow/model/workflow.h"
#include <algorithm>

namespace studio {

inline QRectF detection_box_rect(const jocelin::meta::BboxComp& detection) {
    const auto& box = detection.box;
    return {box.cx() - box.w() / 2, box.cy() - box.h() / 2, box.w(), box.h()};
}

inline ResultPresentationItem present_detection(
    const jocelin::meta::BboxComp& detection,
    const std::vector<jocelin::model::ModelClass>& model_classes,
    const std::vector<AnnotationClass>& task_classes)
{
    const auto model_class = std::find_if(model_classes.begin(), model_classes.end(),
        [&detection](const auto& value) { return value.id == detection.class_id; });
    const auto task_class = std::find_if(task_classes.begin(), task_classes.end(),
        [&detection](const AnnotationClass& value) { return value.id == detection.class_id; });
    const QString name = QString::fromStdString(model_class == model_classes.end()
        ? detection.class_id : model_class->name);
    const QString confidence = detection.score ? QString::number(*detection.score, 'f', 2) : QString{};
    const QRectF outline = detection_box_rect(detection);
    const QColor color = task_class == task_classes.end()
        ? QColor("#458BF0") : QColor(QString::fromStdString(task_class->color));
    ResultPresentationItem item;
    item.text = confidence.isEmpty() ? name : name + QStringLiteral(" ") + confidence;
    item.outline = outline;
    item.color = color.isValid() ? color : QColor("#458BF0");
    item.hit_test = [outline](const QPointF& point) { return outline.contains(point); };
    item.attributes = {{QObject::tr("Class"), name}};
    if (detection.score) {
        item.attributes.append({QObject::tr("Confidence"), confidence});
    }
    item.attributes.append({QObject::tr("x1, y1, x2, y2 (px)"), QStringLiteral("%1, %2, %3, %4")
        .arg(outline.left(), 0, 'f', 1).arg(outline.top(), 0, 'f', 1)
        .arg(outline.right(), 0, 'f', 1).arg(outline.bottom(), 0, 'f', 1)});
    return item;
}

inline QVector<ResultPresentationItem> present_detections(
    const jocelin::result::od::OdResult& result,
    const std::vector<jocelin::model::ModelClass>& model_classes,
    const std::vector<AnnotationClass>& task_classes)
{
    QVector<ResultPresentationItem> items;
    for (const jocelin::meta::BboxComp& detection : result.detections) {
        items.push_back(present_detection(detection, model_classes, task_classes));
    }
    return items;
}

inline QString detection_result_summary(const jocelin::result::od::OdResult& result) {
    return QObject::tr("%1 detection(s)\nConfidence threshold: %2\nNMS IoU: %3\nMax detections: %4")
        .arg(result.detections.size()).arg(result.confidence_threshold, 0, 'f', 2)
        .arg(result.nms_iou_threshold, 0, 'f', 2).arg(result.max_detections);
}

} // namespace studio
