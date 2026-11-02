#include "task_workspace.h"

#include "classification_annotation.h"
#include "icon_provider.h"
#include "segmentation_annotation.h"
#include "task_workspace_pages.h"
#include "project_model_browser.h"
#include "task_module_catalog.h"
#include "ui_metrics.h"

#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <stdexcept>

namespace studio {

TaskWorkspace::TaskWorkspace(QWidget* parent) : QWidget(parent) {
    setObjectName("taskWorkspace");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(ThemeTokens::Space1);

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

    connect(back, &QToolButton::clicked, this,
            [this] { if (back_requested) back_requested(); });
    data_->changed = [this] {
        annotation_binding_dirty_ = true;
        domain_changed();
    };
    data_->open_dataset_requested = [this](int node_id) {
        if (dataset_requested) dataset_requested(node_id);
    };
    data_->dataset_replace_requested = [this](int source, DatasetConfig dataset, int owner, const TaskDatasetBinding* imported) {
        if (!dataset_replace_requested) throw std::runtime_error("Dataset replacement handler unavailable");
        return dataset_replace_requested(source, std::move(dataset), owner, imported);
    };
    split_->changed = [this] { domain_changed(); };
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
    UiMetrics::apply_semantic_metrics(this);
    activate_step(Step::Data);
}

QWidget* TaskWorkspace::build_annotation_page() {
    auto* page = new QWidget(pages_);
    page->setObjectName("taskAnnotationPage");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                               ThemeTokens::Space2, ThemeTokens::Space2);
    layout->setSpacing(0);
    segmentation_inspector_ = new QTabWidget(page);
    classification_inspector_ = new QTabWidget(page);
    segmentation_ = new SegmentationAnnotationWidget(page, segmentation_inspector_);
    classification_ = new ClassificationAnnotationWidget(page, classification_inspector_);
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
        if (service_) {
            service_->select_inference_result(model_id, weight_id);
        }
    };
    annotation_models_->set_weight_selection_mode(true);
    models_layout->addWidget(annotation_models_, 1);
    annotation_selection_ = new QLabel(tr("No model selected"), annotation_models_tab_);
    annotation_selection_->setObjectName("annotationModelSelection");
    annotation_selection_->setWordWrap(true);
    annotation_selection_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    models_layout->addWidget(annotation_selection_);
    segmentation_inspector_->insertTab(1, annotation_models_tab_, tr("Models"));
    segmentation_->hide();
    classification_->hide();
    layout->addWidget(segmentation_, 1);
    layout->addWidget(classification_, 1);

    segmentation_->status = [this](const QString& text) {
        if (status) status(text);
    };
    segmentation_->mask_saved = [this](const QString& image, const QImage& mask) {
        if (!service_) return;
        try {
            service_->set_segmentation_mask_for_image(image.toStdString(), mask);
            segmentation_->refresh_samples(service_->binding().samples, {});
            domain_changed();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Mask metadata failed"),
                                 QString::fromUtf8(error.what()));
        }
    };
    segmentation_->class_add_requested = [this](const QString& name) {
        if (!service_) return;
        try {
            service_->add_class(name.toStdString());
            segmentation_->set_class_schema(service_->binding().classes);
            domain_changed();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Cannot add class"),
                                 QString::fromUtf8(error.what()));
        }
    };
    segmentation_->class_rename_requested =
        [this](const QString& node_id, const QString& name) {
        if (!service_) return;
        try {
            service_->rename_class(node_id.toStdString(), name.toStdString());
            segmentation_->set_class_schema(service_->binding().classes);
            domain_changed();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Cannot rename class"),
                                 QString::fromUtf8(error.what()));
        }
    };
    segmentation_->class_retire_requested = [this](const QString& node_id) {
        if (!service_) return;
        try {
            service_->retire_class(node_id.toStdString());
            segmentation_->set_class_schema(service_->binding().classes);
            domain_changed();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Cannot delete class"),
                                 QString::fromUtf8(error.what()));
        }
    };
    segmentation_->class_move_requested = [this](const QString& node_id, int direction) {
        if (!service_) return;
        service_->move_class(node_id.toStdString(), direction);
        segmentation_->set_class_schema(service_->binding().classes);
        domain_changed();
    };
    segmentation_->class_visibility_changed =
        [this](const QString& node_id, bool visible) {
        if (!service_) return;
        service_->set_class_visible(node_id.toStdString(), visible);
        segmentation_->set_class_schema(service_->binding().classes);
        domain_changed();
    };
    segmentation_->class_lock_changed = [this](const QString& node_id, bool locked) {
        if (!service_) return;
        service_->set_class_locked(node_id.toStdString(), locked);
        segmentation_->set_class_schema(service_->binding().classes);
        domain_changed();
    };
    auto assign_sample_split = [this](
        const std::vector<std::string>& sample_ids, const std::string& split) {
        if (!service_) return;
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
    classification_->class_assigned = [this](const QString& image, int class_index) {
        if (!service_ || class_index < 0) return;
        std::vector<const AnnotationClass*> active;
        for (const auto& value : service_->binding().classes)
            if (value.active) active.push_back(&value);
        if (class_index >= static_cast<int>(active.size())) return;
        const QString absolute = QFileInfo(image).absoluteFilePath();
        for (const auto& sample : service_->binding().samples) {
            const bool matches = std::any_of(
                sample.image_paths.begin(), sample.image_paths.end(),
                [&absolute](const std::string& path) {
                    return QFileInfo(QString::fromStdString(path)).absoluteFilePath() == absolute;
                });
            if (!matches) continue;
            service_->set_class_for_sample(sample.sample_id, active[class_index]->id);
            domain_changed();
            break;
        }
    };
    classification_->class_add_requested = [this](const QString& name) {
        if (!service_) return;
        try {
            service_->add_class(name.toStdString());
            refresh_annotation();
            domain_changed();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Cannot add class"),
                                 QString::fromUtf8(error.what()));
        }
    };
    return page;
}

