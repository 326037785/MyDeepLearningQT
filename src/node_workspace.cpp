#include "node_workspace.h"

#include "dataset_import_dialog.h"
#include "task_dataset_binding.h"
#include "task_workspace.h"
#include "ui_metrics.h"

#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QScrollArea>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace studio {
namespace {

int raw_sample_count(const DatasetConfig& dataset) {
    std::set<std::string> explicit_samples;
    int implicit_images = 0;
    for (const auto& item : dataset.items) {
        if (!item.valid) continue;
        if (item.sample_id.empty())
            ++implicit_images;
        else
            explicit_samples.insert(item.sample_id);
    }
    const int bucket = std::max(1, dataset.bucket_capacity);
    return static_cast<int>(explicit_samples.size()) +
           (implicit_images + bucket - 1) / bucket;
}

} // namespace

NodeWorkspace::NodeWorkspace(QWidget* parent) : QWidget(parent) {
    setObjectName("nodeWorkspace");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    header_ = new QFrame(this);
    header_->setObjectName("nodeWorkspaceHeader");
    auto* header_layout = new QHBoxLayout(header_);
    header_layout->setContentsMargins(
        ThemeTokens::Space3, ThemeTokens::Space1,
        ThemeTokens::Space3, ThemeTokens::Space1);
    header_layout->setSpacing(ThemeTokens::Space2);
    auto* back = new QPushButton(tr("‹  Workflow"), header_);
    back->setObjectName("nodeWorkspaceBackButton");
    back->setProperty("compact", true);
    title_ = new QLabel(header_);
    title_->setProperty("workspaceTitle", true);
    header_layout->addWidget(back);
    header_layout->addWidget(title_);
    header_layout->addStretch();
    root->addWidget(header_);

    auto* content_host = new QWidget(this);
    content_ = new QVBoxLayout(content_host);
    content_->setContentsMargins(0, 0, 0, 0);
    content_->setSpacing(0);
    root->addWidget(content_host, 1);

    connect(back, &QPushButton::clicked, this, [this] {
        if (back_requested) back_requested();
    });
}

void NodeWorkspace::edit_node(
    WorkflowGraph* graph,
    int node_id,
    const QString& dataset_directory)
{
    graph_ = graph;
    node_id_ = node_id;
    dataset_directory_ = dataset_directory;
    rebuild();
}

void NodeWorkspace::clear_node() {
    graph_ = nullptr;
    node_id_ = 0;
    dataset_directory_.clear();
    rebuild();
}

void NodeWorkspace::show_inference() {
    if (task_workspace_)
        task_workspace_->activate_step(TaskWorkspace::Step::Inference);
}

bool NodeWorkspace::has_current_node(NodeKind kind) const {
    return graph_ && graph_->nodes().count(node_id_) != 0 &&
           graph_->node(node_id_).kind == kind;
}

void NodeWorkspace::rebuild() {
    task_workspace_ = nullptr;
    if (page_) {
        content_->removeWidget(page_);
        delete page_;
        page_ = nullptr;
    }
    if (!graph_ || graph_->nodes().count(node_id_) == 0) {
        header_->hide();
        title_->clear();
        return;
    }

    const auto kind = graph_->node(node_id_).kind;
    header_->setVisible(kind == NodeKind::DatasetInput);
    title_->setText(
        tr("Dataset · #%1").arg(node_id_));
    page_ = kind == NodeKind::DatasetInput
        ? dataset_page()
        : task_page();
    content_->addWidget(page_, 1);
}

