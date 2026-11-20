#include "modules/task/ui/pages/inference_workspace.h"

#include "common/ui/ui_metrics.h"
#include "modules/model_management/ui/weight_conversion_popup.h"
#include "modules/task/application/annotation/classification_annotation_state.h"
#include "modules/task/application/task_dataset_binding.h"
#include "modules/task/application/task_workspace_service.h"
#include "modules/task/integration/storage/model_weight_assets.h"
#include "modules/task/presentation/result_presentation.h"
#include "modules/task/presentation/sample_presentation.h"
#include "modules/task/ui/annotation/cls/classification_annotation.h"
#include "modules/task/ui/common/image_result_canvas.h"
#include "modules/task/ui/common/project_model_browser.h"
#include "modules/task/ui/common/resource_monitor.h"
#include "modules/task/ui/filters/inference_filter_panel.h"
#include "modules/task/ui/filters/task_inference_filter.h"
#include "modules/task/ui/pages/task_page_ui.h"

#include <runtime/inference_route.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <set>
#include <utility>

namespace studio {

using task_page::annotation_label;
using task_page::set_runtime_state;
using task_page::weight_type_label;

namespace {

QString device_type_label(jocelin::config::DeviceType type) {
    switch (type) {
    case jocelin::config::DeviceType::Cpu: return QObject::tr("CPU");
    case jocelin::config::DeviceType::Gpu: return QObject::tr("GPU");
    case jocelin::config::DeviceType::Other: return QObject::tr("Other");
    }
    return QObject::tr("Other");
}

} // namespace

InferenceWorkspace::InferenceWorkspace(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                             ThemeTokens::Space2, ThemeTokens::Space2);
    root->setSpacing(ThemeTokens::Space1);

