#include "modules/task/ui/pages/dataset_split_view.h"

#include "common/ui/ui_metrics.h"
#include "modules/task/application/task_dataset_binding.h"
#include "modules/task/application/task_workspace_service.h"
#include "modules/task/presentation/sample_presentation.h"
#include "modules/task/ui/common/sample_browser.h"
#include "modules/task/ui/pages/task_page_ui.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace studio {

using task_page::annotation_label;
using task_page::metric_card;
using task_page::split_label;

DatasetSplitView::DatasetSplitView(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                             ThemeTokens::Space2, ThemeTokens::Space2);
    root->setSpacing(ThemeTokens::Space1);

    auto* metrics = new QGridLayout;
    metrics->setSpacing(ThemeTokens::Space2);
    metrics->addWidget(metric_card(tr("TRAIN"), train_value_, this), 0, 0);
    metrics->addWidget(metric_card(tr("VALIDATION"), validation_value_, this), 0, 1);
    metrics->addWidget(metric_card(tr("TEST"), test_value_, this), 0, 2);
    metrics->addWidget(metric_card(tr("UNASSIGNED"), unassigned_value_, this), 0, 3);
    for (int column = 0; column < 4; ++column) metrics->setColumnStretch(column, 1);
    root->addLayout(metrics);

    auto* generator = new QFrame(this);
    generator->setProperty("sectionPanel", true);
    auto* generator_layout = new QHBoxLayout(generator);
    generator_layout->setContentsMargins(ThemeTokens::Space3, ThemeTokens::Space2,
                                         ThemeTokens::Space3, ThemeTokens::Space2);
    train_percent_ = new QSpinBox(generator);
    train_percent_->setObjectName("splitTrainPercent");
    validation_percent_ = new QSpinBox(generator);
    validation_percent_->setObjectName("splitValidationPercent");
    test_percent_ = new QSpinBox(generator);
    test_percent_->setObjectName("splitTestPercent");
    seed_ = new QSpinBox(generator);
    seed_->setObjectName("splitRandomSeed");
    for (auto* value : {train_percent_, validation_percent_, test_percent_}) {
        value->setRange(0, 100);
        value->setSuffix("%");
    }
    train_percent_->setValue(80);
    validation_percent_->setValue(20);
    test_percent_->setValue(0);
    seed_->setRange(0, 999999999);
    seed_->setValue(42);
    auto* apply = new QPushButton(tr("Apply smart split"), generator);
    apply->setObjectName("applySmartSplitButton");
    apply->setProperty("primary", true);
    auto* reset = new QPushButton(tr("Reset manual overrides"), generator);
    reset->setObjectName("resetSplitOverridesButton");
    reset->setProperty("compact", true);
    generator_layout->addWidget(new QLabel(tr("Train"), generator));
    generator_layout->addWidget(train_percent_);
    generator_layout->addWidget(new QLabel(tr("Validation"), generator));
    generator_layout->addWidget(validation_percent_);
    generator_layout->addWidget(new QLabel(tr("Test"), generator));
    generator_layout->addWidget(test_percent_);
    generator_layout->addWidget(new QLabel(tr("Seed"), generator));
    generator_layout->addWidget(seed_);
    generator_layout->addStretch();
    generator_layout->addWidget(reset);
    generator_layout->addWidget(apply);
    root->addWidget(generator);

    warning_ = new QLabel(this);
    warning_->setObjectName("splitAnnotationWarning");
    warning_->setProperty("inlineWarning", true);
    warning_->setWordWrap(true);
    root->addWidget(warning_);

    auto* content = new QSplitter(Qt::Horizontal, this);
    content->setObjectName("splitContentSplitter");
    auto* thumbnail_panel = new QFrame(content);
    thumbnail_panel->setProperty("sectionPanel", true);
    auto* thumbnail_layout = new QVBoxLayout(thumbnail_panel);
    thumbnail_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                                         ThemeTokens::Space2, ThemeTokens::Space2);
    auto* thumbnail_title = new QLabel(tr("SAMPLES"), thumbnail_panel);
    thumbnail_title->setProperty("eyebrow", true);
    thumbnail_layout->addWidget(thumbnail_title);
    thumbnails_ = new SampleBrowser(thumbnail_panel);
    thumbnails_->setObjectName("splitThumbnailList");
    thumbnails_->setIconSize(QSize(72, 54));
    thumbnail_layout->addWidget(thumbnails_, 1);

    table_ = new QTableWidget(content);
    table_->setObjectName("taskSplitTable");
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        {tr("Sample"), tr("Images"), tr("Annotation"), tr("Assignment"), tr("Source")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    content->addWidget(thumbnail_panel);
    content->addWidget(table_);
    content->setCollapsible(0, false);
    content->setStretchFactor(0, 0);
    content->setStretchFactor(1, 1);
    content->setSizes({240, 900});
    root->addWidget(content, 1);
    connect(apply, &QPushButton::clicked, this, [this] { apply_random_split(); });
    connect(reset, &QPushButton::clicked, this, [this] {
        if (!service_) return;
        const auto settings = service_->binding().split_settings;
        service_->assign_reproducible_split(
            settings.train_percent, settings.validation_percent,
            settings.test_percent, settings.seed, true);
        refresh();
        if (changed) changed();
        if (status) status(tr("Manual split overrides were reset and the smart split was recalculated."));
    });
    thumbnails_->current_requested = [this](int row, const QString&) {
        if (row >= 0 && row < table_->rowCount()) table_->selectRow(row);
        return true;
    };
    connect(table_, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
        if (row >= 0 && row < thumbnails_->count())
            thumbnails_->set_current_source_index(row);
    });
    thumbnails_->split_requested = [this](const std::vector<std::string>& ids,
                                           const std::string& value) {
        if (!service_) return;
        for (const auto& id : ids) service_->set_sample_split(id, value);
        refresh();
        if (changed) changed();
    };
    UiMetrics::apply_semantic_metrics(this);
}

