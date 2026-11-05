#pragma once

#include <common/model/model_list.h>

#include <QDialog>
#include <QCoreApplication>

namespace studio {

class WeightConversionPopup final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(studio::WeightConversionPopup)
public:
    WeightConversionPopup(const jocelin::model::ModelEntry& model,
                          const jocelin::model::ModelWeight& source,
                          QWidget* parent = nullptr);
};

} // namespace studio