QWidget* NodeWorkspace::dataset_page() {
    auto* scroll = new QScrollArea;
    scroll->setObjectName("datasetWorkspace");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(scroll);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(
        ThemeTokens::Space4, ThemeTokens::Space4,
        ThemeTokens::Space4, ThemeTokens::Space4);
    layout->setSpacing(ThemeTokens::Space3);

    auto& config =
        std::get<DatasetConfig>(graph_->node(node_id_).config);
    const int valid = static_cast<int>(std::count_if(
        config.items.begin(), config.items.end(),
        [](const DatasetConfig::Item& item) { return item.valid; }));
    const int samples = raw_sample_count(config);
    const int bucket = std::max(1, config.bucket_capacity);

    auto* intro = new QLabel(
        tr("This node owns raw assets and stable sample structure. "
           "Selection, classes, annotation, split, and runtime artifacts "
           "belong to the connected Task."),
        page);
    intro->setObjectName("datasetWorkspaceDescription");
    intro->setProperty("meta", true);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* stats = new QFrame(page);
    stats->setObjectName("datasetStatsPanel");
    auto* stats_layout = new QGridLayout(stats);
    stats_layout->setContentsMargins(
        ThemeTokens::Space4, ThemeTokens::Space3,
        ThemeTokens::Space4, ThemeTokens::Space3);
    stats_layout->setHorizontalSpacing(ThemeTokens::Space5);
    const QStringList metric_names = {
        tr("SAMPLES"), tr("IMAGES"), tr("RESOURCE HEALTH")};
    const QStringList metric_values = {
        QString::number(samples),
        QString::number(valid),
        tr("%1 valid · %2 failed")
            .arg(valid)
            .arg(static_cast<int>(config.items.size()) - valid)};
    const QStringList metric_helpers = {
        bucket == 1
            ? tr("one image per sample")
            : tr("%1 images per sample").arg(bucket),
        tr("authoritative imported inventory"),
        tr("decode and file checks")};
    for (int column = 0; column < metric_names.size(); ++column) {
        auto* name = new QLabel(metric_names[column], stats);
        name->setProperty("eyebrow", true);
        auto* value = new QLabel(metric_values[column], stats);
        value->setProperty("datasetMetric", true);
        value->setObjectName(
            column == 0 ? "datasetSampleCount" :
            column == 1 ? "datasetImageCount" :
                          "datasetResourceHealth");
        auto* helper = new QLabel(metric_helpers[column], stats);
        helper->setProperty("meta", true);
        helper->setWordWrap(true);
        stats_layout->addWidget(name, 0, column);
        stats_layout->addWidget(value, 1, column);
        stats_layout->addWidget(helper, 2, column);
        stats_layout->setColumnStretch(column, 1);
    }
    layout->addWidget(stats);

    auto* group = new QGroupBox(tr("Raw data source"), page);
    group->setProperty("sectionCard", true);
    auto* form = new QFormLayout(group);
    auto* path = new QLabel(
        config.directory.empty()
            ? tr("Multiple sources or not imported")
            : QString::fromStdString(config.directory),
        group);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path->setWordWrap(true);
    form->addRow(tr("Source"), path);
    form->addRow(
        tr("Bucket capacity"),
        new QLabel(
            bucket == 1
                ? tr("1 · standard single-image sample")
                : tr("%1 · images share one sample annotation").arg(bucket),
            group));
    form->addRow(
        tr("Inventory"),
        new QLabel(
            tr("%1 images valid · %2 failed")
                .arg(valid)
                .arg(static_cast<int>(config.items.size()) - valid),
            group));

    auto* import = new QPushButton(
        config.items.empty()
            ? tr("Import dataset images…")
            : tr("Review imported images…"),
        group);
    import->setObjectName("datasetImportButton");
    import->setProperty("primary", config.items.empty());
    auto* action_row = new QWidget(group);
    auto* action_layout = new QHBoxLayout(action_row);
    action_layout->setContentsMargins(0, 0, 0, 0);
    action_layout->addWidget(import);
    action_layout->addStretch();
    form->addRow(QString{}, action_row);
    layout->addWidget(group);

    auto* next = new QLabel(
        tr("Connect this Dataset to one or more Tasks, then open a Task to inspect "
           "samples, annotate, split, train, infer, and review results."),
        page);
    next->setProperty("muted", true);
    next->setWordWrap(true);
    layout->addWidget(next);
    layout->addStretch();

    connect(import, &QPushButton::clicked, page, [this, page] {
        auto& dataset =
            std::get<DatasetConfig>(graph_->node(node_id_).config);
        DatasetImportDialog dialog(dataset, page);
        if (dialog.exec() != QDialog::Accepted) return;
        if (!dataset_replace_requested ||
            !dataset_replace_requested(node_id_, dialog.result(), 0, nullptr)) return;
        dataset_directory_ =
            QString::fromStdString(dataset.directory);
        if (changed) changed();
        QMetaObject::invokeMethod(
            this,
            [this] {
                if (has_current_node(NodeKind::DatasetInput))
                    rebuild();
            },
            Qt::QueuedConnection);
    });

    scroll->setWidget(page);
    return scroll;
}

