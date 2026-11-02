#pragma once

#include "inference_filter_panel.h"

class QCheckBox;
class QGridLayout;
class QLineEdit;
class QSlider;
class QSpinBox;

namespace studio {

class SgInferenceFilterContent final : public InferenceFilterContent {
public:
    explicit SgInferenceFilterContent(QWidget* parent = nullptr);
    void load(const jocelin::config::AiConfig& config) override;
    bool apply_to(jocelin::config::AiConfig& config, QString& error) const override;

private:
    void build_ui();
    void build_confidence_controls(QGridLayout* fields);
    void build_region_controls(QGridLayout* fields);
    void build_morphology_controls(QGridLayout* fields);
    void connect_controls();

    QLineEdit* pixel_confidence_ = nullptr;
    QLineEdit* region_confidence_ = nullptr;
    QSlider* pixel_slider_ = nullptr;
    QSlider* region_slider_ = nullptr;
    QSpinBox* min_area_ = nullptr;
    QLineEdit* max_area_ = nullptr;
    QLineEdit* min_aspect_ = nullptr;
    QLineEdit* max_aspect_ = nullptr;
    QCheckBox* morphology_ = nullptr;
    QSpinBox* iterations_ = nullptr;
};

} // namespace studio
