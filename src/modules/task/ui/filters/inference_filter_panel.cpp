#include "modules/task/ui/filters/inference_filter_panel.h"

#include "common/ui/ui_metrics.h"

#include <common/config/ai_config.h>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>

namespace studio {

InferenceFilterPanel::InferenceFilterPanel(QWidget* parent) : QFrame(parent) {
    build_ui();
    connect_controls();
    hide();
}

void InferenceFilterPanel::build_ui() {
    setObjectName("inferenceFilterPanel");
    setProperty("sectionPanel", true);
    setMinimumWidth(0);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                               ThemeTokens::Space2, ThemeTokens::Space1);
    layout->setSpacing(ThemeTokens::Space1);

    QHBoxLayout* header = new QHBoxLayout;
    toggle_ = new QToolButton(this);
    toggle_->setObjectName("inferenceFilterToggle");
    toggle_->setText(tr("Filter"));
    toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle_->setArrowType(Qt::RightArrow);
    toggle_->setCheckable(true);
    toggle_->setProperty("panelCommand", true);
    header->addWidget(toggle_);
    header->addStretch();
    reset_ = new QPushButton(tr("Reset"), this);
    reset_->setObjectName("inferenceFilterReset");
    reset_->setToolTip(tr("Restore Core defaults in the editor."));
    header->addWidget(reset_);
    layout->addLayout(header);

    body_ = new QWidget(this);
    body_->setObjectName("inferenceFilterBody");
    fields_ = new QGridLayout(body_);
    fields_->setContentsMargins(0, ThemeTokens::Space1, 0, 0);
    fields_->setHorizontalSpacing(ThemeTokens::Space2);
    fields_->setVerticalSpacing(ThemeTokens::Space1);
    fields_->setColumnStretch(2, 1);

    error_ = new QLabel(body_);
    error_->setObjectName("inferenceFilterError");
    error_->setWordWrap(true);
    error_->setProperty("status", "error");
    error_->hide();
    fields_->addWidget(error_, 1, 0, 1, 3);
    apply_ = new QPushButton(tr("Apply"), body_);
    apply_->setObjectName("inferenceFilterApply");
    apply_->setProperty("primary", true);
    apply_->setToolTip(tr("Use these settings for the next inference."));
    QLabel* apply_hint = new QLabel(tr("Next inference"), body_);
    apply_hint->setProperty("meta", true);
    fields_->addWidget(apply_hint, 2, 0, 1, 2);
    fields_->addWidget(apply_, 2, 2, Qt::AlignRight);
    body_->hide();
    layout->addWidget(body_);
}

void InferenceFilterPanel::connect_controls() {
    connect(toggle_, &QToolButton::toggled, this, [this](bool expanded) {
        toggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        body_->setVisible(expanded);
    });
    connect(reset_, &QPushButton::clicked, this, [this] {
        if (content_) {
            content_->load(jocelin::config::AiConfig{});
            error_->hide();
        }
    });
    connect(apply_, &QPushButton::clicked, this, [this] {
        if (!content_ || !config_) {
            return;
        }
        QString error;
        if (!content_->apply_to(*config_, error)) {
            error_->setText(error);
            error_->show();
            return;
        }
        error_->hide();
        if (applied) {
            applied();
        }
    });
}

void InferenceFilterPanel::set_content(std::unique_ptr<InferenceFilterContent> content) {
    delete content_;
    content_ = nullptr;
    if (content) {
        fields_->addWidget(content.get(), 0, 0, 1, 3);
        // The layout reparents the widget, so Qt owns it from this point on.
        content_ = content.release();
        if (config_) {
            content_->load(*config_);
        }
    }
    error_->hide();
    setVisible(content_ != nullptr);
}

void InferenceFilterPanel::set_config(jocelin::config::AiConfig* config) {
    config_ = config;
    if (content_ && config_) {
        content_->load(*config_);
    }
    error_->hide();
}

} // namespace studio
