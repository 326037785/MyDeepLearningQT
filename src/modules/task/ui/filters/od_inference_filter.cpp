#include "modules/task/ui/filters/od_inference_filter.h"

#include "common/ui/ui_metrics.h"

#include <common/config/ai_config.h>

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>

#include <limits>

namespace studio {

OdInferenceFilterContent::OdInferenceFilterContent(QWidget* parent)
    : InferenceFilterContent(parent) {
    QFormLayout* fields = new QFormLayout(this);
    fields->setContentsMargins(0, 0, 0, 0);
    fields->setHorizontalSpacing(ThemeTokens::Space2);
    confidence_ = new QDoubleSpinBox(this);
    confidence_->setObjectName("odFilterConfidence");
    confidence_->setRange(0.0, 1.0);
    confidence_->setDecimals(6);
    confidence_->setSingleStep(0.01);
    fields->addRow(tr("Confidence"), confidence_);

    nms_label_ = new QLabel(tr("NMS IoU"), this);
    nms_iou_ = new QDoubleSpinBox(this);
    nms_iou_->setObjectName("odFilterNmsIou");
    nms_iou_->setRange(0.0, 1.0);
    nms_iou_->setDecimals(6);
    nms_iou_->setSingleStep(0.01);
    fields->addRow(nms_label_, nms_iou_);

    max_detections_ = new QSpinBox(this);
    max_detections_->setObjectName("odFilterMaxDetections");
    max_detections_->setRange(1, std::numeric_limits<int>::max());
    fields->addRow(tr("Maximum detections"), max_detections_);
    nms_label_->hide();
    nms_iou_->hide();
    load(jocelin::config::AiConfig{});
}

void OdInferenceFilterContent::load(const jocelin::config::AiConfig& config) {
    confidence_->setValue(config.od_confidence_threshold);
    nms_iou_->setValue(config.od_nms_iou_threshold);
    max_detections_->setValue(config.od_max_detections);
}

bool OdInferenceFilterContent::apply_to(jocelin::config::AiConfig& config, QString&) const {
    config.od_confidence_threshold = static_cast<float>(confidence_->value());
    if (nms_available_) {
        config.od_nms_iou_threshold = static_cast<float>(nms_iou_->value());
    }
    config.od_max_detections = max_detections_->value();
    return true;
}

void OdInferenceFilterContent::set_nms_available(bool available, float threshold) {
    if (available && !nms_available_) {
        nms_iou_->setValue(threshold);
    }
    nms_available_ = available;
    nms_label_->setVisible(available);
    nms_iou_->setVisible(available);
}

} // namespace studio
