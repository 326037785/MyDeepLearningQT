#include "modules/task/ui/pages/results_workspace.h"

#include "common/ui/ui_metrics.h"
#include "modules/task/application/annotation/classification_annotation_state.h"
#include "modules/task/application/task_workspace_service.h"
#include "modules/task/presentation/result_presentation.h"
#include "modules/task/presentation/sample_presentation.h"
#include "modules/task/ui/annotation/cls/classification_annotation.h"
#include "modules/task/ui/common/image_result_canvas.h"
#include "modules/task/ui/common/project_model_browser.h"
#include "modules/task/ui/pages/task_page_ui.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <set>
#include <type_traits>

namespace studio {

using task_page::annotation_label;
using task_page::helper_label;
using task_page::weight_type_label;

namespace {

QString unique_destination(const QDir& directory, const QString& file_name) {
    const QFileInfo info(file_name);
    QString destination = directory.filePath(info.fileName());
    for (int suffix = 2; QFileInfo::exists(destination); ++suffix)
        destination = directory.filePath(
            info.completeBaseName() + "-" + QString::number(suffix) +
            (info.suffix().isEmpty() ? QString{} : "." + info.suffix()));
    return destination;
}

} // namespace

ResultsWorkspace::ResultsWorkspace(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                             ThemeTokens::Space2, ThemeTokens::Space2);
    root->setSpacing(ThemeTokens::Space1);

    auto* actions = new QHBoxLayout;
    actions->addStretch();
    open_folder_ = new QPushButton(tr("Open folder"), this);
    open_folder_->setProperty("compact", true);
    open_folder_->setToolTip(tr("Open the persisted result directory"));
    export_button_ = new QPushButton(tr("Export"), this);
    export_button_->setObjectName("exportTaskResultsButton");
    export_button_->setProperty("accentSecondary", true);
    export_button_->setToolTip(tr("Export the selected persisted result"));
    actions->addWidget(open_folder_);
    actions->addWidget(export_button_);
    root->addLayout(actions);

