#include "task_workspace.h"

#include "classification_annotation.h"
#include "icon_provider.h"
#include "segmentation_annotation.h"
#include "task_workspace_pages.h"
#include "task_module_catalog.h"
#include "ui_metrics.h"

#include <QComboBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
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
    data_->open_dataset_requested = [this](int id) {
        if (dataset_requested) dataset_requested(id);
    };
    split_->changed = [this] { domain_changed(); };
    split_->status = [this](const QString& text) { if (status) status(text); };
    training_->changed = [this] { domain_changed(); };
    training_->start_requested = [this](const TrainingLaunchRequest& request) {
        if (train_requested) train_requested(task_node_id_, request);
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
        if (import_model_requested) import_model_requested(task_node_id_);
    };
    training_->export_model_requested = [this](const QString& model_id) {
        if (export_model_requested) export_model_requested(task_node_id_, model_id);
    };
    training_->delete_models_requested = [this](const QStringList& model_ids) {
        if (delete_models_requested) delete_models_requested(task_node_id_, model_ids);
    };
    training_->clear_models_requested = [this] {
        if (clear_models_requested) clear_models_requested(task_node_id_);
    };
    inference_->import_model_requested = [this] {
        if (import_model_requested) import_model_requested(task_node_id_);
    };
    inference_->export_model_requested = [this](const QString& model_id) {
        if (export_model_requested) export_model_requested(task_node_id_, model_id);
    };
    inference_->delete_models_requested = [this](const QStringList& model_ids) {
        if (delete_models_requested) delete_models_requested(task_node_id_, model_ids);
    };
    inference_->clear_models_requested = [this] {
        if (clear_models_requested) clear_models_requested(task_node_id_);
    };
    inference_->changed = [this] { domain_changed(); };
    inference_->start_requested = [this](const InferenceLaunchRequest& request) {
        if (infer_requested) infer_requested(task_node_id_, request);
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
    segmentation_ = new SegmentationAnnotationWidget(page);
    classification_ = new ClassificationAnnotationWidget(page);
    segmentation_->hide();
    classification_->hide();
    layout->addWidget(segmentation_, 1);
    layout->addWidget(classification_, 1);

    segmentation_->status = [this](const QString& text) {
        if (status) status(text);
    };
    segmentation_->mask_saved = [this](const QString& image, const QString& mask) {
        if (!service_) return;
        try {
            service_->set_mask_path_for_image(
                image.toStdString(), mask.toStdString());
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
        [this](const QString& id, const QString& name) {
        if (!service_) return;
        try {
            service_->rename_class(id.toStdString(), name.toStdString());
            segmentation_->set_class_schema(service_->binding().classes);
            domain_changed();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Cannot rename class"),
                                 QString::fromUtf8(error.what()));
        }
    };
    segmentation_->class_retire_requested = [this](const QString& id) {
        if (!service_) return;
        try {
            service_->retire_class(id.toStdString());
            segmentation_->set_class_schema(service_->binding().classes);
            domain_changed();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Cannot delete class"),
                                 QString::fromUtf8(error.what()));
        }
    };
    segmentation_->class_move_requested = [this](const QString& id, int direction) {
        if (!service_) return;
        service_->move_class(id.toStdString(), direction);
        segmentation_->set_class_schema(service_->binding().classes);
        domain_changed();
    };
    segmentation_->class_visibility_changed =
        [this](const QString& id, bool visible) {
        if (!service_) return;
        service_->set_class_visible(id.toStdString(), visible);
        segmentation_->set_class_schema(service_->binding().classes);
        domain_changed();
    };
    segmentation_->class_lock_changed = [this](const QString& id, bool locked) {
        if (!service_) return;
        service_->set_class_locked(id.toStdString(), locked);
        segmentation_->set_class_schema(service_->binding().classes);
        domain_changed();
    };
    auto assign_sample_split = [this](
        const std::vector<std::string>& sample_ids, const std::string& split) {
        if (!service_) return;
        for (const auto& id : sample_ids) service_->set_sample_split(id, split);
        refresh_annotation();
        if (split_) split_->refresh();
        domain_changed();
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
    WorkflowGraph* graph, int task_node_id,
    const std::vector<ProjectModel>* models,
    const QString& annotation_directory)
{
    graph_ = graph;
    task_node_id_ = task_node_id;
    project_models_ = models;
    annotation_directory_ = annotation_directory;
    service_.reset();
    if (!graph_ || graph_->nodes().count(task_node_id_) == 0 ||
        graph_->node(task_node_id_).kind != NodeKind::TaskConfig) {
        clear();
        return;
    }
    try {
        service_ = std::make_unique<TaskWorkspaceService>(*graph_, task_node_id_);
        service_->synchronize();
        refresh();
        activate_step(Step::Data);
    } catch (const std::exception& error) {
        context_->setText(QString::fromUtf8(error.what()));
        refresh_step_availability();
    }
}

void TaskWorkspace::refresh_models() {
    const auto models = project_models_ ? *project_models_ : std::vector<ProjectModel>{};
    if (training_) training_->set_models(models);
    if (inference_) inference_->set_models(models);
    if (results_) results_->set_models(models);
}

void TaskWorkspace::clear() {
    service_.reset();
    graph_ = nullptr;
    task_node_id_ = 0;
    project_models_ = nullptr;
    annotation_directory_.clear();
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
    refresh_step_availability();
}

void TaskWorkspace::refresh_annotation() {
    if (!service_) return;
    const auto& task = service_->task();
    const QString directory = QString::fromStdString(service_->source_dataset().directory);
    const bool segmentation = task.task == "sg";
    segmentation_->setVisible(segmentation);
    classification_->setVisible(!segmentation);
    if (segmentation) {
        segmentation_->set_task_dataset(
            directory, service_->binding().samples,
            service_->binding().classes, annotation_directory_);
        segmentation_->set_inference_directory(
            QString::fromStdString(task.inference_output_directory));
    } else {
        classification_->set_inference_directory(
            QString::fromStdString(task.inference_output_directory));
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
        refresh_models();
        inference_->set_service(service_.get());
        results_->set_service(service_.get());
        refresh_annotation();
        refresh_step_availability();
    } catch (const std::exception& error) {
        context_->setText(QString::fromUtf8(error.what()));
        context_->show();
        refresh_step_availability();
    }
}

void TaskWorkspace::refresh_step_availability() {
    const bool bound = service_ && service_->dataset_node_id() != 0;
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
    if (busy && service_ && project_models_) {
        const std::string& model_id = service_->task().runtime.training_model_id;
        const auto model = std::find_if(
            project_models_->begin(), project_models_->end(),
            [&model_id](const ProjectModel& value) { return value.id == model_id; });
        training_busy = model != project_models_->end() &&
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

void TaskWorkspace::append_training_log(const QString& text) {
    training_->append_log(text);
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
