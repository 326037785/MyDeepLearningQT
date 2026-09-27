#pragma once

#include <common/config/ai_config.h>
#include <common/config/augment_catalog.h>

#include <QDialog>

class QDoubleSpinBox;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace studio {

class AugmentationDialog final : public QDialog {
public:
    AugmentationDialog(jocelin::config::ToolType task,
                       const jocelin::config::Augment& current,
                       QWidget* parent = nullptr);

    const jocelin::config::Augment& value() const { return draft_; }

private:
    void render_parameters(const std::string& operator_id);
    void apply();

    jocelin::config::ToolType task_;
    jocelin::config::Augment draft_;
    std::vector<jocelin::config::AugmentDescriptor> descriptors_;
    QDoubleSpinBox* probability_ = nullptr;
    QTreeWidget* operations_ = nullptr;
    QVBoxLayout* parameters_layout_ = nullptr;
};

} // namespace studio
