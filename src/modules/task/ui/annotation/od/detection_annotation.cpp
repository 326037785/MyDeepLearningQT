#include "modules/task/ui/annotation/shared/annotation_classes_panel.h"
#include "modules/task/ui/annotation/od/canvas/detection_canvas.h"
#include "modules/task/ui/annotation/shared/annotation_panels.h"
#include "modules/task/ui/samples/annotation_samples.h"
#include "modules/task/ui/annotation/od/detection_annotation.h"

#include "common/ui/icon_provider.h"
#include "common/ui/ui_metrics.h"
#include "common/ui/editing/confirm_transition.h"
#include "modules/settings/application/annotation_preferences.h"
#include "modules/task/application/task_dataset_binding.h"
#include "modules/task/application/task_workspace_service.h"
#include "modules/task/presentation/results/detection_result_presentation.h"
#include "modules/task/ui/annotation/shared/annotation_panel_toggle.h"
#include "modules/task/ui/annotation/tools/annotation_tool_interaction.h"
#include "modules/task/ui/image/image_viewport_transform.h"
#include "modules/task/ui/samples/sample_browser.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace studio {

namespace {

using Box = jocelin::meta::BboxComp;

QColor class_color(const AnnotationClass& value) {
    const QColor color(QString::fromStdString(value.color));
    return color.isValid() ? color : QColor("#0EA67A");
}

} // namespace

