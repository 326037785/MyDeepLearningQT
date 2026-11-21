#include "modules/task/ui/annotation/shared/annotation_class_dialog.h"
#include "modules/task/ui/annotation/shared/annotation_panels.h"
#include "modules/task/ui/task_workspace.h"

#include "modules/task/ui/annotation/cls/classification_annotation.h"
#include "modules/task/ui/annotation/od/detection_annotation.h"
#include "common/ui/icon_provider.h"
#include "modules/task/ui/annotation/sg/segmentation_annotation.h"
#include "modules/task/ui/annotation/sg/segmentation_classes.h"
#include "modules/task/application/task_dataset_binding.h"
#include "modules/task/ui/pages/dataset_selection_view.h"
#include "modules/task/ui/pages/dataset_split_view.h"
#include "modules/task/ui/pages/training_workspace.h"
#include "modules/task/ui/pages/inference_workspace.h"
#include "modules/task/ui/pages/results_workspace.h"
#include "modules/task/ui/models/project_model_browser.h"
#include "modules/task/application/catalog/task_module_catalog.h"
#include "common/ui/ui_metrics.h"

#include <QComboBox>
#include <QColorDialog>
#include <QCursor>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <stdexcept>

namespace studio {

TaskWorkspace::TaskWorkspace(QWidget* parent) : QWidget(parent) {
    setObjectName("taskWorkspace");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new QFrame(this);
    header->setObjectName("taskWorkspaceHeader");
    auto* header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(ThemeTokens::Space2, 2,
                                      ThemeTokens::Space2, 0);
    header_layout->setSpacing(0);

    auto* identity_row = new QHBoxLayout;
    identity_row->setSpacing(ThemeTokens::Space1);
    auto* back = new QToolButton(header);
    back->setObjectName("taskWorkspaceBackButton");
    IconProvider::configure(back, "previous", tr("Back to Workflow"), 16);
    identity_ = new QLabel(tr("Task"), header);
    identity_->setObjectName("taskWorkspaceIdentity");
    identity_->setProperty("workspaceTitle", true);
    context_ = new QLabel(header);
    context_->setObjectName("taskWorkspaceContext");
    context_->setProperty("workspacePath", true);
    identity_row->addWidget(back);
    identity_row->addWidget(identity_);
    identity_row->addWidget(context_, 1);
    header_layout->addLayout(identity_row);

    auto* navigation = new QHBoxLayout;
    navigation->setContentsMargins(ThemeTokens::Space2, 0, ThemeTokens::Space2, 0);
    navigation->setSpacing(0);
    const QStringList names = {
        tr("1  Data"), tr("2  Annotation"), tr("3  Split"),
        tr("4  Training"), tr("5  Inference"), tr("6  Results")};
    for (int index = 0; index < names.size(); ++index) {
        auto* button = new QPushButton(names[index], header);
        button->setObjectName(QString("workspaceStep%1Button").arg(index + 1));
        button->setProperty("stepNav", true);
        button->setProperty("compact", true);
        navigation->addWidget(button, 1);
        step_buttons_[static_cast<size_t>(index)] = button;
        connect(button, &QPushButton::clicked, this, [this, index] {
            activate_step(static_cast<Step>(index));
        });
    }
    header_layout->addLayout(navigation);
    root->addWidget(header);

    pages_ = new QStackedWidget(this);
    pages_->setObjectName("taskStepPages");
    data_ = new DatasetSelectionView(pages_);
    pages_->addWidget(data_);
    pages_->addWidget(build_annotation_page());
    split_ = new DatasetSplitView(pages_);
    pages_->addWidget(split_);
    training_ = new TrainingWorkspace(pages_);
    pages_->addWidget(training_);
    inference_ = new InferenceWorkspace(pages_);
    pages_->addWidget(inference_);
    results_ = new ResultsWorkspace(pages_);
    pages_->addWidget(results_);
    root->addWidget(pages_, 1);

    connect(back, &QToolButton::clicked, this, [this] {
        if (back_requested && confirm_annotation_transition()) {
            back_requested();
        }
    });
    data_->changed = [this] {
        annotation_binding_dirty_ = true;
        domain_changed();
    };
    data_->open_dataset_requested = [this](int node_id) {
        if (dataset_requested) dataset_requested(node_id);
    };
    data_->dataset_replace_requested = [this](int source, DatasetConfig dataset, int owner, const TaskDatasetBinding* imported) {
        if (!dataset_replace_requested) {
            throw std::runtime_error("Dataset replacement handler unavailable");
        }
        const bool replaced = dataset_replace_requested(source, std::move(dataset), owner, imported);
        if (replaced && imported) {
            // The accepted import has replaced the Task labels and targets.
            segmentation_->canvas()->clear();
        }
        return replaced;
    };
    split_->changed = [this] {
        if (service_) {
            inference_->refresh();
        }
        domain_changed();
    };
    split_->status = [this](const QString& text) { if (status) status(text); };
    training_->changed = [this] { domain_changed(); };
    training_->start_requested = [this](const TrainingLaunchRequest& request) {
        if (train_requested) train_requested(node_id_, request);
    };
    training_->stop_requested = [this] {
        if (stop_training_requested) stop_training_requested();
    };
    training_->pause_requested = [this] {
        if (pause_training_requested) pause_training_requested();
    };
    training_->run_details_requested = [this](const QString& run_id) {
        if (run_details_requested) run_details_requested(run_id);
    };
    training_->import_model_requested = [this] {
        if (import_model_requested) import_model_requested(node_id_);
    };
    training_->export_model_requested = [this](const QString& model_id) {
        if (export_model_requested) export_model_requested(node_id_, model_id);
    };
    training_->delete_models_requested = [this](const QStringList& model_ids) {
        if (delete_models_requested) delete_models_requested(node_id_, model_ids);
    };
    training_->delete_model_weights_requested = [this](const ModelWeightSelection& weights) {
        if (delete_model_weights_requested) {
            delete_model_weights_requested(node_id_, weights);
        }
    };
    training_->delete_model_weight_requested = [this](const QString& model_id, const QString& weight_id) {
        if (delete_model_weight_requested) {
            delete_model_weight_requested(node_id_, model_id, weight_id);
        }
    };
    training_->clear_models_requested = [this] {
        if (clear_models_requested) clear_models_requested(node_id_);
    };
    inference_->import_model_requested = [this] {
        if (import_model_requested) import_model_requested(node_id_);
    };
    inference_->export_model_requested = [this](const QString& model_id) {
        if (export_model_requested) export_model_requested(node_id_, model_id);
    };
    inference_->delete_models_requested = [this](const QStringList& model_ids) {
        if (delete_models_requested) delete_models_requested(node_id_, model_ids);
    };
    inference_->delete_model_weights_requested = [this](const ModelWeightSelection& weights) {
        if (delete_model_weights_requested) {
            delete_model_weights_requested(node_id_, weights);
        }
    };
    inference_->delete_model_weight_requested = [this](const QString& model_id, const QString& weight_id) {
        if (delete_model_weight_requested) {
            delete_model_weight_requested(node_id_, model_id, weight_id);
        }
    };
    inference_->clear_models_requested = [this] {
        if (clear_models_requested) clear_models_requested(node_id_);
    };
    inference_->changed = [this] { domain_changed(); };
    inference_->start_requested = [this](const InferenceLaunchRequest& request) {
        if (infer_requested) infer_requested(node_id_, request);
    };
    inference_->convert_weight_requested = [this](const QString& model_id, const QString& weight_id,
                                                    jocelin::config::WeightType target) {
        if (convert_weight_requested) {
            convert_weight_requested(node_id_, model_id, weight_id, target);
        }
    };
    UiMetrics::apply_semantic_metrics(this);
    activate_step(Step::Data);
}

QWidget* TaskWorkspace::build_annotation_page() {
    auto* page = new QWidget(pages_);
    page->setObjectName("taskAnnotationPage");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                               ThemeTokens::Space2, ThemeTokens::Space2);
    layout->setSpacing(0);
    segmentation_inspector_ = new QTabWidget(page);
    classification_inspector_ = new QTabWidget(page);
    detection_inspector_ = new QTabWidget(page);
    segmentation_ = new SegmentationAnnotationWidget(page, segmentation_inspector_);
    classification_ = new ClassificationAnnotationWidget(page, classification_inspector_);
    detection_annotation_ = new DetectionAnnotationWidget(detection_inspector_, page);
    detection_annotation_->choose_class = [this] { return choose_annotation_class(); };
    build_segmentation_classes();
    QListWidget* classification_classes = classification_inspector_->findChild<QListWidget*>("classificationClassList");
    QListWidget* detection_classes = detection_inspector_->findChild<QListWidget*>("odClassList");
    for (QListWidget* list : {segmentation_classes_, classification_classes, detection_classes}) {
        const std::shared_ptr<bool> current_before_press = std::make_shared<bool>(true);
        const std::shared_ptr<bool> pressed_current_class = std::make_shared<bool>(false);
        connect(list, &QListWidget::currentItemChanged, this,
                [current_before_press](QListWidgetItem* current, QListWidgetItem*) {
            // Mouse presses change the current item before Qt updates the selection.
            *current_before_press = !current || current->isSelected();
        });
        connect(list, &QListWidget::itemPressed, this,
                [current_before_press, pressed_current_class](QListWidgetItem*) {
            *pressed_current_class = *current_before_press;
            *current_before_press = true;
        });
        const auto class_clicked = [this, list, detection_classes, pressed_current_class](QListWidgetItem* item, bool double_click) {
            if (!service_ || !item) {
                return;
            }
            QStyleOptionViewItem option;
            option.initFrom(list);
            option.rect = list->visualItemRect(item);
            option.decorationSize = list->iconSize();
            option.features = QStyleOptionViewItem::HasDecoration | QStyleOptionViewItem::HasDisplay;
            option.text = item->text();
            if (item->flags().testFlag(Qt::ItemIsUserCheckable)) {
                option.features |= QStyleOptionViewItem::HasCheckIndicator;
                option.checkState = item->checkState();
            }
            const QRect color_rect = list->style()->subElementRect(
                QStyle::SE_ItemViewItemDecoration, &option, list);
            const QPoint clicked_position = list->viewport()->mapFromGlobal(QCursor::pos());
            if (!color_rect.contains(clicked_position)) {
                const QRect check_rect = list->style()->subElementRect(
                    QStyle::SE_ItemViewItemCheckIndicator, &option, list);
                const bool selectable_class = list != segmentation_classes_ || list->row(item) > 0;
                if ((double_click || *pressed_current_class) && selectable_class &&
                    item == list->currentItem() &&
                    !check_rect.contains(clicked_position)) {
                    list->setCurrentRow(list == segmentation_classes_ ? 0 : -1);
                }
                return;
            }
            if (double_click) {
                return;
            }
            const std::string id = item->data(Qt::UserRole).toString().toStdString();
            const AnnotationClass* schema = find_annotation_class(service_->binding(), id);
            if (!schema) {
                return;
            }
            const QColor previous(QString::fromStdString(schema->color));
            const QColor selected = QColorDialog::getColor(previous, this, tr("Choose class color"),
                                                            QColorDialog::DontUseNativeDialog);
            if (!selected.isValid() || selected == previous || !service_) {
                return;
            }
            service_->set_class_color(id, selected.name().toStdString());
            if (list == segmentation_classes_) {
                QTimer::singleShot(0, this, [this, id] { refresh_segmentation_classes(id); });
            } else {

                const QSignalBlocker blocker(list);
                item->setIcon(annotation_class_icon(selected));
                if (list == detection_classes) {
                    detection_annotation_->refresh_class_display();
                }
            }
            refresh_inference_result();
            if (inference_) {
                inference_->synchronize_result_selection();
            }
            domain_changed();
        };
        connect(list, &QListWidget::itemClicked, this,
                [class_clicked](QListWidgetItem* item) { class_clicked(item, false); });
        connect(list, &QListWidget::itemDoubleClicked, this,
                [class_clicked](QListWidgetItem* item) { class_clicked(item, true); });
    }
    annotation_models_tab_ = new QWidget(page);
    QVBoxLayout* models_layout = new QVBoxLayout(annotation_models_tab_);
    models_layout->setContentsMargins(8, 8, 8, 8);
    annotation_models_ = new ProjectModelBrowser(annotation_models_tab_);
    annotation_models_->setObjectName("annotationProjectModelBrowser");
    annotation_models_->set_management_actions_enabled(false);
    annotation_models_->set_detail_visible(false);
    annotation_models_->selection_changed = [this](const QString& model_id) {
        if (service_) {
            service_->select_inference_model(model_id);
        }
    };
    annotation_models_->weight_branch_selection_changed = [this](const QString& model_id, const QString& weight_id) {
        if (!service_) {
            return;
        }
        if (weight_id.isEmpty()) {
            service_->select_inference_model(model_id);
        } else {
            service_->select_inference_result(model_id, weight_id);
        }
    };
    annotation_models_->set_weight_selection_mode(true);
    models_layout->addWidget(annotation_models_, 1);
    annotation_weight_branch_ = new QComboBox(annotation_models_tab_);
    annotation_weight_branch_->setObjectName("annotationWeightBranch");
    annotation_weight_branch_->setAccessibleName(tr("Weight branch"));
    annotation_weight_branch_->setPlaceholderText(tr("No model selected"));
    annotation_weight_branch_->setEnabled(false);
    models_layout->addWidget(annotation_weight_branch_);
    connect(annotation_weight_branch_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (service_ && index >= 0) {
            service_->select_inference_result(service_->selected_model_id(),
                annotation_weight_branch_->itemData(index).toString());
        }
    });
    segmentation_inspector_->insertTab(1, annotation_models_tab_, tr("Models"));
    segmentation_->hide();
    classification_->hide();
    layout->addWidget(segmentation_, 1);
    layout->addWidget(classification_, 1);
    detection_annotation_->hide();
    layout->addWidget(detection_annotation_, 1);
    connect(detection_annotation_, &DetectionAnnotationWidget::changed, this, [this] {
        domain_changed();
    });

    segmentation_->status = [this](const QString& text) {
        if (status) status(text);
    };
    segmentation_->save_mask = [this](const std::string& sample_id, const QImage& mask) {
        if (!service_) {
            return false;
        }
        try {
            service_->set_segmentation_mask(sample_id, mask);
            segmentation_->refresh_samples(service_->binding().samples, {});
            domain_changed();
            return true;
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Mask metadata failed"),
                                 QString::fromUtf8(error.what()));
            return false;
        }
    };
    auto assign_sample_split = [this](
        const std::vector<std::string>& sample_ids, const std::string& split) {
        if (!service_ || !confirm_annotation_transition()) {
            return;
        }
        for (const auto& node_id : sample_ids) service_->set_sample_split(node_id, split);
        refresh_annotation();
        if (split_) split_->refresh();
        domain_changed();
    };
    segmentation_->samples_empty_requested = [this](const std::vector<std::string>& ids) {
        if (!service_) return;
        try {
            for (const auto& node_id : ids) service_->set_segmentation_sample_empty(node_id);
            segmentation_->refresh_samples(service_->binding().samples, ids);
            domain_changed();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Cannot set empty target"), QString::fromUtf8(error.what()));
        }
    };
    segmentation_->sample_split_requested = assign_sample_split;
    classification_->sample_split_requested = assign_sample_split;
    detection_annotation_->sample_split_requested = assign_sample_split;
    classification_->class_assigned = [this](const std::vector<std::string>& sample_ids, int class_index) {
        if (!service_ || class_index < 0 ||
            class_index >= static_cast<int>(service_->binding().classes.size())) {
            return;
        }
        const std::string class_id = service_->binding().classes[static_cast<size_t>(class_index)].id;
        for (const std::string& sample_id : sample_ids) {
            service_->set_class_for_sample(sample_id, class_id);
        }
        domain_changed();
    };
    classification_->class_add_requested = [this](const QString& name) -> std::vector<AnnotationClass> {
        if (!service_) {
            return {};
        }
        try {
            service_->add_class(name.toStdString());
            domain_changed();
            return service_->binding().classes;
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Cannot add class"),
                                 QString::fromUtf8(error.what()));
            return {};
        }
    };
    return page;
}

