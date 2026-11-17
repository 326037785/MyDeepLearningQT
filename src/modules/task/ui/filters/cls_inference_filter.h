#pragma once

#include "modules/task/ui/filters/inference_filter_panel.h"

class QLineEdit;
class QSlider;

namespace studio {

class ClsInferenceFilterContent final : public InferenceFilterContent {
public:
    explicit ClsInferenceFilterContent(QWidget* parent = nullptr);
    void load(const jocelin::config::AiConfig& config) override;
    bool apply_to(jocelin::config::AiConfig& config, QString& error) const override;

private:
    QLineEdit* confidence_ = nullptr;
    QSlider* slider_ = nullptr;
};

} // namespace studio