    model_browser_ = new ProjectModelBrowser(this);
    model_browser_->setObjectName("resultsProjectModelBrowser");
    model_browser_->set_model_check_selection(true);
    model_browser_->set_management_actions_enabled(false);
    model_browser_->set_detail_visible(false);
    model_browser_->setMaximumHeight(165);
    const auto select_model = [this](const QString& id) {
        if (service_ && service_->selected_model_id() != id) {
            service_->select_inference_model(id);
            refresh();
        }
    };
    model_browser_->selection_changed = select_model;
    model_browser_->checked_model_changed = select_model;
    model_browser_->weight_branch_selection_changed = [this, select_model](const QString& id, const QString& weight) {
        if (weight.isEmpty()) {
            select_model(id);
        } else if (service_) {
            service_->select_inference_result(id, weight);
            refresh();
        }
    };
    model_browser_->set_weight_selection_mode(true);
    root->addWidget(model_browser_);
    weight_branch_ = new QComboBox(this);
    weight_branch_->setObjectName("resultsWeightBranch");
    root->addWidget(weight_branch_);
    connect(weight_branch_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!service_) {
            return;
        }
        service_->select_inference_result(service_->selected_model_id(), weight_branch_->currentData().toString());
        refresh();
    });


    auto* summary = new QFrame(this);
    summary_ = summary;
    summary->setObjectName("taskResultsSummary");
    summary->setProperty("sectionPanel", true);
    summary->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* summary_layout = new QGridLayout(summary);
    summary_layout->setContentsMargins(ThemeTokens::Space3, ThemeTokens::Space2,
                                       ThemeTokens::Space3, ThemeTokens::Space2);
    context_ = new QLabel(tr("No result selected"), summary);
    context_->setProperty("workspaceTitle", true);
    summary_layout->addWidget(context_, 0, 0, 1, 6);
    summary_layout->addWidget(new QLabel(tr("Precision"), summary), 1, 0);
    precision_ = new QLabel(QStringLiteral("—"), summary);
    summary_layout->addWidget(precision_, 1, 1);
    summary_layout->addWidget(new QLabel(tr("Recall"), summary), 1, 2);
    recall_ = new QLabel(QStringLiteral("—"), summary);
    summary_layout->addWidget(recall_, 1, 3);
    summary_layout->addWidget(new QLabel(tr("F1"), summary), 1, 4);
    f1_ = new QLabel(QStringLiteral("—"), summary);
    summary_layout->addWidget(f1_, 1, 5);
    benchmark_label_ = new QLabel(summary);
    summary_layout->addWidget(benchmark_label_, 2, 0);
    benchmark_ = new QLabel(QStringLiteral("—"), summary);
    summary_layout->addWidget(benchmark_, 2, 1);
    summary_layout->setColumnStretch(6, 1);
    root->addWidget(summary);

    state_ = helper_label({}, this);
    state_->setObjectName("taskResultsState");
    root->addWidget(state_);

    empty_state_ = new QFrame(this);
    empty_state_->setObjectName("taskResultsEmptyState");
    empty_state_->setProperty("emptyState", true);
    empty_state_->setMaximumHeight(44);
    auto* empty_layout = new QHBoxLayout(empty_state_);
    empty_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                                     ThemeTokens::Space2, ThemeTokens::Space1);
    auto* empty_copy = new QLabel(
        tr("No persisted result for this model and weight format."), empty_state_);
    empty_copy->setProperty("meta", true);
    empty_layout->addWidget(empty_copy);
    empty_layout->addStretch();
    root->addWidget(empty_state_);

    empty_filler_ = new QWidget(this);
    empty_filler_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    root->addWidget(empty_filler_, 1);

    auto* content_split = new CollapsibleSplitter(Qt::Horizontal, this);
    content_ = content_split;
    content_split->setObjectName("taskResultsContent");

    auto* samples_panel = new QFrame(content_split);
    samples_panel->setObjectName("resultsSamplesPanel");
    samples_panel->setMinimumWidth(240);
    auto* samples_layout = new QVBoxLayout(samples_panel);
    samples_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                                       ThemeTokens::Space2, ThemeTokens::Space2);
    samples_layout->setSpacing(ThemeTokens::Space1);
    auto* samples_title = new QLabel(tr("SAMPLES"), samples_panel);
    samples_title->setProperty("eyebrow", true);
    samples_layout->addWidget(samples_title);
    table_ = new QTableWidget(samples_panel);
    table_->setObjectName("taskResultsTable");
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(
        {tr("Sample"), tr("Prediction"), tr("Confidence"), tr("Area (px)" )});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    samples_layout->addWidget(table_, 1);

    auto* viewport = new QFrame(content_split);
    viewport->setObjectName("resultsViewportPanel");
    auto* viewport_layout = new QVBoxLayout(viewport);
    viewport_layout->setContentsMargins(0, 0, 0, 0);
    viewport_layout->setSpacing(0);
    auto* layers_panel = new QFrame(viewport);
    layers_panel->setObjectName("resultsLayerBar");
    auto* layers = new QHBoxLayout(layers_panel);
    layers->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                               ThemeTokens::Space2, ThemeTokens::Space1);
    overlay_layer_ = new QCheckBox(tr("Inference"), layers_panel);
    overlay_layer_->setObjectName("resultsOverlayLayerCheck");
    overlay_layer_->setChecked(true);
    layers->addWidget(overlay_layer_);
    layers->addStretch();
    viewport_layout->addWidget(layers_panel);
    canvas_stack_ = new QStackedWidget(viewport);
    canvas_stack_->setObjectName("resultsCanvasStack");
    segmentation_canvas_ = new ImageResultCanvas(canvas_stack_);
    segmentation_canvas_->setObjectName("resultsSegmentationCanvas");
    segmentation_canvas_->set_prediction_visible(true);
    segmentation_canvas_->set_overlay_opacity(0.42);
    classification_canvas_ = new ClassificationCanvas(canvas_stack_);
    classification_canvas_->setObjectName("resultsClassificationCanvas");
    canvas_stack_->addWidget(segmentation_canvas_);
    canvas_stack_->addWidget(classification_canvas_);
    viewport_layout->addWidget(canvas_stack_, 1);

    auto* detail = new QFrame(content_split);
    detail->setObjectName("resultsDetailPanel");
    detail->setMinimumWidth(220);
    auto* detail_layout = new QVBoxLayout(detail);
    detail_layout->setContentsMargins(ThemeTokens::Space3, ThemeTokens::Space3,
                                      ThemeTokens::Space3, ThemeTokens::Space3);
    auto* detail_title = new QLabel(tr("DETAIL"), detail);
    detail_title->setProperty("eyebrow", true);
    detail_layout->addWidget(detail_title);
    sample_detail_ = new QLabel(tr("Select a result sample."), detail);
    sample_detail_->setWordWrap(true);
    sample_detail_->setProperty("meta", true);
    detail_layout->addWidget(sample_detail_);
    detail_layout->addStretch();

    content_split->addWidget(samples_panel);
    content_split->addWidget(viewport);
    content_split->addWidget(detail);
    content_split->setStretchFactor(0, 0);
    content_split->setStretchFactor(1, 1);
    content_split->setStretchFactor(2, 0);
    content_split->setCollapsible(0, true);
    content_split->setCollapsible(2, true);
    content_split->setHandleWidth(11);
    content_split->setSizes({360, 900, 260});
    content_split->handle(1)->setObjectName("resultsSamplesHandle");
    content_split->handle(1)->setProperty("collapsePanelIndex", 0);
    content_split->handle(1)->setProperty("restoreWidth", 360);
    content_split->handle(1)->setToolTip(tr("Drag to resize result samples; click to collapse or expand."));
    content_split->handle(2)->setObjectName("resultsDetailHandle");
    content_split->handle(2)->setProperty("collapsePanelIndex", 2);
    content_split->handle(2)->setProperty("restoreWidth", 260);
    content_split->handle(2)->setToolTip(tr("Drag to resize result details; click to collapse or expand."));
    root->addWidget(content_, 1);

    connect(table_, &QTableWidget::itemSelectionChanged, this,
            [this] { refresh_sample_detail(); });
    connect(overlay_layer_, &QCheckBox::toggled, this, [this](bool visible) {
        if (segmentation_canvas_) segmentation_canvas_->set_prediction_visible(visible);
        if (classification_canvas_) classification_canvas_->set_heatmap_visible(visible);
    });
    connect(export_button_, &QPushButton::clicked, this,
            [this] { export_results(); });
    connect(open_folder_, &QPushButton::clicked, this, [this] {
        const auto* result = selected_result_set();
        if (!result) return;
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QString::fromStdString(result->result_directory)));
    });
    UiMetrics::apply_semantic_metrics(this);
}