void TaskWorkspace::build_segmentation_classes() {
    segmentation_class_editor_ = new SegmentationClasses(segmentation_inspector_, segmentation_);
    segmentation_classes_ = segmentation_class_editor_->list();
    segmentation_class_editor_->choose_class = [this] { return choose_annotation_class(); };
    segmentation_class_editor_->changed = [this] { domain_changed(); };
}

void TaskWorkspace::refresh_segmentation_classes(const std::string& class_id) {
    segmentation_class_editor_->set_service(service_.get());
    segmentation_class_editor_->refresh(class_id);
}

const AnnotationClass* TaskWorkspace::selected_segmentation_class() const {
    return segmentation_class_editor_->selected_class();
}

std::string TaskWorkspace::add_annotation_class(const QString& name) {
    try {
        const std::string id = service_->add_class(name.toStdString());
        if (service_->task().task == "sg") {
            const AnnotationClass* selected = selected_segmentation_class();
            refresh_segmentation_classes(selected ? selected->id : std::string{});
        }
        domain_changed();
        return id;
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Cannot add class"), QString::fromUtf8(error.what()));
        return {};
    }
}

std::string TaskWorkspace::choose_annotation_class() {
    if (!service_) {
        return {};
    }
    return studio::choose_annotation_class(this, service_->binding().classes,
        [this](const QString& name) { return add_annotation_class(name); });
}