DetectionAnnotationWidget::DetectionAnnotationWidget(QTabWidget* inspector, QWidget* parent)
    : QWidget(parent) {
    setObjectName("odAnnotationEditor");
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeTokens::Space1);
    QFrame* context_bar = new QFrame(this);
    context_bar->setObjectName("annotationContextBar");
    QHBoxLayout* context = new QHBoxLayout(context_bar);
    context->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                               ThemeTokens::Space2, ThemeTokens::Space1);
    context->setSpacing(ThemeTokens::Space1);
    previous_ = annotation_icon_button(context_bar, context, "odPreviousSample", "previous", tr("Previous sample"));
    next_ = annotation_icon_button(context_bar, context, "odNextSample", "next", tr("Next sample"));
    QToolButton* toggle_samples = make_annotation_panel_toggle(context_bar, tr("Samples"), "odToggleSamples");
    QToolButton* toggle_inspector = make_annotation_panel_toggle(context_bar, tr("Inspector"), "odToggleInspector");
    context->addWidget(toggle_samples);
    context->addWidget(toggle_inspector);
    sample_context_ = new QLabel(context_bar);
    sample_context_->setProperty("annotationContext", true);
    sample_context_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    context->addWidget(sample_context_, 1);
    QToolButton* zoom_out = annotation_icon_button(context_bar, context, "odZoomOut", "zoom-out", tr("Zoom out"));
    QLabel* zoom = new QLabel(QStringLiteral("100%"), context_bar);
    zoom->setAlignment(Qt::AlignCenter);
    zoom->setMinimumWidth(zoom->fontMetrics().horizontalAdvance(QStringLiteral("1000%")));
    context->addWidget(zoom);
    QToolButton* zoom_in = annotation_icon_button(context_bar, context, "odZoomIn", "zoom-in", tr("Zoom in"));
    QToolButton* fit = annotation_icon_button(context_bar, context, "odFitImage", "fit", tr("Fit image"));
    QToolButton* undo = annotation_icon_button(context_bar, context, "odUndo", "undo", tr("Undo"));
    QToolButton* redo = annotation_icon_button(context_bar, context, "odRedo", "redo", tr("Redo"));
    QToolButton* save = annotation_icon_button(context_bar, context, "odSaveBoxes", "save", tr("Save annotations"));
    layout->addWidget(context_bar);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    layout->addWidget(splitter, 1);
    QFrame* samples_panel = new QFrame(splitter);
    samples_panel->setObjectName("annotationSamplesPanel");
    QVBoxLayout* samples_layout = new QVBoxLayout(samples_panel);
    samples_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                                       ThemeTokens::Space2, ThemeTokens::Space2);
    samples_layout->setSpacing(ThemeTokens::Space1);
    QLabel* samples_title = new QLabel(tr("SAMPLES"), samples_panel);
    samples_title->setProperty("eyebrow", true);
    samples_layout->addWidget(samples_title);

    samples_ = new SampleBrowser(samples_panel);
    samples_->setObjectName("odAnnotationSamples");
    samples_->setAccessibleName(tr("Samples"));
    samples_->setIconSize(QSize(56, 42));
    samples_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    samples_layout->addWidget(samples_, 1);

    QFrame* center = new QFrame(splitter);
    center->setObjectName("annotationViewportPanel");
    QVBoxLayout* central = new QVBoxLayout(center);
    central->setContentsMargins(0, 0, 0, 0);
    central->setSpacing(0);
    QFrame* tools_panel = new QFrame(center);
    tools_panel->setObjectName("annotationToolBar");
    QHBoxLayout* tools = new QHBoxLayout(tools_panel);
    tools->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                              ThemeTokens::Space2, ThemeTokens::Space1);
    tools->setSpacing(ThemeTokens::Space1);
    QToolButton* select = annotation_icon_button(tools_panel, tools, "odSelectTool", "select", tr("Select box"));
    QToolButton* rectangle = annotation_icon_button(tools_panel, tools, "odRectangleTool", "rectangle", tr("Draw box (R)"));
    QButtonGroup* modes = new QButtonGroup(tools_panel);
    for (QToolButton* tool : {select, rectangle}) {
        tool->setCheckable(true);
        modes->addButton(tool);
    }
    rectangle->setChecked(true);
    QToolButton* remove = annotation_icon_button(tools_panel, tools, "odDeleteBox", "trash", tr("Delete selected box (Del)"));
    tools->addStretch();
    central->addWidget(tools_panel);

    QFrame* layers_panel = new QFrame(center);
    layers_panel->setObjectName("annotationLayerBar");
    QHBoxLayout* layers = new QHBoxLayout(layers_panel);
    layers->setContentsMargins(10, 5, 10, 5);
    layers->setSpacing(10);
    QCheckBox* annotations = new QCheckBox(tr("Annotation"), layers_panel);
    annotations->setObjectName("odShowAnnotations");
    annotations->setToolTip(tr("Show annotations"));
    QCheckBox* predictions = new QCheckBox(tr("Inference"), layers_panel);
    predictions->setObjectName("odShowPredictions");
    predictions->setToolTip(tr("Show predictions"));
    annotations->setChecked(true);
    predictions->setChecked(true);
    layers->addWidget(annotations);
    layers->addWidget(predictions);
    layers->addStretch();
    central->addWidget(layers_panel);

    canvas_ = new DetectionCanvas(center);
    central->addWidget(canvas_, 1);
    canvas_->request_class = [this] {
        const QListWidgetItem* selected = classes_->currentItem();
        if (selected && selected->isSelected()) {
            return selected->data(Qt::UserRole).toString().toStdString();
        }
        const std::string class_id = choose_class ? choose_class() : std::string{};
        if (!class_id.empty()) {
            refresh_classes();
        }
        return class_id;
    };

    splitter->addWidget(inspector);
    splitter->setCollapsible(1, false);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({210, 740, 360});
    connect(toggle_samples, &QToolButton::toggled, samples_panel, &QWidget::setVisible);
    connect(toggle_inspector, &QToolButton::toggled, inspector, &QWidget::setVisible);
    class_panel_ = new AnnotationClassesPanel(
        {"odClassList", "odAddClass", "odRenameClass", "odDeleteClass"}, inspector);
    classes_ = class_panel_->list();
    inspector->insertTab(0, class_panel_, tr("Classes"));
    result_details_ = annotation_results_tab(inspector, "odAnnotationResultDetails");

    const auto update_layers = [this, annotations, predictions] {
        canvas_->set_layers(annotations->isChecked(), predictions->isChecked());
    };
    canvas_->zoom_changed = [this, zoom] {
        zoom->setText(QStringLiteral("%1%").arg(canvas_->zoom_percent()));
    };
    canvas_->selection_changed = [this, remove] { remove->setEnabled(canvas_->selected_box()); };
    connect(select, &QToolButton::clicked, this, [this] { canvas_->set_tool(AnnotationTool::Pick); });
    connect(rectangle, &QToolButton::clicked, this, [this] { canvas_->set_tool(AnnotationTool::Rectangle); });
    connect(remove, &QToolButton::clicked, this, [this] { canvas_->remove_selected(); });
    connect(annotations, &QCheckBox::toggled, this, update_layers);
    connect(predictions, &QCheckBox::toggled, this, update_layers);
    connect(fit, &QToolButton::clicked, canvas_, &DetectionCanvas::fit);
    connect(zoom_in, &QToolButton::clicked, this, [this] {
        canvas_->zoom_at(canvas_->rect().center(), 1.2);
    });
    connect(zoom_out, &QToolButton::clicked, this, [this] {
        canvas_->zoom_at(canvas_->rect().center(), 1.0 / 1.2);
    });
    connect(previous_, &QToolButton::clicked, this, [this] { samples_->setCurrentRow(samples_->currentRow() - 1); });
    connect(next_, &QToolButton::clicked, this, [this] { samples_->setCurrentRow(samples_->currentRow() + 1); });
    connect(save, &QToolButton::clicked, this, [this] { save_current(); });

    QUndoStack* history = &canvas_->history;
    connect(undo, &QToolButton::clicked, this, [this, history] {
        canvas_->cancel_edit();
        history->undo();
    });
    connect(redo, &QToolButton::clicked, this, [this, history] {
        canvas_->cancel_edit();
        history->redo();
    });
    connect(history, &QUndoStack::canUndoChanged, undo, &QToolButton::setEnabled);
    connect(history, &QUndoStack::canRedoChanged, redo, &QToolButton::setEnabled);
    connect(history, &QUndoStack::cleanChanged, save, [save](bool clean) { save->setEnabled(!clean); });
    connect(history, &QUndoStack::indexChanged, canvas_, qOverload<>(&QWidget::update));
    undo->setEnabled(false);
    redo->setEnabled(false);
    save->setEnabled(false);
    remove->setEnabled(false);

    const auto shortcut = [this](const QKeySequence& key, const std::function<void()>& action) {
        QShortcut* binding = new QShortcut(key, canvas_);
        binding->setContext(Qt::WidgetShortcut);
        connect(binding, &QShortcut::activated, this, action);
    };
    shortcut(QKeySequence(Qt::Key_R), [rectangle] { rectangle->click(); });
    shortcut(QKeySequence::Delete, [remove] { remove->click(); });
    shortcut(QKeySequence::Undo, [undo] { undo->click(); });
    shortcut(QKeySequence::Redo, [redo] { redo->click(); });
    shortcut(QKeySequence(Qt::Key_Escape), [this] { canvas_->cancel_edit(); });

    samples_->current_requested = [this](int index, const QString&) {
        if (index == current_index_) {
            return true;
        }
        if (!confirm_transition()) {
            return false;
        }
        load_sample(index);
        return true;
    };
    samples_->split_requested = [this](const std::vector<std::string>& ids, const std::string& split) {
        if (sample_split_requested) {
            sample_split_requested(ids, split);
        }
    };
    samples_->extend_context_menu = [this](QMenu& menu, const std::vector<std::string>& ids) {
        if (!service_ || !canvas_->has_image() || !canvas_->boxes().empty() ||
            ids.size() != 1 || ids.front() != current_sample_id_) {
            return;
        }
        QAction* reviewed = menu.addAction(tr("Mark as reviewed with no objects"));
        reviewed->setObjectName("annotationSetAsEmptyAction");
        connect(reviewed, &QAction::triggered, this, [this] {
            service_->set_detection_boxes(current_sample_id_, {}, true);
            canvas_->history.setClean();
            refresh_sample_status(current_index_);
            Q_EMIT changed();
        });
    };

    class_panel_->add_class = [this](const QString& name) {
        const std::string id = service_->add_class(name.toStdString());
        refresh_classes(QString::fromStdString(id));
        Q_EMIT changed();
    };
    class_panel_->rename_class = [this](const std::string& id, const QString& name) {
        service_->rename_class(id, name.toStdString());
        refresh_classes(QString::fromStdString(id));
        Q_EMIT changed();
    };
    class_panel_->removal_message = [this](const std::string& id) {
        const AnnotationClass* value = find_annotation_class(service_->binding(), id);
        return tr("Delete “%1” and its boxes from all samples?").arg(QString::fromStdString(value->name));
    };
    class_panel_->remove_class = [this](const std::string& id) {
        if (!confirm_transition()) {
            return;
        }
        service_->remove_class(id);
        for (int index = 0; index < samples_->count(); ++index) {
            refresh_sample_status(samples_->item(index)->data(Qt::UserRole + 1).toInt());
        }
        refresh_classes();
        load_sample(current_index_);
        Q_EMIT changed();
    };
    connect(classes_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!service_) {
            return;
        }
        const std::string id = item->data(Qt::UserRole).toString().toStdString();
        const AnnotationClass* value = find_annotation_class(service_->binding(), id);
        const bool visible = item->checkState() == Qt::Checked;
        if (value && value->visible != visible) {
            service_->set_class_visible(id, visible);
            refresh_class_display();
            Q_EMIT changed();
        }
    });
    UiMetrics::apply_semantic_metrics(this);
}