void ResultsWorkspace::set_service(TaskWorkspaceService* service) {
    if (service_ != service) {
        model_browser_->set_models(service ? models_ : std::vector<jocelin::model::ModelEntry>{});
    }
    service_ = service;
    refresh();
}

void ResultsWorkspace::set_models(const std::vector<jocelin::model::ModelEntry>& models) {
    models_ = models;
    model_browser_->set_models(service_ ? models_ : std::vector<jocelin::model::ModelEntry>{});
    refresh();
}

const InferenceResultSet* ResultsWorkspace::selected_result_set() const {
    return service_ ? service_->selected_inference_result() : nullptr;
}

void ResultsWorkspace::refresh() {
    table_->setRowCount(0);
    const QSignalBlocker blocker(weight_branch_);
    weight_branch_->clear();
    selected_model_id_ = service_ ? service_->selected_model_id() : QString{};
    if (service_) {
        const auto* model = service_->find_model(selected_model_id_);
        if (model) {
            for (const auto& weight : model->weights)
                weight_branch_->addItem(QString::fromStdString(weight.id) + " · " + weight_type_label(weight.format),
                    QString::fromStdString(weight.id));
            const QSignalBlocker browser_blocker(model_browser_);
            const jocelin::model::ModelWeight* weight = jocelin::model::find_weight(*model, service_->selected_weight_id().toStdString());
            if (weight) {
                model_browser_->set_current_weight(selected_model_id_, weight->format, QString::fromStdString(weight->id));
            } else {
                model_browser_->set_current_model_id(selected_model_id_);
                model_browser_->set_checked_model_id(selected_model_id_);
            }
            weight_branch_->setCurrentIndex(weight_branch_->findData(service_->selected_weight_id()));
        }
    }
    refresh_selected_result();
}