    auto* command_bar = new QFrame(this);
    command_bar->setObjectName("inferenceCommandBar");
    command_bar->setProperty("sectionPanel", true);
    auto* command_layout = new QGridLayout(command_bar);
    command_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                                       ThemeTokens::Space2, ThemeTokens::Space1);
    command_layout->setHorizontalSpacing(ThemeTokens::Space2);
    command_layout->setVerticalSpacing(ThemeTokens::Space1);
    auto* device_type_label_widget = new QLabel(tr("Device type"), command_bar);
    device_type_label_widget->setProperty("meta", true);
    device_type_ = new QComboBox(command_bar);
    device_type_->setObjectName("inferenceDeviceType");
    device_type_->setMinimumContentsLength(5);
    device_type_->setMaximumWidth(100);
    auto* device_label = new QLabel(tr("Device"), command_bar);
    device_label->setProperty("meta", true);
    device_ = new QComboBox(command_bar);
    device_->setObjectName("inferenceDevice");
    device_->setMinimumContentsLength(10);
    device_->setMaximumWidth(210);
    checked_target_ = new QLabel(command_bar);
    checked_target_->setObjectName("inferenceCheckedWeight");
    checked_target_->setTextFormat(Qt::PlainText);
    checked_target_->setWordWrap(true);
    checked_target_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* batch_label = new QLabel(tr("Batch"), command_bar);
    batch_label->setProperty("meta", true);
    batch_size_ = new QSpinBox(command_bar);
    batch_size_->setObjectName("inferenceBatchSize");
    batch_size_->setRange(1, 1024);
    batch_size_->setMaximumWidth(90);
    current_image_ = new QLabel(tr("Ready"), command_bar);
    current_image_->setObjectName("inferenceCurrentImage");
    current_image_->setProperty("meta", true);
    current_image_->setWordWrap(true);
    current_image_->setMinimumWidth(0);
    current_image_->setMaximumHeight(44);
    current_image_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    state_ = new QLabel(tr("Idle"), command_bar);
    state_->setObjectName("inferenceRuntimeState");
    state_->setProperty("statusBadge", true);
    start_ = new QPushButton(tr("Infer"), command_bar);
    start_->setObjectName("inferenceStartButton");
    start_->setProperty("primary", true);
    progress_ = new QProgressBar(command_bar);
    progress_->setObjectName("inferenceProgress");
    progress_->setRange(0, 100);
    command_layout->addWidget(device_type_label_widget, 0, 0);
    command_layout->addWidget(device_type_, 0, 1);
    command_layout->addWidget(device_label, 0, 2);
    command_layout->addWidget(device_, 0, 3);
    command_layout->addWidget(checked_target_, 0, 4, 1, 2);
    command_layout->addWidget(batch_label, 0, 6);
    command_layout->addWidget(batch_size_, 0, 7);
    command_layout->addWidget(state_, 0, 8);
    command_layout->addWidget(start_, 0, 9);
    command_layout->addWidget(current_image_, 1, 0, 1, 4);
    command_layout->addWidget(progress_, 1, 4, 1, 6);
    command_layout->setColumnStretch(3, 1);
    command_layout->setColumnStretch(5, 1);
    root->addWidget(command_bar);

    auto* split = new CollapsibleSplitter(Qt::Horizontal, this);
    split->setObjectName("inferenceWorkspaceSplitter");

    auto* samples = new QFrame(split);
    samples->setObjectName("inferenceSamplesPanel");
    samples->setMinimumWidth(220);
    auto* samples_layout = new QVBoxLayout(samples);
    samples_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                                       ThemeTokens::Space2, ThemeTokens::Space2);
    samples_layout->setSpacing(ThemeTokens::Space1);
    auto* sample_header = new QHBoxLayout;
    auto* samples_title = new QLabel(tr("SAMPLES"), samples);
    samples_title->setProperty("eyebrow", true);
    sample_header->addWidget(samples_title);
    scope_summary_ = new QLabel(samples);
    scope_summary_->setObjectName("inferenceScopeSummary");
    scope_summary_->setProperty("meta", true);
    sample_header->addWidget(scope_summary_);
    sample_header->addStretch();
    samples_layout->addLayout(sample_header);

    QWidget* filter_bar = new QWidget(samples);
    filter_bar->setObjectName("inferenceSampleFilterBar");
    QHBoxLayout* filter_layout = new QHBoxLayout(filter_bar);
    filter_layout->setContentsMargins(0, 0, 0, 0);
    filter_layout->setSpacing(ThemeTokens::Space1);
    QFont filter_font = filter_bar->font();
    filter_font.setPixelSize(11);
    sample_split_filter_ = new QComboBox(filter_bar);
    sample_split_filter_->setObjectName("inferenceSampleSplitFilter");
    sample_split_filter_->setFont(filter_font);
    sample_split_filter_->setFixedHeight(24);
    sample_split_filter_->setMinimumWidth(90);
    sample_split_filter_->setMaximumWidth(110);
    sample_split_filter_->addItem(tr("All splits"), "all");
    sample_split_filter_->addItem(tr("Train"), "train");
    sample_split_filter_->addItem(tr("Validation"), "validation");
    sample_split_filter_->addItem(tr("Test"), "test");
    sample_split_filter_->addItem(tr("Unassigned"), "unassigned");
    sample_split_filter_->setToolTip(tr("Filter by assigned sample split."));
    sample_search_ = new QLineEdit(filter_bar);
    sample_search_->setObjectName("inferenceSampleSearch");
    sample_search_->setFont(filter_font);
    sample_search_->setFixedHeight(24);
    sample_search_->setMinimumWidth(0);
    sample_search_->setPlaceholderText(tr("Find samples"));
    sample_search_->setClearButtonEnabled(true);
    filter_layout->addWidget(sample_split_filter_);
    filter_layout->addWidget(sample_search_, 1);
    samples_layout->addWidget(filter_bar);

    images_ = new QListWidget(samples);
    images_->setToolTip(
        tr("Select images to limit the inference scope. No selection means all matching images; use Ctrl or Shift for multi-selection."));
    images_->setObjectName("inferenceImageList");
    images_->setIconSize(QSize(56, 42));
    images_->setTextElideMode(Qt::ElideMiddle);
    images_->setUniformItemSizes(true);
    images_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    samples_layout->addWidget(images_, 1);

    auto* viewport = new QFrame(split);
    viewport->setObjectName("inferenceViewportPanel");
    auto* viewport_layout = new QVBoxLayout(viewport);
    viewport_layout->setContentsMargins(0, 0, 0, 0);
    viewport_layout->setSpacing(0);
    auto* layers_panel = new QFrame(viewport);
    layers_panel->setObjectName("inferenceLayerBar");
    auto* layers = new QHBoxLayout(layers_panel);
    layers->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                               ThemeTokens::Space2, ThemeTokens::Space1);
    overlay_layer_ = new QCheckBox(tr("Inference"), layers_panel);
    overlay_layer_->setObjectName("inferenceOverlayLayerCheck");
    overlay_layer_->setChecked(true);
    layers->addWidget(overlay_layer_);
    layers->addStretch();
    viewport_layout->addWidget(layers_panel);
    canvas_stack_ = new QStackedWidget(viewport);
    canvas_stack_->setObjectName("inferenceCanvasStack");
    segmentation_canvas_ = new ImageResultCanvas(canvas_stack_);
    segmentation_canvas_->setObjectName("inferenceSegmentationCanvas");
    segmentation_canvas_->set_prediction_visible(true);
    segmentation_canvas_->set_overlay_opacity(0.42);
    classification_canvas_ = new ClassificationCanvas(canvas_stack_);
    classification_canvas_->setObjectName("inferenceClassificationCanvas");
    canvas_stack_->addWidget(segmentation_canvas_);
    canvas_stack_->addWidget(classification_canvas_);
    viewport_layout->addWidget(canvas_stack_, 1);

    auto* models = new QFrame(split);
    models->setObjectName("inferenceModelsPanel");
    models->setMinimumWidth(260);
    auto* models_layout = new QVBoxLayout(models);
    models_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                                      ThemeTokens::Space2, ThemeTokens::Space2);
    models_layout->setSpacing(ThemeTokens::Space1);
    filter_panel_ = new InferenceFilterPanel(models);
    filter_panel_->applied = [this] {
        if (changed) {
            changed();
        }
    };
    models_layout->addWidget(filter_panel_);
    model_browser_ = new ProjectModelBrowser(models);
    model_browser_->setObjectName("inferenceProjectModelBrowser");
    model_browser_->set_branch_check_selection();
    model_browser_->set_management_multi_selection(true);
    model_browser_->set_detail_visible(false);
    model_browser_->convert_weight_requested = [this](const QString& model_id, const QString& weight_id, QWidget* anchor) {
        if (!service_ || busy_) {
            return;
        }
        const jocelin::model::ModelEntry* model = service_->find_model(model_id);
        const jocelin::model::ModelWeight* source = model
            ? jocelin::model::find_weight(*model, weight_id.toStdString()) : nullptr;
        if (!model || !source) {
            return;
        }
        WeightConversionPopup popup(*model, *source, anchor);
        popup.adjustSize();
        const QPoint position = anchor->mapToGlobal(QPoint(0, anchor->height()));
        QScreen* screen = QGuiApplication::screenAt(position);
        if (screen) {
            const QRect bounds = screen->availableGeometry();
            popup.move(qBound(bounds.left(), position.x(), qMax(bounds.left(), bounds.right() - popup.width() + 1)),
                       qBound(bounds.top(), position.y(), qMax(bounds.top(), bounds.bottom() - popup.height() + 1)));
        } else {
            popup.move(position);
        }
        if (popup.exec() == QDialog::Accepted && convert_weight_requested) {
            convert_weight_requested(model_id, weight_id, popup.target_format());
        }
    };
    model_browser_->import_requested = [this] {
        if (import_model_requested) import_model_requested();
    };
    model_browser_->export_requested = [this](const QString& id) {
        if (export_model_requested) export_model_requested(id);
    };
    model_browser_->delete_models_requested = [this](const QStringList& ids) {
        if (delete_models_requested) delete_models_requested(ids);
    };
    model_browser_->delete_model_weights_requested = [this](const ModelWeightSelection& weights) {
        if (delete_model_weights_requested) {
            delete_model_weights_requested(weights);
        }
    };
    model_browser_->delete_model_weight_requested = [this](const QString& model_id, const QString& weight_id) {
        if (delete_model_weight_requested) {
            delete_model_weight_requested(model_id, weight_id);
        }
    };
    model_browser_->clear_models_requested = [this] {
        if (clear_models_requested) clear_models_requested();
    };
    model_browser_->weight_checks_changed = [this] {
        refresh_device_types();
        commit_result_selection();
        update_command_presentation();
    };
    models_layout->addWidget(model_browser_, 3);
    ResourceMonitorWidget* resources = new ResourceMonitorWidget(models);
    resources->setObjectName("inferenceResourceMonitor");
    models_layout->addWidget(resources, 1);

    split->addWidget(samples);
    split->addWidget(viewport);
    split->addWidget(models);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setCollapsible(0, true);
    split->setCollapsible(2, true);
    split->setHandleWidth(11);
    split->setSizes({280, 900, 340});
    split->handle(1)->setObjectName("inferenceSamplesHandle");
    split->handle(1)->setProperty("collapsePanelIndex", 0);
    split->handle(1)->setProperty("restoreWidth", 280);
    split->handle(1)->setToolTip(tr("Drag to resize samples; click to collapse or expand."));
    split->handle(2)->setObjectName("inferenceModelsHandle");
    split->handle(2)->setProperty("collapsePanelIndex", 2);
    split->handle(2)->setProperty("restoreWidth", 340);
    split->handle(2)->setToolTip(tr("Drag to resize Project Models; click to collapse or expand."));
    root->addWidget(split, 1);

    connect(batch_size_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (!service_ || rebuilding_) return;
        service_->task().inference_batch_size = value;
        if (changed) changed();
    });
    connect(device_type_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
        if (rebuilding_) return;
        refresh_devices();
        commit_device();
        update_command_presentation();
    });
    connect(device_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
        if (rebuilding_) return;
        commit_device();
        update_command_presentation();
    });
    const auto apply_sample_filter = [this] {
        if (rebuilding_) {
            return;
        }
        const QSignalBlocker blocker(images_);
        const QString split = sample_split_filter_->currentData().toString();
        const QString query = sample_search_->text().trimmed();
        QListWidgetItem* first_visible = nullptr;
        for (int index = 0; index < images_->count(); ++index) {
            QListWidgetItem* item = images_->item(index);
            const bool matches_split = split == "all" || item->data(Qt::UserRole + 1).toString() == split;
            const bool matches_query = query.isEmpty() ||
                item->text().contains(query, Qt::CaseInsensitive) ||
                item->data(Qt::UserRole).toString().contains(query, Qt::CaseInsensitive) ||
                item->data(Qt::UserRole + 2).toString().contains(query, Qt::CaseInsensitive);
            const bool visible = matches_split && matches_query;
            item->setHidden(!visible);
            if (visible && !first_visible) {
                first_visible = item;
            } else if (!visible) {
                item->setSelected(false);
            }
        }
        if (!first_visible) {
            images_->setCurrentRow(-1, QItemSelectionModel::NoUpdate);
        } else if (!images_->currentItem() || images_->currentItem()->isHidden()) {
            images_->setCurrentItem(first_visible, QItemSelectionModel::NoUpdate);
        }
        refresh_canvas();
        update_command_presentation();
    };
    connect(sample_split_filter_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [apply_sample_filter](int) { apply_sample_filter(); });
    connect(sample_search_, &QLineEdit::textChanged, this,
            [apply_sample_filter](const QString&) { apply_sample_filter(); });
    connect(images_, &QListWidget::itemSelectionChanged, this, [this] {
        update_command_presentation();
    });
    connect(images_, &QListWidget::currentRowChanged, this, [this](int) {
        refresh_canvas();
    });
    connect(overlay_layer_, &QCheckBox::toggled, this, [this](bool visible) {
        if (segmentation_canvas_) segmentation_canvas_->set_prediction_visible(visible);
        if (classification_canvas_) classification_canvas_->set_heatmap_visible(visible);
    });
    connect(start_, &QPushButton::clicked, this, [this] { request_start(); });
    update_command_presentation();
    UiMetrics::apply_semantic_metrics(this);
}