void DetectionAnnotationWidget::set_service(TaskWorkspaceService* service) {
    if (service_ == service && !confirm_transition()) {
        return;
    }
    QString selected_class;
    const QListWidgetItem* selected = classes_->currentItem();
    if (service_ == service && selected && selected->isSelected()) {
        selected_class = selected->data(Qt::UserRole).toString();
    }
    if (service_ != service) {
        samples_->clear();
    }
    service_ = service;
    std::vector<SampleBrowserEntry> entries;
    if (service_ && service_->source_node_id() != 0) {
        const auto& samples = service_->binding().samples;
        for (int index = 0; index < static_cast<int>(samples.size()); ++index) {
            const TaskSampleBinding& sample = samples[static_cast<size_t>(index)];
            if (annotation_sample_visible(sample)) {
                entries.push_back(annotation_sample_entry(sample, index, index));
            }
        }
    }
    samples_->set_entries(entries);
    refresh_classes(selected_class);
    load_sample(samples_->current_source_index());
}

void DetectionAnnotationWidget::load_sample(int index) {
    current_index_ = index;
    previous_->setEnabled(samples_->currentRow() > 0);
    next_->setEnabled(samples_->currentRow() >= 0 && samples_->currentRow() + 1 < samples_->count());
    if (index < 0) {
        current_sample_id_.clear();
        sample_context_->clear();
        sample_context_->setToolTip({});
        canvas_->empty_text = tr("Select a sample");
        canvas_->load({}, {});
    } else {
        const TaskSampleBinding& sample = service_->binding().samples[static_cast<size_t>(index)];
        const SampleBrowserEntry entry = annotation_sample_entry(sample, index, index);
        current_sample_id_ = sample.sample_id;
        sample_context_->setText(entry.display_name);
        sample_context_->setToolTip(entry.tooltip);
        if (sample.image_paths.size() != 1) {
            canvas_->empty_text = tr("OD requires one image per sample");
            canvas_->load({}, {});
        } else {
            QImageReader reader(entry.image_path);
            reader.setAutoTransform(true);
            const QImage image = reader.read();
            canvas_->empty_text = tr("Cannot read image");
            canvas_->load(image, sample.bbox_comps);
        }
    }
    refresh_prediction();
}

