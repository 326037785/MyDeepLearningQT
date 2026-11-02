#include "cls_inference_filter.h"

#include "ui_metrics.h"

#include <common/config/ai_config.h>

#include <QDoubleValidator>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QSignalBlocker>
#include <QSlider>

#include <limits>

namespace studio {

ClsInferenceFilterContent::ClsInferenceFilterContent(QWidget* parent)
    : InferenceFilterContent(parent) {
    setMinimumWidth(0);
    QGridLayout* fields = new QGridLayout(this);
    fields->setContentsMargins(0, 0, 0, 0);
    fields->setHorizontalSpacing(ThemeTokens::Space2);
    fields->setColumnStretch(2, 1);

    confidence_ = new QLineEdit(this);
    confidence_->setObjectName("clsFilterConfidence");
    confidence_->setMinimumWidth(0);
    confidence_->setMaximumWidth(90);
    QDoubleValidator* validator = new QDoubleValidator(0.0, 1.0, 1000, confidence_);
    validator->setLocale(QLocale::c());
    validator->setNotation(QDoubleValidator::ScientificNotation);
    confidence_->setValidator(validator);
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setObjectName("clsFilterConfidenceSlider");
    slider_->setRange(0, 1000);
    slider_->setMinimumWidth(40);
    fields->addWidget(new QLabel(tr("Confidence"), this), 0, 0);
    fields->addWidget(confidence_, 0, 1);
    fields->addWidget(slider_, 0, 2);

    connect(confidence_, &QLineEdit::textChanged, this, [this] {
        if (confidence_->hasAcceptableInput()) {
            const double value = QLocale::c().toDouble(confidence_->text());
            const QSignalBlocker blocker(slider_);
            slider_->setValue(qRound(value * 1000.0));
        }
    });
    connect(slider_, &QSlider::valueChanged, this, [this](int value) {
        confidence_->setText(QString::number(value / 1000.0, 'g', 4));
    });
    load(jocelin::config::AiConfig{});
}

void ClsInferenceFilterContent::load(const jocelin::config::AiConfig& config) {
    confidence_->setText(QString::number(config.cls_confidence_threshold, 'g',
                                         std::numeric_limits<double>::max_digits10));
}

bool ClsInferenceFilterContent::apply_to(jocelin::config::AiConfig& config, QString& error) const {
    if (!confidence_->hasAcceptableInput()) {
        error = tr("Enter a confidence between 0 and 1.");
        return false;
    }
    config.cls_confidence_threshold = QLocale::c().toDouble(confidence_->text());
    return true;
}

} // namespace studio