void InferenceWorkspace::set_service(TaskWorkspaceService* service) {
    if (service_ == service) {
        refresh();
        return;
    }

    filter_panel_->set_config(nullptr);
    filter_panel_->set_content(nullptr);
    model_browser_->set_models({});
    service_ = service;
    if (!service_) {
        refresh();
        return;
    }

    const TaskInferenceFilterSpec* spec = find_inference_filter_spec(service_->task().task);
    if (!spec) {
        refresh();
        return;
    }

    std::unique_ptr<InferenceFilterContent> content = spec->create_content();
    filter_panel_->set_content(std::move(content));
    jocelin::config::AiConfig& config = service_->task().inference_config;
    filter_panel_->set_config(&config);
    refresh();
}

void InferenceWorkspace::set_models(const std::vector<jocelin::model::ModelEntry>& models) {
    models_ = models;
    refresh();
}

void InferenceWorkspace::refresh() {
    rebuilding_ = true;
    images_->clear();
    if (!service_) {
        available_devices_.clear();
        cpu_only_weight_ = false;
        device_type_->clear();
        device_->clear();
        model_browser_->set_models({});
        if (segmentation_canvas_) segmentation_canvas_->clear();
        if (classification_canvas_) classification_canvas_->clear_canvas();
        rebuilding_ = false;
        update_command_presentation();
        return;
    }
    const auto& task = service_->task();
    available_devices_ = jocelin::runtime::available_compute_devices();
    const auto task_type = jocelin::config::tool_type_from_string(task.task);
    std::vector<jocelin::model::ModelEntry> task_models;
    for (const auto& model : models_)
        if (model.task == task_type) task_models.push_back(model);
    model_browser_->set_models(task_models);
    if (service_->selected_model_id().isEmpty() && !task_models.empty())
        service_->select_inference_model(QString::fromStdString(task_models.front().id));
    model_browser_->set_current_model_id(service_->selected_model_id());
    refresh_device_types();
    batch_size_->setValue(task.inference_batch_size);
    const std::set<std::string> selected(
        task.inference_images.begin(), task.inference_images.end());
    const auto& samples = service_->binding().samples;
    const QString split_filter = sample_split_filter_->currentData().toString();
    const QString query = sample_search_->text().trimmed();
    for (int sample_index = 0;
         sample_index < static_cast<int>(samples.size());
         ++sample_index) {
        const auto& sample = samples[static_cast<size_t>(sample_index)];
        if (!sample.selected) {
            continue;
        }
        const QString display_name = image_display_name(sample_index, sample);
        const QString tooltip = sample_image_tooltip(
            display_name, sample, annotation_label(sample));
        for (int image_index = 0;
             image_index < static_cast<int>(sample.image_paths.size());
             ++image_index) {
            const auto& path = sample.image_paths[static_cast<size_t>(image_index)];
            const QString item_text = sample.image_paths.size() > 1
                ? tr("%1 · %2/%3")
                      .arg(display_name)
                      .arg(image_index + 1)
                      .arg(sample.image_paths.size())
                : display_name;
            QListWidgetItem* item = new QListWidgetItem(item_text, images_);
            item->setData(Qt::UserRole, QString::fromStdString(path));
            item->setData(Qt::UserRole + 1, QString::fromStdString(normalized_split(sample.split)));
            item->setData(Qt::UserRole + 2, QString::fromStdString(sample.sample_id));
            item->setToolTip(tooltip);
            QImageReader reader(QString::fromStdString(path));
            reader.setAutoTransform(true);
            reader.setScaledSize(QSize(112, 84));
            const QImage thumbnail = reader.read();
            if (!thumbnail.isNull()) {
                item->setIcon(QPixmap::fromImage(thumbnail));
            }
            const bool matches_split = split_filter == "all" ||
                item->data(Qt::UserRole + 1).toString() == split_filter;
            const bool matches_query = query.isEmpty() ||
                item->text().contains(query, Qt::CaseInsensitive) ||
                item->data(Qt::UserRole).toString().contains(query, Qt::CaseInsensitive) ||
                item->data(Qt::UserRole + 2).toString().contains(query, Qt::CaseInsensitive);
            const bool visible = matches_split && matches_query;
            item->setHidden(!visible);
            if (visible && !task.inference_use_all_images && selected.count(path) != 0) {
                item->setSelected(true);
            }
        }
    }
    set_runtime_state(state_, QString::fromStdString(task.runtime.inference_status));
    if (!images_->currentItem() || images_->currentItem()->isHidden()) {
        QListWidgetItem* current = images_->selectedItems().isEmpty()
            ? nullptr : images_->selectedItems().front();
        for (int index = 0; !current && index < images_->count(); ++index) {
            if (!images_->item(index)->isHidden()) {
                current = images_->item(index);
            }
        }
        if (current) {
            images_->setCurrentItem(current, QItemSelectionModel::NoUpdate);
        } else {
            images_->setCurrentRow(-1, QItemSelectionModel::NoUpdate);
        }
    }
    rebuilding_ = false;
    update_command_presentation();
    refresh_canvas();
}