void DetectionAnnotationWidget::refresh_sample_status(int index) {
    const SampleBrowserEntry entry = annotation_sample_entry(service_->binding().samples[static_cast<size_t>(index)], index, index);
    samples_->update_entry_state(entry.source_index, entry.display_name, entry.annotation_state, entry.split_state);
}

void DetectionAnnotationWidget::refresh_classes(const QString& selected) {
    classes_->clear();
    class_panel_->set_available(service_ != nullptr);
    refresh_class_display();
    if (!service_) {
        return;
    }

    QListWidgetItem* current = nullptr;
    {
        const QSignalBlocker blocker(classes_);
        for (const AnnotationClass& value : service_->binding().classes) {
            const QString id = QString::fromStdString(value.id);
            QListWidgetItem* item = new QListWidgetItem(
                annotation_class_icon(class_color(value)), QString::fromStdString(value.name), classes_);
            item->setData(Qt::UserRole, id);
            item->setData(AnnotationClassesPanel::NameRole, QString::fromStdString(value.name));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(value.visible ? Qt::Checked : Qt::Unchecked);
            if (selected == id) {
                current = item;
            }
        }
    }
    classes_->setCurrentItem(current);
}

void DetectionAnnotationWidget::refresh_class_display() {
    canvas_->set_classes(service_ ? service_->binding().classes : std::vector<AnnotationClass>{});
}

void DetectionAnnotationWidget::refresh_prediction() {
    const InferenceResultSet* result = service_ ? service_->selected_inference_result() : nullptr;
    const jocelin::model::ModelEntry* model = service_ ? service_->find_model(service_->selected_model_id()) : nullptr;
    if (!current_sample_id_.empty() && result && model) {
        const auto record = std::find_if(result->samples.begin(), result->samples.end(),
            [this](const ResultReference& value) { return value.source_sample_id == current_sample_id_; });
        if (record != result->samples.end()) {
            if (const auto* detection = std::get_if<jocelin::result::od::OdResult>(&record->result)) {
                canvas_->set_prediction(detection, model->classes);
                result_details_->setText(detection_result_summary(*detection));
                return;
            }
        }
    }
    canvas_->set_prediction(nullptr, {});
    result_details_->setText(tr("No inference result for this image."));
}

bool DetectionAnnotationWidget::save_current() {
    if (!service_ || current_index_ < 0 || canvas_->history.isClean()) {
        return true;
    }
    try {
        service_->set_detection_boxes(current_sample_id_, canvas_->boxes());
        canvas_->history.setClean();
        refresh_sample_status(current_index_);
        Q_EMIT changed();
        return true;
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Cannot save boxes"), QString::fromUtf8(error.what()));
        return false;
    }
}

bool DetectionAnnotationWidget::confirm_transition() {
    return studio::confirm_transition(this, !canvas_->history.isClean(), annotation_auto_save(),
        [this] { return save_current(); },
        [this] { load_sample(current_index_); });
}

} // namespace studio