void DatasetSplitView::set_service(TaskWorkspaceService* service) {
    service_ = service;
    refresh();
}

void DatasetSplitView::refresh() {
    rebuilding_ = true;
    table_->setRowCount(0);
    std::vector<SampleBrowserEntry> browser_entries;
    if (!service_) {
        thumbnails_->set_entries(browser_entries);
        rebuilding_ = false;
        refresh_stats();
        return;
    }
    service_->refresh_quality();
    const auto split_settings = service_->binding().split_settings;
    train_percent_->setValue(split_settings.train_percent);
    validation_percent_->setValue(split_settings.validation_percent);
    test_percent_->setValue(split_settings.test_percent);
    seed_->setValue(static_cast<int>(split_settings.seed));
    const auto& samples = service_->binding().samples;
    int selected_rows = 0;
    int malformed = 0;
    for (const auto& sample : samples) {
        if (!sample.selected) continue;
        ++selected_rows;
        malformed += sample.quality_issues.empty() ? 0 : 1;
    }
    table_->setRowCount(selected_rows);
    int row = 0;
    for (int sample_index = 0;
         sample_index < static_cast<int>(samples.size());
         ++sample_index) {
        const auto& sample = samples[static_cast<size_t>(sample_index)];
        if (!sample.selected) continue;
        const QString display_name = image_display_name(sample_index, sample);
        const QString annotation_text = annotation_label(sample);
        const QString tooltip = sample_image_tooltip(
            display_name, sample, annotation_text);
        SampleBrowserEntry browser_entry;
        browser_entry.sample_id = sample.sample_id;
        browser_entry.image_path = sample.image_paths.empty()
            ? QString{}
            : QString::fromStdString(sample.image_paths.front());
        browser_entry.display_name = display_name;
        browser_entry.annotation_state = annotation_text;
        browser_entry.split_state = split_label(normalized_split(sample.split));
        browser_entry.tooltip = tooltip;
        browser_entry.source_index = row;
        browser_entries.push_back(std::move(browser_entry));
        auto* id = new QTableWidgetItem(display_name);
        id->setData(Qt::UserRole, QString::fromStdString(sample.sample_id));
        id->setToolTip(tooltip);
        auto* count = new QTableWidgetItem(QString::number(sample.image_paths.size()));
        count->setToolTip(tooltip);
        auto* annotation = new QTableWidgetItem(annotation_text);
        for (auto* item : {id, count, annotation})
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 0, id);
        table_->setItem(row, 1, count);
        table_->setItem(row, 2, annotation);
        auto* assignment = new QComboBox(table_);
        assignment->setProperty("compact", true);
        assignment->addItem(
            tr("Automatic · %1").arg(split_label(normalized_split(sample.split))),
            "auto");
        assignment->addItem(tr("Train"), "train");
        assignment->addItem(tr("Validation"), "validation");
        assignment->addItem(tr("Test"), "test");
        assignment->addItem(tr("Unassigned"), "unassigned");
        const int current = sample.split_manual
            ? assignment->findData(
                  QString::fromStdString(normalized_split(sample.split)))
            : 0;
        assignment->setCurrentIndex(std::max(0, current));
        const std::string sample_id = sample.sample_id;
        connect(assignment, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this, assignment, sample_id](int) {
            if (rebuilding_ || !service_) return;
            const auto selected = assignment->currentData().toString();
            if (selected == "auto")
                service_->restore_automatic_split(sample_id);
            else
                service_->set_sample_split(
                    sample_id, selected.toStdString());
            refresh_stats();
            if (changed) changed();
            QTimer::singleShot(0, this, [this] { refresh(); });
        });
        table_->setCellWidget(row, 3, assignment);
        auto* source = new QTableWidgetItem(
            sample.split_manual ? tr("Manual") : tr("Automatic"));
        source->setFlags(source->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 4, source);
        ++row;
    }
    thumbnails_->set_entries(browser_entries);
    warning_->setVisible(malformed > 0);
    warning_->setText(
        tr("%1 sample(s) have quality issues that must be fixed or deselected before training.")
            .arg(malformed));
    rebuilding_ = false;
    refresh_stats();
}

void DatasetSplitView::refresh_stats() {
    const auto stats = service_ ? service_->split_stats() : TaskSplitStats{};
    train_value_->setText(QString::number(stats.train));
    validation_value_->setText(
        stats.derived_validation > 0
            ? tr("%1 (+%2 derived/shared)")
                  .arg(stats.validation)
                  .arg(stats.derived_validation)
            : QString::number(stats.validation));
    validation_value_->setToolTip(
        stats.derived_validation > 0
            ? tr("No validation split was explicitly assigned. %1 training sample(s) will also be evaluated as a deterministic derived validation set.")
                  .arg(stats.shared_validation)
            : tr("Explicit validation assignments"));
    test_value_->setText(QString::number(stats.test));
    unassigned_value_->setText(QString::number(stats.unassigned));
}

void DatasetSplitView::apply_random_split() {
    if (!service_) return;
    try {
        service_->assign_reproducible_split(
            train_percent_->value(), validation_percent_->value(),
            test_percent_->value(), static_cast<std::uint32_t>(seed_->value()));
        refresh();
        if (changed) changed();
        if (status) status(
            tr("Stratified split applied with seed %1; manual overrides were preserved.")
                .arg(seed_->value()));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Cannot apply split"), QString::fromUtf8(error.what()));
    }
}

} // namespace studio