void InferenceWorkspace::commit_result_selection() {
    if (rebuilding_ || !service_) {
        return;
    }
    const ModelWeightSelection checked = model_browser_->checked_weights();
    if (checked.size() == 1) {
        service_->select_inference_result(checked.front().first, checked.front().second);
        refresh_canvas();
    }
}

void InferenceWorkspace::synchronize_result_selection() {
    if (!service_) {
        return;
    }
    model_browser_->set_current_model_id(service_->selected_model_id());
    update_command_presentation();
    refresh_canvas();
}

void InferenceWorkspace::refresh_device_types() {
    if (!device_type_) {
        return;
    }
    using jocelin::config::DeviceType;
    const jocelin::model::ModelWeight* weight = selected_weight(model_browser_->checked_weights());
    cpu_only_weight_ = false;
    if (weight && jocelin::model::weight_supports_device(*weight, DeviceType::Cpu)) {
        cpu_only_weight_ = !jocelin::model::weight_supports_device(*weight, DeviceType::Gpu);
        if (!cpu_only_weight_) {
            const jocelin::runtime::RuntimeCapabilities capabilities = jocelin::runtime::available_runtime_capabilities();
            cpu_only_weight_ = jocelin::runtime::runtime_supports_device(weight->format, DeviceType::Cpu, capabilities) &&
                !jocelin::runtime::runtime_supports_device(weight->format, DeviceType::Gpu, capabilities);
        }
    }
    QString preferred;
    if (service_)
        preferred = QString::fromStdString(
            jocelin::config::to_string(service_->task().device.type));

    QSignalBlocker blocker(device_type_);
    device_type_->clear();
    std::set<jocelin::config::DeviceType> types;
    for (const auto& device : available_devices_) {
        if (device.device.type != jocelin::config::DeviceType::Cpu &&
            device.device.type != jocelin::config::DeviceType::Gpu)
            continue;
        types.insert(device.device.type);
    }
    for (const auto type : {jocelin::config::DeviceType::Cpu,
                            jocelin::config::DeviceType::Gpu}) {
        if (types.count(type) == 0 || (cpu_only_weight_ && type != DeviceType::Cpu)) {
            continue;
        }
        device_type_->addItem(
            device_type_label(type),
            QString::fromStdString(jocelin::config::to_string(type)));
    }
    device_type_->setToolTip(cpu_only_weight_ ? tr("The checked Weight branch requires CPU.") : QString{});
    const int preferred_index = device_type_->findData(preferred);
    if (preferred_index >= 0) device_type_->setCurrentIndex(preferred_index);
    refresh_devices();
}