void TaskWorkspace::bind(
    WorkflowGraph* graph, int node_id)
{
    graph_ = graph;
    node_id_ = node_id;
    segmentation_->canvas()->clear();
    {
        const QSignalBlocker blocker(segmentation_classes_);
        segmentation_classes_->setCurrentRow(0);
    }
    training_->set_service(nullptr);
    inference_->set_service(nullptr);
    results_->set_service(nullptr);
    detection_annotation_->set_service(nullptr);
    segmentation_class_editor_->set_service(nullptr);
    service_.reset();
    if (!graph_ || graph_->nodes().count(node_id_) == 0 ||
        graph_->node(node_id_).kind != NodeKind::TaskConfig) {
        clear();
        return;
    }
    try {
        service_ = std::make_unique<TaskWorkspaceService>(*graph_, node_id_);
        service_->inference_selection_changed = [this] {
            inference_->synchronize_result_selection();
            results_->refresh();
            refresh_inference_result();
        };
        service_->synchronize();
        refresh();
        activate_step(Step::Data);
    } catch (const std::exception& error) {
        context_->setText(QString::fromUtf8(error.what()));
        refresh_step_availability();
    }
}

void TaskWorkspace::refresh_models() {
    const auto models = service_ ? service_->models() : std::vector<jocelin::model::ModelEntry>{};
    annotation_models_->set_models(models);
    if (training_) training_->set_models(models);
    if (inference_) inference_->set_models(models);
    if (results_) results_->set_models(models);
    if (service_) {
        const auto* selected = service_->find_model(service_->selected_model_id());
        if (!selected) {
            service_->select_inference_model(models.empty() ? QString{} : QString::fromStdString(models.front().id));
        } else if (!jocelin::model::find_weight(*selected, service_->selected_weight_id().toStdString())) {
            service_->select_inference_model(service_->selected_model_id());
        }
    }
    if (inference_) inference_->synchronize_result_selection();
    if (results_) results_->refresh();
    refresh_inference_result();
}

