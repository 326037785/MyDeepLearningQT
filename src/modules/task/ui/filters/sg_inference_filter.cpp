#include "modules/task/ui/filters/sg_inference_filter.h"

#include "common/ui/ui_metrics.h"

#include <common/config/ai_config.h>

#include <QCheckBox>
#include <QDoubleValidator>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>

#include <limits>
#include <utility>

namespace studio {

SgInferenceFilterContent::SgInferenceFilterContent(QWidget* parent)
    : InferenceFilterContent(parent) {
    build_ui();
    connect_controls();
    load(jocelin::config::AiConfig{});
}

void SgInferenceFilterContent::build_ui() {
    setMinimumWidth(0);
    QGridLayout* fields = new QGridLayout(this);
    fields->setContentsMargins(0, 0, 0, 0);
    fields->setHorizontalSpacing(ThemeTokens::Space2);
    fields->setVerticalSpacing(ThemeTokens::Space1);
    fields->setColumnStretch(2, 1);
    build_confidence_controls(fields);
    build_region_controls(fields);
    build_morphology_controls(fields);
}

void SgInferenceFilterContent::build_confidence_controls(QGridLayout* fields) {
    pixel_confidence_ = new QLineEdit(this);
    pixel_confidence_->setObjectName("sgFilterPixelConfidence");
    region_confidence_ = new QLineEdit(this);
    region_confidence_->setObjectName("sgFilterRegionConfidence");
    pixel_slider_ = new QSlider(Qt::Horizontal, this);
    pixel_slider_->setObjectName("sgFilterPixelSlider");
    region_slider_ = new QSlider(Qt::Horizontal, this);
    region_slider_->setObjectName("sgFilterRegionSlider");
    for (QLineEdit* edit : {pixel_confidence_, region_confidence_}) {
        QDoubleValidator* validator = new QDoubleValidator(0.0, 1.0, 1000, edit);
        validator->setLocale(QLocale::c());
        validator->setNotation(QDoubleValidator::ScientificNotation);
        edit->setValidator(validator);
        edit->setMinimumWidth(0);
        edit->setMaximumWidth(90);
    }
    for (QSlider* slider : {pixel_slider_, region_slider_}) {
        slider->setRange(0, 1000);
        slider->setMinimumWidth(40);
    }
    fields->addWidget(new QLabel(tr("Pixel confidence"), this), 0, 0);
    fields->addWidget(pixel_confidence_, 0, 1);
    fields->addWidget(pixel_slider_, 0, 2);
    fields->addWidget(new QLabel(tr("Region confidence"), this), 1, 0);
    fields->addWidget(region_confidence_, 1, 1);
    fields->addWidget(region_slider_, 1, 2);
}

void SgInferenceFilterContent::build_region_controls(QGridLayout* fields) {
    QWidget* areas = new QWidget(this);
    QHBoxLayout* area_layout = new QHBoxLayout(areas);
    area_layout->setContentsMargins(0, 0, 0, 0);
    area_layout->setSpacing(ThemeTokens::Space1);
    min_area_ = new QSpinBox(areas);
    min_area_->setObjectName("sgFilterMinArea");
    min_area_->setRange(0, std::numeric_limits<int>::max());
    min_area_->setMinimumWidth(0);
    min_area_->setToolTip(tr("Minimum region area in pixels."));
    max_area_ = new QLineEdit(areas);
    max_area_->setObjectName("sgFilterMaxArea");
    max_area_->setMinimumWidth(0);
    max_area_->setPlaceholderText(tr("No limit"));
    max_area_->setToolTip(tr("Maximum region area in pixels; empty uses the Core maximum."));
    max_area_->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{0,19}"), max_area_));
    area_layout->addWidget(min_area_, 1);
    area_layout->addWidget(new QLabel(QStringLiteral("–"), areas));
    area_layout->addWidget(max_area_, 1);
    fields->addWidget(new QLabel(tr("Region area (px)"), this), 2, 0);
    fields->addWidget(areas, 2, 1, 1, 2);

    QWidget* aspects = new QWidget(this);
    QHBoxLayout* aspect_layout = new QHBoxLayout(aspects);
    aspect_layout->setContentsMargins(0, 0, 0, 0);
    aspect_layout->setSpacing(ThemeTokens::Space1);
    min_aspect_ = new QLineEdit(aspects);
    min_aspect_->setObjectName("sgFilterMinAspectRatio");
    max_aspect_ = new QLineEdit(aspects);
    max_aspect_->setObjectName("sgFilterMaxAspectRatio");
    max_aspect_->setPlaceholderText(tr("No limit"));
    max_aspect_->setToolTip(tr("Empty uses the Core maximum."));
    for (QLineEdit* edit : {min_aspect_, max_aspect_}) {
        QDoubleValidator* validator = new QDoubleValidator(0.0, std::numeric_limits<double>::max(), 1000, edit);
        validator->setLocale(QLocale::c());
        validator->setNotation(QDoubleValidator::ScientificNotation);
        edit->setValidator(validator);
        edit->setMinimumWidth(0);
    }
    aspect_layout->addWidget(min_aspect_, 1);
    aspect_layout->addWidget(new QLabel(QStringLiteral("–"), aspects));
    aspect_layout->addWidget(max_aspect_, 1);
    fields->addWidget(new QLabel(tr("Aspect ratio (W / H)"), this), 3, 0);
    fields->addWidget(aspects, 3, 1, 1, 2);
}

void SgInferenceFilterContent::build_morphology_controls(QGridLayout* fields) {
    morphology_ = new QCheckBox(this);
    morphology_->setObjectName("sgFilterMorphology");
    iterations_ = new QSpinBox(this);
    iterations_->setObjectName("sgFilterIterations");
    iterations_->setRange(1, std::numeric_limits<int>::max());
    iterations_->setMinimumWidth(0);
    QWidget* iteration_row = new QWidget(this);
    QHBoxLayout* iteration_layout = new QHBoxLayout(iteration_row);
    iteration_layout->setContentsMargins(0, 0, 0, 0);
    iteration_layout->setSpacing(ThemeTokens::Space1);
    iteration_layout->addWidget(new QLabel(tr("Iterations"), iteration_row));
    iteration_layout->addWidget(iterations_, 1);
    fields->addWidget(new QLabel(tr("Morphology"), this), 4, 0);
    fields->addWidget(morphology_, 4, 1);
    fields->addWidget(iteration_row, 4, 2);
}

void SgInferenceFilterContent::connect_controls() {
    connect(morphology_, &QCheckBox::toggled, iterations_, &QSpinBox::setEnabled);
    for (const std::pair<QLineEdit*, QSlider*>& pair : {
             std::pair<QLineEdit*, QSlider*>{pixel_confidence_, pixel_slider_},
             std::pair<QLineEdit*, QSlider*>{region_confidence_, region_slider_}}) {
        QLineEdit* edit = pair.first;
        QSlider* slider = pair.second;
        connect(edit, &QLineEdit::textChanged, this, [edit, slider] {
            if (edit->hasAcceptableInput()) {
                const double value = QLocale::c().toDouble(edit->text());
                const QSignalBlocker blocker(slider);
                slider->setValue(qRound(value * 1000.0));
            }
        });
        connect(slider, &QSlider::valueChanged, this, [edit](int value) {
            edit->setText(QString::number(value / 1000.0, 'g', 4));
        });
    }
}

void SgInferenceFilterContent::load(const jocelin::config::AiConfig& config) {
    pixel_confidence_->setText(QString::number(config.confidence_threshold, 'g', std::numeric_limits<float>::max_digits10));
    region_confidence_->setText(QString::number(config.min_component_confidence, 'g', std::numeric_limits<double>::max_digits10));
    min_area_->setValue(config.min_region_area);
    max_area_->setText(config.max_region_area == std::numeric_limits<int64_t>::max()
        ? QString{} : QString::number(static_cast<qlonglong>(config.max_region_area)));
    min_aspect_->setText(QString::number(config.aspect_ratio_range.low, 'g', std::numeric_limits<double>::max_digits10));
    max_aspect_->setText(config.aspect_ratio_range.high == std::numeric_limits<double>::max()
        ? QString{} : QString::number(config.aspect_ratio_range.high, 'g', std::numeric_limits<double>::max_digits10));
    morphology_->setChecked(config.morphological_refine);
    iterations_->setValue(config.morph_iterations);
    iterations_->setEnabled(config.morphological_refine);
}

bool SgInferenceFilterContent::apply_to(jocelin::config::AiConfig& config, QString& error) const {
    const QString invalid = tr("Enter valid confidence values and nonnegative, ordered area and aspect ratio bounds.");
    if (!pixel_confidence_->hasAcceptableInput() || !region_confidence_->hasAcceptableInput() ||
        !min_aspect_->hasAcceptableInput() || !max_area_->hasAcceptableInput() ||
        (!max_aspect_->text().isEmpty() && !max_aspect_->hasAcceptableInput())) {
        error = invalid;
        return false;
    }
    bool max_area_valid = true;
    const qlonglong max_area = max_area_->text().isEmpty()
        ? std::numeric_limits<int64_t>::max() : max_area_->text().toLongLong(&max_area_valid);
    const double min_aspect = QLocale::c().toDouble(min_aspect_->text());
    const double max_aspect = max_aspect_->text().isEmpty()
        ? std::numeric_limits<double>::max() : QLocale::c().toDouble(max_aspect_->text());
    if (!max_area_valid || max_area < min_area_->value() || min_aspect > max_aspect) {
        error = invalid;
        return false;
    }
    config.confidence_threshold = static_cast<float>(QLocale::c().toDouble(pixel_confidence_->text()));
    config.min_component_confidence = QLocale::c().toDouble(region_confidence_->text());
    config.min_region_area = min_area_->value();
    config.max_region_area = max_area;
    config.aspect_ratio_range = {min_aspect, max_aspect};
    config.morphological_refine = morphology_->isChecked();
    config.morph_iterations = iterations_->value();
    return true;
}

} // namespace studio