void InferenceWorkspace::refresh_devices() {
    if (!device_ || !device_type_) return;
    const QString selected_type = device_type_->currentData().toString();
    QString preferred_id;
    int preferred_index = -1;
    if (service_) {
        preferred_id = QString::fromStdString(service_->task().device.id);
        preferred_index = service_->task().device.index;
    }
    const jocelin::runtime::ComputeDevice* selected = selected_device();
    if (selected && QString::fromStdString(jocelin::config::to_string(selected->device.type)) == selected_type) {
        preferred_id = QString::fromStdString(selected->device.id);
        preferred_index = selected->device.index;
    }

    QSignalBlocker blocker(device_);
    device_->clear();
    int match = -1;
    for (size_t index = 0; index < available_devices_.size(); ++index) {
        const auto& compute = available_devices_[index];
        if (QString::fromStdString(jocelin::config::to_string(compute.device.type)) !=
            selected_type)
            continue;
        device_->addItem(
            QString::fromStdString(compute.display_name),
            static_cast<int>(index));
        if (QString::fromStdString(compute.device.id) == preferred_id &&
            compute.device.index == preferred_index)
            match = device_->count() - 1;
    }
    if (match >= 0) device_->setCurrentIndex(match);
}

void InferenceWorkspace::commit_device() {
    // Keep the user's device preference when CPU is required by the checked branch.
    if (rebuilding_ || !service_ || cpu_only_weight_) {
        return;
    }
    const auto* compute = selected_device();
    if (!compute || service_->task().device == compute->device) return;
    service_->task().device = compute->device;
    if (changed) changed();
}

