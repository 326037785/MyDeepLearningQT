#pragma once

#include "modules/task/ui/filters/inference_filter_panel.h"

class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace studio {

class OdInferenceFilterContent final : public InferenceFilterContent {
public:
    explicit OdInferenceFilterContent(QWidget* parent = nullptr);
    void load(const jocelin::config::AiConfig& config) override;
    bool apply_to(jocelin::config::AiConfig& config, QString& error) const override;
    void set_nms_available(bool available, float threshold);

private:
    QDoubleSpinBox* confidence_ = nullptr;
    QLabel* nms_label_ = nullptr;
    QDoubleSpinBox* nms_iou_ = nullptr;
    QSpinBox* max_detections_ = nullptr;
    bool nms_available_ = false;
};

} // namespace studio