QWidget* NodeWorkspace::task_page() {
    task_workspace_ = new TaskWorkspace;
    task_workspace_->back_requested = [this] {
        if (back_requested) back_requested();
    };
    task_workspace_->changed = [this] {
        if (changed) changed();
    };
    task_workspace_->dataset_requested = [this](int node_id) {
        if (node_requested) node_requested(node_id);
    };
    task_workspace_->dataset_replace_requested = [this](int source, DatasetConfig dataset, int owner, const TaskDatasetBinding* imported) {
        return dataset_replace_requested && dataset_replace_requested(source, std::move(dataset), owner, imported);
    };
    task_workspace_->train_requested =
        [this](int node_id, const TrainingLaunchRequest& request) {
            if (training_launch_requested)
                training_launch_requested(node_id, request);
        };
    task_workspace_->stop_training_requested = [this] {
        if (stop_requested) stop_requested();
    };
    task_workspace_->pause_training_requested = [this] {
        if (pause_requested) pause_requested();
    };
    task_workspace_->infer_requested =
        [this](int node_id, const InferenceLaunchRequest& request) {
            if (infer_requested) infer_requested(node_id, request);
        };
    task_workspace_->convert_weight_requested = [this](int node_id, const QString& model_id, const QString& weight_id,
                                                        jocelin::config::WeightType target) {
        if (convert_weight_requested) {
            convert_weight_requested(node_id, model_id, weight_id, target);
        }
    };
    task_workspace_->status = [this](const QString& message) {
        if (status) status(message);
    };
    task_workspace_->run_details_requested = [this](const QString& run_id) {
        if (run_details_requested) run_details_requested(run_id);
    };
    task_workspace_->import_model_requested = [this](int node_id) {
        if (import_model_requested) import_model_requested(node_id);
    };
    task_workspace_->export_model_requested = [this](int node_id, const QString& model_id) {
        if (export_model_requested) export_model_requested(node_id, model_id);
    };
    task_workspace_->delete_models_requested = [this](int node_id, const QStringList& model_ids) {
        if (delete_models_requested) delete_models_requested(node_id, model_ids);
    };
    task_workspace_->delete_model_weights_requested = [this](int node_id, const ModelWeightSelection& weights) {
        if (delete_model_weights_requested) {
            delete_model_weights_requested(node_id, weights);
        }
    };
    task_workspace_->delete_model_weight_requested = [this](int node_id, const QString& model_id, const QString& weight_id) {
        if (delete_model_weight_requested) {
            delete_model_weight_requested(node_id, model_id, weight_id);
        }
    };
    task_workspace_->clear_models_requested = [this](int node_id) {
        if (clear_models_requested) clear_models_requested(node_id);
    };
    task_workspace_->bind(
        graph_, node_id_);
    task_workspace_->set_runtime_busy(runtime_busy_);
    return task_workspace_;
}

void NodeWorkspace::refresh_models() {
    if (task_workspace_) task_workspace_->refresh_models();
}

void NodeWorkspace::set_runtime_busy(bool busy) {
    runtime_busy_ = busy;
    if (task_workspace_)
        task_workspace_->set_runtime_busy(busy);
}

void NodeWorkspace::training_started(int node_id) {
    if (node_id == node_id_ && task_workspace_)
        task_workspace_->training_started();
}

void NodeWorkspace::update_training_progress(
    int node_id,
    const jocelin::train::TrainingProgress& progress)
{
    if (node_id == node_id_ && task_workspace_)
        task_workspace_->update_training_progress(progress);
}

void NodeWorkspace::finish_training(
    int node_id,
    const jocelin::training::TrainingResult& result,
    const QString& error)
{
    if (node_id == node_id_ && task_workspace_)
        task_workspace_->finish_training(result, error);
}

void NodeWorkspace::inference_started(int node_id) {
    if (node_id == node_id_ && task_workspace_)
        task_workspace_->inference_started();
}

void NodeWorkspace::update_inference_progress(
    int node_id,
    size_t done,
    size_t total,
    const QString& image)
{
    if (node_id == node_id_ && task_workspace_)
        task_workspace_->update_inference_progress(done, total, image);
}

void NodeWorkspace::finish_inference(int node_id, const QString& error)
{
    if (node_id == node_id_ && task_workspace_)
        task_workspace_->finish_inference(error);
}

} // namespace studio
