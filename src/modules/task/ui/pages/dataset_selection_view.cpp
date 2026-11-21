#include "modules/task/ui/pages/dataset_selection_view.h"

#include "common/ui/ui_metrics.h"
#include "modules/task/application/task_dataset_binding.h"
#include "modules/task/application/task_workspace_service.h"
#include "modules/task/integration/exchange/annotation_exchange.h"
#include "modules/task/presentation/sample_presentation.h"
#include "modules/task/ui/pages/task_page_ui.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace studio {

using task_page::helper_label;
using task_page::metric_card;

DatasetSelectionView::DatasetSelectionView(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                             ThemeTokens::Space2, ThemeTokens::Space2);
    root->setSpacing(ThemeTokens::Space1);

    auto* exchange = new QFrame(this);
    exchange->setObjectName("taskAnnotationExchange");
    exchange->setProperty("sectionPanel", true);
    auto* exchange_layout = new QHBoxLayout(exchange);
    exchange_layout->setContentsMargins(
        ThemeTokens::Space2, ThemeTokens::Space1,
        ThemeTokens::Space2, ThemeTokens::Space1);
    exchange_layout->setSpacing(ThemeTokens::Space1);

    auto* open_dataset = new QPushButton(tr("Dataset…"), exchange);
    open_dataset->setObjectName("taskOpenDatasetButton");
    open_dataset->setProperty("compact", true);
    open_dataset->setToolTip(tr("Open the connected source Dataset"));
    annotation_exchange_ = new QWidget(exchange);
    QHBoxLayout* annotation_layout = new QHBoxLayout(annotation_exchange_);
    annotation_layout->setContentsMargins(ThemeTokens::Space2, 0, 0, 0);
    annotation_layout->setSpacing(ThemeTokens::Space1);
    QLabel* exchange_label = new QLabel(tr("Annotations"), annotation_exchange_);
    exchange_label->setProperty("eyebrow", true);
    QPushButton* import_annotations = new QPushButton(tr("Import…"), annotation_exchange_);
    import_annotations->setObjectName("taskAnnotationImportButton");
    import_annotations->setProperty("compact", true);
    import_annotations->setToolTip(
        tr("Import a Studio bundle or an images/masks directory."));
    QComboBox* export_format = new QComboBox(annotation_exchange_);
    export_format->setObjectName("taskAnnotationExportFormat");
    export_format->setToolTip(tr("Annotation export format"));
    export_format->addItem(
        tr("Studio bundle"),
        static_cast<int>(AnnotationExchangeFormat::StudioNative));
    QPushButton* export_annotations = new QPushButton(tr("Export…"), annotation_exchange_);
    export_annotations->setObjectName("taskAnnotationExportButton");
    export_annotations->setProperty("compact", true);
    exchange_layout->addWidget(open_dataset);
    annotation_layout->addWidget(exchange_label);
    annotation_layout->addWidget(import_annotations);
    annotation_layout->addWidget(export_format);
    annotation_layout->addWidget(export_annotations);
    exchange_layout->addWidget(annotation_exchange_);
    exchange_layout->addStretch();
    root->addWidget(exchange);

    auto* metrics = new QGridLayout;
    metrics->setSpacing(ThemeTokens::Space2);
    metrics->addWidget(metric_card(tr("SAMPLES"), sample_value_, this), 0, 0);
    metrics->addWidget(metric_card(tr("IMAGES"), image_value_, this), 0, 1);
    metrics->addWidget(metric_card(tr("SELECTED"), selected_value_, this), 0, 2);
    metrics->addWidget(metric_card(tr("NEEDS REVIEW"), quality_value_, this), 0, 3);
    for (int column = 0; column < 4; ++column) metrics->setColumnStretch(column, 1);
    root->addLayout(metrics);

    issue_banner_ = helper_label({}, this);
    issue_banner_->setObjectName("taskDataIssueBanner");
    issue_banner_->setProperty("inlineWarning", true);
    issue_banner_->hide();
    root->addWidget(issue_banner_);

    auto* filters = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setObjectName("taskDataSearch");
    search_->setPlaceholderText(tr("Search sample id, file, split, or issue"));
    filter_ = new QComboBox(this);
    filter_->setObjectName("taskDataFilter");
    filter_->addItem(tr("All samples"), "all");
    filter_->addItem(tr("Selected"), "selected");
    filter_->addItem(tr("Annotated"), "annotated");
    filter_->addItem(tr("Needs review"), "issues");
    filters->addWidget(search_, 1);
    filters->addWidget(filter_);
    root->addLayout(filters);

    table_ = new QTableWidget(this);
    table_->setObjectName("taskDataTable");
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels(
        {tr("Use"), tr("Sample"), tr("Images"), tr("Split"), tr("Annotation"), tr("Issues")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    root->addWidget(table_, 1);

    connect(search_, &QLineEdit::textChanged, this, [this] { apply_filter(); });
    connect(filter_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { apply_filter(); });
    connect(open_dataset, &QPushButton::clicked, this, [this] {
        if (service_ && open_dataset_requested)
            open_dataset_requested(service_->source_node_id());
    });
    connect(
        import_annotations, &QPushButton::clicked,
        this, [this] {
            if (!service_) return;
            const QString source = QFileDialog::getExistingDirectory(
                this, tr("Select annotation package"));
            if (source.isEmpty()) return;
            try {
                auto imported =
                    AnnotationExchange::import_auto(source);
                const auto stats =
                    AnnotationExchange::import_stats(imported);
                if (!dataset_replace_requested)
                    throw std::runtime_error("Dataset replacement handler unavailable");
                if (!dataset_replace_requested(service_->source_node_id(), std::move(imported.dataset),
                                               service_->node_id(), &imported.binding)) return;
                refresh();
                if (changed) changed();
                QString summary =
                    tr("%1 images · %2 masks · %3 unannotated · %4 classes")
                        .arg(stats.image_count)
                        .arg(stats.mask_count)
                        .arg(stats.unannotated_count)
                        .arg(stats.class_count);
                if (stats.review_count > 0)
                    summary += tr("\n%1 item(s) need review.")
                                   .arg(stats.review_count);
                QMessageBox::information(
                    this,
                    tr("Annotations imported"),
                    summary);
            } catch (const std::exception& error) {
                QMessageBox::critical(
                    this,
                    tr("Cannot import annotations"),
                    QString::fromUtf8(error.what()));
            }
        });
    connect(
        export_annotations, &QPushButton::clicked,
        this, [this, export_format] {
            if (!service_) return;
            const QString destination =
                QFileDialog::getExistingDirectory(
                    this,
                    tr("Select annotation export destination"));
            if (destination.isEmpty()) return;
            try {
                const auto format =
                    static_cast<AnnotationExchangeFormat>(
                        export_format->currentData().toInt());
                const QString manifest =
                    AnnotationExchange::export_task(
                        service_->binding(),
                        destination,
                        format);
                QMessageBox::information(
                    this,
                    tr("Annotations exported"),
                    tr("%1 written to %2")
                        .arg(
                            AnnotationExchange::format_name(format),
                            manifest));
            } catch (const std::exception& error) {
                QMessageBox::critical(
                    this,
                    tr("Cannot export annotations"),
                    QString::fromUtf8(error.what()));
            }
        });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (rebuilding_ || !service_ || !item || item->column() != 0) return;
        const QString sample_id = table_->item(item->row(), 1)->data(Qt::UserRole).toString();
        try {
            service_->set_sample_selected(
                sample_id.toStdString(), item->checkState() == Qt::Checked);
            if (changed) changed();
            refresh();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Selection failed"), QString::fromUtf8(error.what()));
        }
    });
    UiMetrics::apply_semantic_metrics(this);
}