void TaskWorkspace::clear() {
    detection_annotation_->set_service(nullptr);
    segmentation_class_editor_->set_service(nullptr);
    service_.reset();
    graph_ = nullptr;
    node_id_ = 0;
    annotation_binding_dirty_ = true;
    segmentation_->canvas()->clear();
    refresh_segmentation_classes({});
    identity_->setText(tr("Task"));
    context_->clear();
    data_->set_service(nullptr);
    split_->set_service(nullptr);
    training_->set_service(nullptr);
    training_->set_models({});
    inference_->set_service(nullptr);
    inference_->set_models({});
    results_->set_service(nullptr);
    results_->set_models({});
    annotation_models_->set_models({});
    detection_annotation_->hide();
    refresh_inference_result();
    refresh_step_availability();
}

void TaskWorkspace::refresh_annotation() {
    if (!service_) {
        segmentation_->set_task_dataset({});
        classification_->set_dataset({}, {}, {});
        segmentation_->set_inference_result(nullptr);
        classification_->set_inference_result(nullptr);
        return;
    }
    const TaskNodeConfig& task = service_->task();
    const bool detection = task.task == "od";
    detection_annotation_->setVisible(detection);
    const bool segmentation = task.task == "sg";
    QTabWidget* inspector = detection ? detection_inspector_ :
        segmentation ? segmentation_inspector_ : classification_inspector_;
    for (QTabWidget* other : {segmentation_inspector_, classification_inspector_, detection_inspector_}) {
        const int previous_tab = other->indexOf(annotation_models_tab_);
        if (other != inspector && previous_tab >= 0) {
            other->removeTab(previous_tab);
        }
    }
    if (inspector->indexOf(annotation_models_tab_) < 0) {
        inspector->insertTab(1, annotation_models_tab_, tr("Models"));
    }
    segmentation_->setVisible(segmentation);
    classification_->setVisible(!detection && !segmentation);
    if (detection) {
        detection_annotation_->set_service(service_.get());
    } else if (segmentation) {
        const AnnotationClass* selected = selected_segmentation_class();
        refresh_segmentation_classes(selected ? selected->id : std::string{});
        if (service_->source_node_id() != 0) {
            segmentation_->set_task_dataset(service_->binding().samples);
        } else {
            segmentation_->set_task_dataset({});
        }
    } else if (service_->source_node_id() == 0) {
        classification_->set_dataset({}, {}, {});
    } else {
        QVector<int> labels;
        QStringList class_names;
        const std::vector<AnnotationClass>& classes = service_->binding().classes;
        for (const AnnotationClass& value : classes) {
            class_names.push_back(QString::fromStdString(value.name));
        }
        for (const auto& sample : service_->binding().samples) {
            int label = -1;
            for (int index = 0; index < static_cast<int>(classes.size()); ++index) {
                if (classes[static_cast<size_t>(index)].id == sample.class_id) {
                    label = index;
                    break;
                }
            }
            labels.push_back(label);
        }
        classification_->set_dataset(
            service_->binding().samples,
            class_names,
            labels,
            classes);
    }
    annotation_binding_dirty_ = false;
    refresh_inference_result();
}