const jocelin::runtime::ComputeDevice* InferenceWorkspace::selected_device() const {
    if (!device_ || device_->currentIndex() < 0) return nullptr;
    const int index = device_->currentData().toInt();
    if (index < 0 || index >= static_cast<int>(available_devices_.size())) return nullptr;
    return &available_devices_[static_cast<size_t>(index)];
}

const jocelin::model::ModelWeight* InferenceWorkspace::selected_weight(const ModelWeightSelection& checked) const {
    if (!service_ || checked.size() != 1) {
        return nullptr;
    }
    const jocelin::model::ModelEntry* model = service_->find_model(checked.front().first);
    return model ? jocelin::model::find_weight(*model, checked.front().second.toStdString()) : nullptr;
}

namespace {

bool inference_weight_available(const jocelin::model::ModelWeight& weight, const jocelin::config::Device& device) {
    if (!ModelWeightAssets::missing(weight).isEmpty() || !jocelin::model::weight_supports_device(weight, device.type)) {
        return false;
    }
    const auto capabilities = jocelin::runtime::available_runtime_capabilities();
    return jocelin::runtime::runtime_is_available(weight.format, capabilities) &&
        jocelin::runtime::runtime_supports_device(weight.format, device.type, capabilities);
}

} // namespace

void InferenceWorkspace::refresh_canvas() {
    if (service_ && !busy_)
        set_runtime_state(state_, service_->selected_inference_result() ? QStringLiteral("completed") : QStringLiteral("idle"));
    if (!canvas_stack_ || !segmentation_canvas_ || !classification_canvas_) return;
    if (!service_ || !images_ || !images_->currentItem()) {
        segmentation_canvas_->clear();
        classification_canvas_->clear_canvas();
        return;
    }

    const QString image_path = images_->currentItem()->data(Qt::UserRole).toString();
    const auto& task = service_->task();
    const bool classification = task.task == "cls";
    canvas_stack_->setCurrentWidget(
        classification ? static_cast<QWidget*>(classification_canvas_)
                       : static_cast<QWidget*>(segmentation_canvas_));
    overlay_layer_->setText(classification ? tr("Heatmap") : tr("Inference"));

    if (classification) {
        const QImage image(image_path);
        if (image.isNull()) {
            classification_canvas_->clear_canvas();
            return;
        }
        classification_canvas_->set_image(image);
        classification_canvas_->set_heatmap({});
        classification_canvas_->set_heatmap_visible(overlay_layer_->isChecked());
    } else {
        QString error;
        if (!segmentation_canvas_->load(image_path, &error)) {
            segmentation_canvas_->clear();
            return;
        }
        segmentation_canvas_->set_prediction_overlay({});
    }

    const ResultReference* match = nullptr;
    const auto* selected_result = service_->selected_inference_result();
    const auto* sample = task_sample_for_path(service_->binding(), image_path);
    if (selected_result && sample) {
        for (const auto& result : selected_result->samples) {
            if (result.source_sample_id != sample->sample_id) continue;
            if (std::visit([&](const auto& value) {
                    return QFileInfo(QString::fromStdString(value.image_path)).absoluteFilePath() == QFileInfo(image_path).absoluteFilePath();
                }, result.result)) {
                match = &result; break;
            }
        }
    }
    if (!match) return;

    if (classification) {
        classification_canvas_->set_heatmap(
            QImage(QString::fromStdString(std::get<jocelin::result::cls::ClsResult>(match->result).heatmap_path)));
    } else {
        const jocelin::model::ModelEntry* model = service_->find_model(service_->selected_model_id());
        const std::vector<jocelin::model::ModelClass> model_classes = model
            ? model->classes : std::vector<jocelin::model::ModelClass>{};
        segmentation_canvas_->set_prediction_overlay(prediction_overlay_from_result(
            std::get<jocelin::result::sg::SgResult>(match->result), model_classes, service_->binding().classes));
    }
}