void ResultsWorkspace::refresh_selected_result() {
    table_->setRowCount(0);
    if (segmentation_canvas_) segmentation_canvas_->clear();
    if (classification_canvas_) classification_canvas_->clear_canvas();
    sample_detail_->setText(tr("Select a result sample."));
    const auto* result_set = selected_result_set();
    const auto metric_text = [](bool available, double value) {
        return available ? QString::number(value, 'f', 4) : QStringLiteral("—");
    };

    QString model_name = selected_model_id_;
    for (const auto& model : models_)
        if (QString::fromStdString(model.id) == selected_model_id_) {
            model_name = QString::fromStdString(model.name);
            break;
        }
    const QString format = service_ ? service_->selected_weight_id() : QString{};
    context_->setText(selected_model_id_.isEmpty()
        ? tr("No result selected")
        : tr("%1 · %2").arg(model_name, format));

    const bool has_result = result_set != nullptr;
    summary_->setVisible(has_result);
    empty_state_->setVisible(!has_result);
    empty_filler_->setVisible(!has_result);
    content_->setVisible(has_result);
    open_folder_->setEnabled(has_result);
    export_button_->setEnabled(has_result);
    benchmark_label_->setText(service_ && service_->task().task == "cls" ? tr("Accuracy") : tr("Mean IoU"));
    benchmark_->setText(QStringLiteral("—"));
    if (!has_result) {
        precision_->setText(QStringLiteral("—"));
        recall_->setText(QStringLiteral("—"));
        f1_->setText(QStringLiteral("—"));
        state_->clear();
        state_->hide();
        return;
    }

    const bool classification = service_->task().task == "cls";
    if (classification) {
        if (result_set->aggregate_metrics.available)
            benchmark_->setText(QString::number(result_set->aggregate_metrics.accuracy, 'f', 4));
        precision_->setText(metric_text(
            result_set->aggregate_metrics.available,
            result_set->aggregate_metrics.precision));
        recall_->setText(metric_text(
            result_set->aggregate_metrics.available,
            result_set->aggregate_metrics.recall));
        f1_->setText(metric_text(
            result_set->aggregate_metrics.available,
            result_set->aggregate_metrics.f1));
    } else {
        precision_->setText(QStringLiteral("—"));
        recall_->setText(QStringLiteral("—"));
        f1_->setText(QStringLiteral("—"));
    }

    state_->show();
    state_->setText(
        tr("%1 sample result(s) · weight %2")
            .arg(result_set->samples.size())
            .arg(QString::fromStdString(result_set->weight_id)));
    table_->setRowCount(static_cast<int>(result_set->samples.size()));
    const auto& binding = service_->binding();
    for (int row = 0; row < static_cast<int>(result_set->samples.size()); ++row) {
        const auto& result = result_set->samples[static_cast<size_t>(row)];
        const int source_index = task_sample_index(binding, result.source_sample_id);
        QString source_name = QString::fromStdString(result.source_sample_id);
        const auto* cls = std::get_if<jocelin::result::cls::ClsResult>(&result.result);
        const auto* sg = std::get_if<jocelin::result::sg::SgResult>(&result.result);
        QString source_tooltip = QString::fromStdString(cls ? cls->image_path : sg->image_path);
        if (source_index >= 0) {
            const auto& source_sample = binding.samples[static_cast<size_t>(source_index)];
            source_name = image_display_name(source_index, source_sample);
            source_tooltip = sample_image_tooltip(
                source_name, source_sample, annotation_label(source_sample));
        }
        auto* sample = new QTableWidgetItem(source_name);
        sample->setToolTip(source_tooltip);
        sample->setData(Qt::UserRole, row);
        auto* prediction = new QTableWidgetItem(
            cls
                ? (cls->predicted_class == -1 ? tr("Unknown") : tr("Class %1").arg(cls->predicted_class))
                : tr("Segmentation"));
        auto* confidence = new QTableWidgetItem(
            QString::number(cls ? cls->confidence : sg->mean_confidence, 'f', 4));
        auto* area = new QTableWidgetItem(
            cls ? QStringLiteral("—")
                           : QString::number(sg->predicted_area));
        for (auto* item : {sample, prediction, confidence, area})
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 0, sample);
        table_->setItem(row, 1, prediction);
        table_->setItem(row, 2, confidence);
        table_->setItem(row, 3, area);
    }
    if (table_->rowCount() > 0) table_->selectRow(0);
}