void TaskWorkspace::refresh_inference_result() {
    const jocelin::model::ModelEntry* model = service_ ? service_->find_model(service_->selected_model_id()) : nullptr;
    const QSignalBlocker branch_blocker(annotation_weight_branch_);
    annotation_weight_branch_->clear();
    annotation_weight_branch_->setPlaceholderText(model ? tr("No weight selected") : tr("No model selected"));
    if (model) {
        for (const jocelin::model::ModelWeight& branch : model->weights) {
            const QString id = QString::fromStdString(branch.id);
            annotation_weight_branch_->addItem(id, id);
        }
        const QString weight_id = service_->selected_weight_id();
        annotation_weight_branch_->setCurrentIndex(annotation_weight_branch_->findData(weight_id));
        const jocelin::model::ModelWeight* weight = jocelin::model::find_weight(*model, weight_id.toStdString());
        if (weight) {
            annotation_models_->set_current_weight(service_->selected_model_id(), weight->format, weight_id);
        } else {
            annotation_models_->set_current_model_id(service_->selected_model_id());
        }
    }
    annotation_weight_branch_->setEnabled(annotation_weight_branch_->count() > 0);
    if (service_ && service_->task().task == "od") {
        detection_annotation_->refresh_prediction();
        return;
    }
    const InferenceResultSet* selected = service_ && service_->source_node_id() != 0 ? service_->selected_inference_result() : nullptr;
    QStringList classes;
    QHash<int, QString> segmentation_class_names;
    if (model) {
        for (const jocelin::model::ModelClass& value : model->classes) {
            const QString name = QString::fromStdString(value.name);
            classes.push_back(name);
            segmentation_class_names.insert(value.mask_value, name);
        }
    }
    if (model && service_) {
        segmentation_->set_inference_result(selected, segmentation_class_names,
                                            model->classes, service_->binding().classes);
    } else {
        segmentation_->set_inference_result(selected, segmentation_class_names);
    }
    classification_->set_inference_result(selected, classes);
}