void InferenceWorkspace::commit_selection_scope() {
    if (rebuilding_ || !service_ || !images_) {
        return;
    }
    TaskNodeConfig& task = service_->task();
    const QList<QListWidgetItem*> selected = images_->selectedItems();
    const bool filtered = sample_split_filter_->currentData().toString() != "all" ||
        !sample_search_->text().trimmed().isEmpty();
    task.inference_use_all_images = selected.isEmpty() && !filtered;
    task.inference_images.clear();
    if (!selected.isEmpty()) {
        for (QListWidgetItem* item : selected) {
            task.inference_images.push_back(
                item->data(Qt::UserRole).toString().toStdString());
        }
    } else if (filtered) {
        for (int index = 0; index < images_->count(); ++index) {
            QListWidgetItem* item = images_->item(index);
            if (!item->isHidden()) {
                task.inference_images.push_back(
                    item->data(Qt::UserRole).toString().toStdString());
            }
        }
    }
    if (changed) {
        changed();
    }
}

void InferenceWorkspace::update_command_presentation() {
    if (device_type_) {
        device_type_->setEnabled(!busy_ && device_type_->count() > 0);
    }
    if (device_) {
        device_->setEnabled(!busy_ && device_->count() > 0);
    }
    const int selected = images_ ? images_->selectedItems().size() : 0;
    int total = 0;
    if (images_) {
        for (int index = 0; index < images_->count(); ++index) {
            if (!images_->item(index)->isHidden()) {
                ++total;
            }
        }
    }
    const bool filtered = sample_split_filter_->currentData().toString() != "all" ||
        !sample_search_->text().trimmed().isEmpty();
    QString scope;
    QString summary;
    if (total == 0) {
        summary = filtered ? tr("No matches") : tr("No Task images");
    } else if (selected == 0) {
        scope = filtered ? tr("all %1 matching images").arg(total)
                         : tr("all %1 images").arg(total);
        summary = filtered ? tr("%1 matching").arg(total)
                           : tr("All %1 images").arg(total);
    } else if (selected == 1) {
        scope = tr("the selected image");
        summary = tr("1 image selected");
    } else {
        scope = tr("%1 selected images").arg(selected);
        summary = tr("%1 selected").arg(selected);
    }
    if (scope_summary_) {
        scope_summary_->setText(summary);
    }
    if (!start_) {
        return;
    }
    start_->setText(tr("Infer"));
    start_->setAccessibleName(tr("Infer"));

    const ModelWeightSelection checked = model_browser_->checked_weights();
    const jocelin::runtime::ComputeDevice* compute = selected_device();
    const jocelin::model::ModelWeight* weight = selected_weight(checked);
    const bool available = compute && weight && inference_weight_available(*weight, compute->device);
    QString checked_label;
    if (weight) {
        const jocelin::model::ModelEntry* model = service_->find_model(checked.front().first);
        checked_label = tr("%1 · %2").arg(QString::fromStdString(model->name), checked.front().second);
    } else if (checked.empty()) {
        checked_label = tr("No Weight branch checked");
    } else {
        checked_label = tr("%1 Weight branches checked").arg(checked.size());
    }
    checked_target_->setText(checked_label);
    checked_target_->setToolTip(checked_label);
    if (total == 0) {
        start_->setToolTip(filtered ? tr("No images match the current filters.")
                                    : tr("Add Task images before starting inference."));
    } else if (!weight) {
        start_->setToolTip(tr("Check exactly one Weight branch to run inference."));
    } else if (!compute) {
        start_->setToolTip(tr("Select an available CPU or GPU device."));
    } else if (!available) {
        start_->setToolTip(tr("The checked Weight branch requires its asset files and a compatible device and runtime."));
    } else {
        start_->setToolTip(
            tr("Run inference for %1 with %2 on %3.")
                .arg(scope,
                     weight_type_label(weight->format),
                     QString::fromStdString(compute->display_name)));
    }
    start_->setEnabled(!busy_ && total > 0 && available);
}