void DatasetSelectionView::set_service(TaskWorkspaceService* service) {
    service_ = service;
    annotation_exchange_->setVisible(!service_ || service_->task().task != "od");
    refresh();
}

void DatasetSelectionView::refresh() {
    rebuilding_ = true;
    table_->setRowCount(0);
    if (!service_) {
        sample_value_->setText("0");
        image_value_->setText("0");
        selected_value_->setText("0");
        quality_value_->setText("0");
        rebuilding_ = false;
        return;
    }
    try {
        service_->refresh_quality();
        const auto stats = service_->dataset_stats();
        sample_value_->setText(QString::number(stats.samples));
        image_value_->setText(QString::number(stats.images));
        selected_value_->setText(tr("%1 / %2").arg(stats.selected).arg(stats.samples));
        quality_value_->setText(QString::number(stats.quality_issues));
        issue_banner_->setVisible(stats.invalid > 0 || stats.quality_issues > 0);
        issue_banner_->setText(tr("%1 invalid sample(s), %2 sample(s) need annotation review. "
                                  "Bucket capacity is %3 image(s) per sample.")
                                   .arg(stats.invalid).arg(stats.quality_issues)
                                   .arg(stats.bucket_capacity));
        const auto& samples = service_->binding().samples;
        table_->setRowCount(static_cast<int>(samples.size()));
        for (int row = 0; row < static_cast<int>(samples.size()); ++row) {
            const auto& sample = samples[static_cast<size_t>(row)];
            const QString display_name = image_display_name(row, sample);
            const QString annotation_text = annotation_label(sample);
            const QString tooltip = sample_image_tooltip(
                display_name, sample, annotation_text);
            auto* use = new QTableWidgetItem;
            use->setFlags((use->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
            use->setCheckState(sample.selected ? Qt::Checked : Qt::Unchecked);
            auto* id = new QTableWidgetItem(display_name);
            id->setData(Qt::UserRole, QString::fromStdString(sample.sample_id));
            id->setToolTip(tooltip);
            auto* images = new QTableWidgetItem(QString::number(sample.image_paths.size()));
            images->setToolTip(tooltip);
            auto* split = new QTableWidgetItem(split_label(normalized_split(sample.split)));
            auto* annotation = new QTableWidgetItem(annotation_text);
            QStringList issues;
            for (const auto& issue : sample.quality_issues)
                issues.push_back(QString::fromStdString(issue));
            auto* issue = new QTableWidgetItem(issues.join("; "));
            issue->setToolTip(issues.join("\n"));
            for (auto* value : {id, images, split, annotation, issue})
                value->setFlags(value->flags() & ~Qt::ItemIsEditable);
            table_->setItem(row, 0, use);
            table_->setItem(row, 1, id);
            table_->setItem(row, 2, images);
            table_->setItem(row, 3, split);
            table_->setItem(row, 4, annotation);
            table_->setItem(row, 5, issue);
        }
    } catch (const std::exception& error) {
        issue_banner_->setText(QString::fromUtf8(error.what()));
        issue_banner_->show();
    }
    rebuilding_ = false;
    apply_filter();
}

void DatasetSelectionView::apply_filter() {
    if (!table_) return;
    const QString needle = search_->text().trimmed();
    const QString mode = filter_->currentData().toString();
    for (int row = 0; row < table_->rowCount(); ++row) {
        QString haystack;
        for (int column = 1; column < table_->columnCount(); ++column)
            if (table_->item(row, column)) haystack += " " + table_->item(row, column)->text();
        if (table_->item(row, 1))
            haystack += " " + table_->item(row, 1)->data(Qt::UserRole).toString();
        const bool text_match = needle.isEmpty() ||
            haystack.contains(needle, Qt::CaseInsensitive);
        const bool selected = table_->item(row, 0)->checkState() == Qt::Checked;
        const QString annotation = table_->item(row, 4)->text();
        const bool has_issue = !table_->item(row, 5)->text().isEmpty();
        const bool mode_match = mode == "all" ||
            (mode == "selected" && selected) ||
            (mode == "annotated" &&
             annotation == tr("Annotated")) ||
            (mode == "issues" && has_issue);
        table_->setRowHidden(row, !text_match || !mode_match);
    }
}

} // namespace studio