void ResultsWorkspace::refresh_sample_detail() {
    const auto* result_set = selected_result_set();
    if (!result_set || table_->currentRow() < 0 ||
        table_->currentRow() >= static_cast<int>(result_set->samples.size())) {
        sample_detail_->setText(tr("Select a result sample."));
        if (segmentation_canvas_) segmentation_canvas_->clear();
        if (classification_canvas_) classification_canvas_->clear_canvas();
        return;
    }

    const auto& result =
        result_set->samples[static_cast<size_t>(table_->currentRow())];
    const auto* cls = std::get_if<jocelin::result::cls::ClsResult>(&result.result);
    const auto* sg = std::get_if<jocelin::result::sg::SgResult>(&result.result);
    const bool classification = cls != nullptr;
    if (sg) {
        benchmark_->setText(sg->metrics.available
            ? QString::number(sg->metrics.mean_iou, 'f', 4) : QStringLiteral("—"));
        precision_->setText(sg->metrics.available
            ? QString::number(sg->metrics.precision, 'f', 4) : QStringLiteral("—"));
        recall_->setText(sg->metrics.available
            ? QString::number(sg->metrics.recall, 'f', 4) : QStringLiteral("—"));
        f1_->setText(sg->metrics.available
            ? QString::number(sg->metrics.f1, 'f', 4) : QStringLiteral("—"));
    }
    sample_detail_->setText(classification
        ? tr("Prediction: %1\nConfidence: %2\nInterest: %3")
              .arg(cls->predicted_class == -1 ? tr("Unknown") : tr("Class %1").arg(cls->predicted_class))
              .arg(cls->confidence, 0, 'f', 4)
              .arg(cls->interest ? tr("Yes") : tr("No"))
        : tr("Confidence: %1\nPredicted area: %2 px")
              .arg(sg->mean_confidence, 0, 'f', 4).arg(sg->predicted_area));

    if (!canvas_stack_ || !segmentation_canvas_ || !classification_canvas_) return;
    const QString source = QString::fromStdString(cls ? cls->image_path : sg->image_path);
    canvas_stack_->setCurrentWidget(
        classification ? static_cast<QWidget*>(classification_canvas_)
                       : static_cast<QWidget*>(segmentation_canvas_));
    overlay_layer_->setText(classification ? tr("Heatmap") : tr("Inference"));

    if (classification) {
        const QImage image(source);
        if (image.isNull()) {
            classification_canvas_->clear_canvas();
            return;
        }
        classification_canvas_->set_image(image);
        classification_canvas_->set_heatmap(
            QImage(QString::fromStdString(cls->heatmap_path)));
        classification_canvas_->set_heatmap_visible(overlay_layer_->isChecked());
        return;
    }

    QString error;
    if (!segmentation_canvas_->load(source, &error)) {
        segmentation_canvas_->clear();
        return;
    }
    const jocelin::model::ModelEntry* model = service_->find_model(service_->selected_model_id());
    const std::vector<jocelin::model::ModelClass> model_classes = model
        ? model->classes : std::vector<jocelin::model::ModelClass>{};
    segmentation_canvas_->set_prediction_overlay(
        prediction_overlay_from_result(*sg, model_classes, service_->binding().classes));
}

void ResultsWorkspace::export_results() {
    const auto* result_set = selected_result_set();
    if (!result_set) return;
    const QString selected = QFileDialog::getExistingDirectory(
        this, tr("Export selected result"));
    if (selected.isEmpty()) return;
    const QDir destination(selected);
    std::set<QString> sources;
    const QString manifest =
        QDir(QString::fromStdString(result_set->result_directory)).filePath("result.json");
    if (QFileInfo(manifest).isFile()) sources.insert(manifest);
    for (const auto& result : result_set->samples) {
        std::visit([&](const auto& task_result) {
            std::vector<std::string> paths{task_result.metadata_path};
            using Result = std::decay_t<decltype(task_result)>;
            if constexpr (std::is_same_v<Result, jocelin::result::sg::SgResult>) {
                paths.push_back(task_result.mask_path);
                paths.push_back(task_result.visualization_path);
            } else {
                paths.push_back(task_result.heatmap_path);
            }
            for (const auto& source : paths) {
                const QString path = QString::fromStdString(source);
                if (QFileInfo(path).isFile()) sources.insert(path);
            }
        }, result.result);
    }
    int copied = 0;
    for (const auto& path : sources)
        copied += QFile::copy(path, unique_destination(destination, path)) ? 1 : 0;
    state_->setText(
        tr("Exported %1 file(s) to %2.").arg(copied).arg(selected));
}

} // namespace studio