void TaskWorkspace::bind(
    WorkflowGraph* graph, int node_id)
{
    graph_ = graph;
    node_id_ = node_id;
    training_->set_service(nullptr);
    inference_->set_service(nullptr);
    results_->set_service(nullptr);
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
    const auto models = service_ ? service_->models() : std::vector<ProjectModel>{};
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
    service_.reset();
    graph_ = nullptr;
    node_id_ = 0;
    annotation_binding_dirty_ = true;
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
    refresh_inference_result();
    refresh_step_availability();
}

void TaskWorkspace::refresh_annotation() {
    if (!service_ || service_->source_node_id() == 0) {
        segmentation_->set_task_dataset({}, {}, {});
        classification_->set_dataset({}, {}, {});
        segmentation_->set_inference_result(nullptr);
        classification_->set_inference_result(nullptr);
        return;
    }
    const auto& task = service_->task();
    const QString directory = QString::fromStdString(service_->source_dataset().directory);
    const bool segmentation = task.task == "sg";
    QTabWidget* inspector = segmentation ? segmentation_inspector_ : classification_inspector_;
    QTabWidget* other_inspector = segmentation ? classification_inspector_ : segmentation_inspector_;
    const int previous_tab = other_inspector->indexOf(annotation_models_tab_);
    if (previous_tab >= 0) {
        other_inspector->removeTab(previous_tab);
    }
    if (inspector->indexOf(annotation_models_tab_) < 0) {
        inspector->insertTab(1, annotation_models_tab_, tr("Models"));
    }
    segmentation_->setVisible(segmentation);
    classification_->setVisible(!segmentation);
    if (segmentation) {
        segmentation_->set_task_dataset(
            directory, service_->binding().samples,
            service_->binding().classes);
    } else {
        QVector<int> labels;
        QStringList class_names;
        std::vector<const AnnotationClass*> active;
        for (const auto& value : service_->binding().classes) {
            if (!value.active) continue;
            active.push_back(&value);
            class_names.push_back(QString::fromStdString(value.name));
        }
        for (const auto& sample : service_->binding().samples) {
            int label = -1;
            for (int index = 0; index < static_cast<int>(active.size()); ++index)
                if (active[index]->id == sample.class_id) label = index;
            labels.push_back(label);
        }
        classification_->set_dataset(
            service_->binding().samples,
            class_names,
            labels);
    }
    annotation_binding_dirty_ = false;
    refresh_inference_result();
}

void TaskWorkspace::refresh_inference_result() {
    annotation_selection_->setText(tr("No model selected"));
    if (service_) {
        const jocelin::model::ModelEntry* model = service_->find_model(service_->selected_model_id());
        const jocelin::model::ModelWeight* weight = model
            ? jocelin::model::find_weight(*model, service_->selected_weight_id().toStdString()) : nullptr;
        if (weight) {
            annotation_selection_->setText(tr("%1 · Weight: %2")
                .arg(QString::fromStdString(model->name), QString::fromStdString(weight->id)));
            annotation_models_->set_current_weight(service_->selected_model_id(), weight->format,
                                                   service_->selected_weight_id());
        } else if (model) {
            annotation_models_->set_current_model_id(service_->selected_model_id());
        }
        if (model && !weight) {
            annotation_selection_->setText(tr("%1 · No weight selected").arg(QString::fromStdString(model->name)));
        }
    }
    const auto* selected = service_ && service_->source_node_id() != 0 ? service_->selected_inference_result() : nullptr;
    QStringList classes;
    if (service_) {
        if (const auto* model = service_->find_model(service_->selected_model_id())) {
            for (const auto& value : model->classes) {
                classes.push_back(QString::fromStdString(value.name));
            }
        }
    }
    segmentation_->set_inference_result(selected);
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
    for (int index = 0; index < static_cast<int>(step_buttons_.size()); ++index) {
        const bool enabled = index == static_cast<int>(Step::Data) ||
            (index == static_cast<int>(Step::Training) && bound) ||
            (index == static_cast<int>(Step::Inference) && bound) ||
            (index == static_cast<int>(Step::Results) && bound) ||
            has_samples;
        auto* button = step_buttons_[static_cast<size_t>(index)];
        if (!button) continue;
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
    if (step == Step::Annotation && segmentation_)
        segmentation_->refresh_inference_layers();
    if (step == Step::Annotation && classification_)
        classification_->refresh_inference_result();
    if (step == Step::Training && training_) training_->refresh();
    if (step == Step::Results && results_) results_->refresh();
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
            [&model_id](const ProjectModel& value) { return value.id == model_id; });
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