void InferenceWorkspace::request_start() {
    if (busy_ || !images_ || !start_ || !start_->isEnabled() || !model_browser_) {
        return;
    }
    const ModelWeightSelection checked = model_browser_->checked_weights();
    const jocelin::runtime::ComputeDevice* compute = selected_device();
    const jocelin::model::ModelWeight* weight = selected_weight(checked);
    if (!compute || !weight || !inference_weight_available(*weight, compute->device)) {
        return;
    }
    InferenceLaunchRequest request;
    request.model_id = checked.front().first;
    request.weight_id = checked.front().second;
    request.device = compute->device;
    commit_result_selection();
    commit_selection_scope();
    commit_device();
    if (start_requested) {
        start_requested(request);
    }
}

void InferenceWorkspace::set_busy(bool busy) {
    busy_ = busy;
    update_command_presentation();
    filter_panel_->setEnabled(!busy);
    model_browser_->setEnabled(!busy);
    images_->setEnabled(!busy);
    sample_split_filter_->setEnabled(!busy);
    sample_search_->setEnabled(!busy);
    device_type_->setEnabled(!busy_ && device_type_->count() > 0);
    device_->setEnabled(!busy_ && device_->count() > 0);
    batch_size_->setEnabled(!busy);
}

void InferenceWorkspace::begin() {
    progress_->setValue(0);
    current_image_->setText(tr("Waiting for first sample"));
    current_image_->setToolTip({});
    set_runtime_state(state_, QStringLiteral("running"));
}

void InferenceWorkspace::update_progress(
    size_t done, size_t total, const QString& image)
{
    set_runtime_state(state_, QStringLiteral("running"));
    progress_->setValue(total > 0
        ? qBound(0, static_cast<int>(done * 100 / total), 100) : 0);
    QString display_name = tr("Task image");
    if (service_) {
        int sample_index = -1;
        int image_index = -1;
        const auto* sample = task_sample_for_path(
            service_->binding(), image, &sample_index, &image_index);
        if (sample) {
            display_name = image_display_name(sample_index, *sample);
            if (sample->image_paths.size() > 1)
                display_name = tr("%1 · %2/%3")
                    .arg(display_name)
                    .arg(image_index + 1)
                    .arg(sample->image_paths.size());
        }
    }
    current_image_->setText(tr("%1 / %2 · %3")
        .arg(done)
        .arg(total)
        .arg(display_name));
    current_image_->setToolTip(image);
}

void InferenceWorkspace::finish(const QString& error) {
    set_busy(false);
    set_runtime_state(
        state_, error.isEmpty() ? QStringLiteral("completed")
                                : QStringLiteral("error"));
    current_image_->setText(error.isEmpty()
        ? tr("Completed · review in Annotation or Results")
        : error);
    if (error.isEmpty()) progress_->setValue(100);
    refresh();
}

} // namespace studio
