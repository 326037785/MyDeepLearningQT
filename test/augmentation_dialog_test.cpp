#include "augmentation_dialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QTreeWidget>

#include <cmath>

namespace {

QTreeWidgetItem* find_operator(QTreeWidget* tree, const QString& id) {
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        auto* item = tree->topLevelItem(index);
        if (item->data(0, Qt::UserRole).toString() == id) return item;
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    jocelin::config::Augment original;
    if (std::abs(original.probability - 0.5f) > 1e-6f) return 1;
    original.probability = 0.25f;

    studio::AugmentationDialog dialog(
        jocelin::config::ToolType::Sg, original);
    auto* tree = dialog.findChild<QTreeWidget*>("augmentationOperatorCatalog");
    if (!tree) return 1;
    if (tree->topLevelItemCount() != 12 || tree->columnCount() != 1 ||
        !tree->isHeaderHidden() || tree->minimumWidth() < 400)
        return 2;

    auto* brightness = find_operator(tree, "brightness");
    auto* scale = find_operator(tree, "scale");
    auto* crop = find_operator(tree, "crop");
    auto* translate = find_operator(tree, "translate");
    if (!brightness || !scale || !crop || !translate) return 3;

    tree->setCurrentItem(crop);
    auto* crop_ratio = dialog.findChild<QDoubleSpinBox*>("crop_ratio");
    if (!crop_ratio || dialog.findChild<QDoubleSpinBox*>("crop_width") ||
        dialog.findChild<QDoubleSpinBox*>("crop_height"))
        return 12;
    crop_ratio->setValue(1000.0);
    if (crop_ratio->value() != 0.9 || dialog.value().crop_ratio != 0.9f)
        return 13;

    tree->setCurrentItem(translate);
    auto* translate_ratio = dialog.findChild<QDoubleSpinBox*>("translate_ratio");
    if (!translate_ratio ||
        dialog.findChild<QDoubleSpinBox*>("translate_x_ratio") ||
        dialog.findChild<QDoubleSpinBox*>("translate_y_ratio"))
        return 14;
    translate_ratio->setValue(0.25);
    if (translate_ratio->value() != 0.25 ||
        dialog.value().translate_ratio != 0.25f)
        return 15;

    brightness->setCheckState(0, Qt::Checked);
    tree->setCurrentItem(brightness);
    auto* delta = dialog.findChild<QDoubleSpinBox*>("brightness_delta");
    if (!delta) return 4;
    delta->setValue(1000.0);
    if (delta->value() != 1.0 || dialog.value().brightness_delta != 1.0f)
        return 5;

    auto* probability = dialog.findChild<QDoubleSpinBox*>("augmentationProbability");
    if (!probability) return 6;
    probability->setValue(2.0);
    if (probability->value() != 1.0 || dialog.value().probability != 1.0f)
        return 7;

    scale->setCheckState(0, Qt::Checked);
    if (!dialog.value().enable.brightness || !dialog.value().enable.scale)
        return 8;
    auto* apply = dialog.findChild<QPushButton*>("augmentationApply");
    if (!apply) return 9;
    apply->click();
    if (dialog.result() != QDialog::Accepted) return 10;

    studio::AugmentationDialog cancelled(
        jocelin::config::ToolType::Sg, original);
    cancelled.findChild<QDoubleSpinBox*>("augmentationProbability")->setValue(0.9);
    cancelled.reject();
    if (cancelled.result() != QDialog::Rejected || original.probability != 0.25f)
        return 11;
    return 0;
}