void TaskWorkspace::refresh() {
    if (!service_) return;
    try {
        service_->synchronize();
        const auto* module = find_task_module(service_->task().task);
        identity_->setText(
            module ? QString::fromLatin1(module->id).toUpper() : tr("TASK"));
        context_->clear();
        context_->hide();
        data_->set_service(service_.get());
        split_->set_service(service_.get());
        training_->set_service(service_.get());
        inference_->set_service(service_.get());
        results_->set_service(service_.get());
        refresh_models();
        refresh_annotation();
        refresh_step_availability();
    } catch (const std::exception& error) {
        context_->setText(QString::fromUtf8(error.what()));
        context_->show();
        refresh_step_availability();
    }
}

void TaskWorkspace::refresh_step_availability() {
    const bool bound = service_ && service_->source_node_id() != 0;
    const bool has_samples = bound && !service_->binding().samples.empty();
    const bool manage_classes = service_ && service_->task().task == "sg";
    const bool detection = service_ && service_->task().task == "od";
    for (int index = 0; index < static_cast<int>(step_buttons_.size()); ++index) {
        QPushButton* button = step_buttons_[static_cast<size_t>(index)];
        if (!button) {
            continue;
        }
        const bool enabled = index == static_cast<int>(Step::Data) ||
            (index == static_cast<int>(Step::Annotation) && (manage_classes || detection)) ||
            (index == static_cast<int>(Step::Training) && bound) ||
            (index == static_cast<int>(Step::Inference) && bound) ||
            (index == static_cast<int>(Step::Results) && bound) ||
            has_samples;
        button->setEnabled(enabled && (!runtime_busy_ || index == static_cast<int>(active_step_)));
        button->setToolTip(enabled ? QString{} :
            !bound ? tr("Connect one Dataset node to this Task first.")
                   : tr("Import at least one valid sample in the connected Dataset."));
    }
}

void TaskWorkspace::activate_step(Step step) {
    const int index = static_cast<int>(step);
    if (index < 0 || index >= static_cast<int>(step_buttons_.size())) return;
    auto* selected_button = step_buttons_[static_cast<size_t>(index)];
    if (!selected_button || !selected_button->isEnabled()) return;
    if (step != Step::Annotation && !confirm_annotation_transition()) {
        return;
    }
    if (step == Step::Data && active_step_ == Step::Annotation && service_) {
        data_->refresh();
    }
    active_step_ = step;
    pages_->setCurrentIndex(index);
    for (int item = 0; item < static_cast<int>(step_buttons_.size()); ++item) {
        auto* button = step_buttons_[static_cast<size_t>(item)];
        if (!button) continue;
        button->setProperty("active", item == index);
        button->setProperty("done", item < index);
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
    if (step == Step::Annotation && annotation_binding_dirty_) {
        // The Data page may replace the complete Task binding through an
        // explicit annotation-package import. Rebind the editor on entry so
        // it cannot retain its pre-import snapshot, while keeping the import
        // action itself responsive.
        refresh_annotation();
    }
    if (step == Step::Annotation && service_ && service_->task().task == "sg") {
        segmentation_->refresh_inference_layers();
    } else if (step == Step::Annotation && service_ && service_->task().task == "cls") {
        classification_->refresh_inference_result();
    }
    if (step == Step::Split && split_) {
        split_->refresh();
    }
    if (step == Step::Inference && inference_) {
        inference_->refresh();
    }
    if (step == Step::Training && training_) training_->refresh();
    if (step == Step::Results && results_) results_->refresh();
}

bool TaskWorkspace::confirm_annotation_transition() {
    if (active_step_ != Step::Annotation || !service_) {
        return true;
    }
    if (service_->task().task == "sg") {
        return segmentation_->confirm_transition();
    }
    if (service_->task().task == "od") {
        return detection_annotation_->confirm_transition();
    }
    return true;
}

void TaskWorkspace::domain_changed() {
    if (changed) changed();
    refresh_step_availability();
    results_->refresh();
}

void TaskWorkspace::set_runtime_busy(bool busy) {
    runtime_busy_ = busy;
    bool training_busy = false;
    if (busy && service_) {
        const std::string& model_id = service_->task().runtime.training_model_id;
        const auto model = std::find_if(
            service_->models().begin(), service_->models().end(),
            [&model_id](const jocelin::model::ModelEntry& value) { return value.id == model_id; });
        training_busy = model != service_->models().end() &&
            model->training_status == jocelin::model::ModelTrainingStatus::Running;
    }
    const bool inference_busy =
        busy && service_ &&
        service_->task().runtime.inference_status == "running";
    training_->set_busy(training_busy);
    inference_->set_busy(inference_busy);
    refresh_step_availability();
}

void TaskWorkspace::training_started() {
    activate_step(Step::Training);
    training_->begin();
    set_runtime_busy(true);
    if (changed) changed();
}

void TaskWorkspace::update_training_progress(
    const jocelin::train::TrainingProgress& progress)
{
    training_->update_progress(progress);
}

void TaskWorkspace::finish_training(
    const jocelin::training::TrainingResult& result,
    const QString& error)
{
    set_runtime_busy(false);
    training_->finish(result, error);
    results_->refresh();
    if (changed) changed();
}

void TaskWorkspace::inference_started() {
    activate_step(Step::Inference);
    inference_->begin();
    set_runtime_busy(true);
    if (changed) changed();
}

void TaskWorkspace::update_inference_progress(
    size_t done, size_t total, const QString& image)
{
    inference_->update_progress(done, total, image);
}

void TaskWorkspace::finish_inference(const QString& error)
{
    set_runtime_busy(false);
    inference_->finish(error);
    refresh_annotation();
    results_->refresh();
    if (error.isEmpty()) activate_step(Step::Annotation);
    if (changed) changed();
}

} // namespace studio
