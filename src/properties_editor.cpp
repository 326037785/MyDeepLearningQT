#include "properties_editor.h"
#include "icon_provider.h"
#include "model_presentation.h"
#include "model_spec_catalog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace studio {

namespace {

QWidget* dataset_editor(WorkflowGraph* graph, int node_id, std::function<void()> changed) {
    (void)changed;
    auto* editor = new QWidget;
    auto* layout = new QVBoxLayout(editor);
    auto* group = new QGroupBox(PropertiesEditor::tr("Data source"), editor);
    auto* form = new QFormLayout(group);
    auto& config = std::get<DatasetConfig>(graph->node(node_id).config);
    auto* directory = new QLabel(config.directory.empty()
        ? PropertiesEditor::tr("Not imported")
        : QString::fromStdString(config.directory), group);
    directory->setWordWrap(true);
    directory->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(PropertiesEditor::tr("Directory"), directory);
    form->addRow(PropertiesEditor::tr("Bucket capacity"), new QLabel(QString::number(config.bucket_capacity), group));
    layout->addWidget(group);
    layout->addStretch();
    return editor;
}

QWidget* task_editor(WorkflowGraph* graph, int node_id, std::function<void()> changed) {
    auto* editor = new QWidget;
    auto* layout = new QVBoxLayout(editor);
    auto& config = std::get<TaskNodeConfig>(graph->node(node_id).config);
    auto notify = [changed] { if (changed) changed(); };
    auto* model_group = new QGroupBox(PropertiesEditor::tr("Model"), editor);
    auto* model_form = new QFormLayout(model_group);
    auto* model_scale = new QComboBox(model_group);
    model_scale->setObjectName("propertiesTaskModelScale");
    for (const auto& option : model_spec_options) {
        if (config.task != option.task || model_scale->findData(option.scale) >= 0) continue;
        model_scale->addItem(model_scale_label(option.scale), option.scale);
    }
    model_scale->setCurrentIndex(model_scale->findData(QString::fromStdString(config.model_scale)));
    auto* model_index = new QComboBox(model_group);
    model_index->setObjectName("propertiesTaskModelIndex");
    const auto populate_models = [&config, model_index, model_scale] {
        const int current = config.model_index;
        model_index->clear();
        for (const auto& option : model_spec_options) {
            if (config.task != option.task || model_scale->currentData().toString() != option.scale ||
                model_index->findData(option.index) >= 0) continue;
            model_index->addItem(PropertiesEditor::tr("Model %1").arg(option.index), option.index);
        }
        int selected = model_index->findData(current);
        if (selected < 0 && model_index->count() > 0) selected = 0;
        model_index->setCurrentIndex(selected);
    };
    populate_models();
    model_form->addRow(PropertiesEditor::tr("Capacity"), model_scale);
    model_form->addRow(PropertiesEditor::tr("Model"), model_index);
    layout->addWidget(model_group);

    auto* training_group = new QGroupBox(PropertiesEditor::tr("Training"), editor);
    auto* training_form = new QFormLayout(training_group);
    auto* device = new QComboBox(training_group);
    device->setObjectName("propertiesTaskDevice");
    device->addItem(PropertiesEditor::tr("CPU"), static_cast<int>(jocelin::config::DeviceType::Cpu));
    device->addItem(PropertiesEditor::tr("GPU"), static_cast<int>(jocelin::config::DeviceType::Gpu));
    device->setCurrentIndex(device->findData(static_cast<int>(config.device.type)));
    auto* input_size = new QWidget(training_group);
    auto* input_layout = new QHBoxLayout(input_size);
    input_layout->setContentsMargins(0, 0, 0, 0);
    auto* width = new QSpinBox(input_size);
    auto* height = new QSpinBox(input_size);
    width->setObjectName("propertiesTaskInputWidth");
    height->setObjectName("propertiesTaskInputHeight");
    for (auto* value : {width, height}) { value->setRange(64, 8192); value->setSingleStep(64); }
    width->setValue(config.training.image_width);
    height->setValue(config.training.image_height);
    input_layout->addWidget(width);
    input_layout->addWidget(new QLabel(QStringLiteral("×"), input_size));
    input_layout->addWidget(height);
    auto* training_batch = new QSpinBox(training_group);
    training_batch->setObjectName("propertiesTrainingBatchSize");
    training_batch->setRange(1, 1024);
    training_batch->setValue(config.training.batch_size);
    auto* epochs = new QSpinBox(training_group);
    epochs->setObjectName("propertiesTrainingEpochs");
    epochs->setRange(1, 100000);
    epochs->setValue(config.training.epochs);
    auto* learning_rate = new QDoubleSpinBox(training_group);
    learning_rate->setObjectName("propertiesTrainingLearningRate");
    learning_rate->setDecimals(8);
    learning_rate->setRange(0.00000001, 10.0);
    learning_rate->setValue(config.training.learning_rate);
    auto* training_output = new QLineEdit(
        QString::fromStdString(config.training.output_directory), training_group);
    training_output->setObjectName("propertiesTrainingOutputDirectory");
    training_form->addRow(PropertiesEditor::tr("Device"), device);
    training_form->addRow(PropertiesEditor::tr("Input size"), input_size);
    training_form->addRow(PropertiesEditor::tr("Batch size"), training_batch);
    training_form->addRow(PropertiesEditor::tr("Epochs"), epochs);
    training_form->addRow(PropertiesEditor::tr("Learning rate"), learning_rate);
    training_form->addRow(PropertiesEditor::tr("Output directory"), training_output);
    layout->addWidget(training_group);

    auto* inference_group = new QGroupBox(PropertiesEditor::tr("Inference"), editor);
    auto* inference_form = new QFormLayout(inference_group);
    auto* inference_batch = new QSpinBox(inference_group);
    inference_batch->setObjectName("propertiesInferenceBatchSize");
    inference_batch->setRange(1, 1024);
    inference_batch->setValue(config.inference_batch_size);
    inference_form->addRow(PropertiesEditor::tr("Batch size"), inference_batch);
    layout->addWidget(inference_group);

    QObject::connect(model_scale, qOverload<int>(&QComboBox::currentIndexChanged), editor,
        [&config, model_index, model_scale, populate_models, notify](int) {
            if (model_scale->currentIndex() < 0) return;
            config.model_scale = model_scale->currentData().toString().toStdString();
            populate_models();
            if (model_index->currentIndex() >= 0)
                config.model_index = model_index->currentData().toInt();
            notify();
        });
    QObject::connect(model_index, qOverload<int>(&QComboBox::currentIndexChanged), editor,
        [&config, model_index, notify](int) {
            if (model_index->currentIndex() < 0) return;
            config.model_index = model_index->currentData().toInt();
            notify();
        });
    QObject::connect(device, qOverload<int>(&QComboBox::currentIndexChanged), editor,
        [&config, device, notify](int) {
            const auto type = static_cast<jocelin::config::DeviceType>(device->currentData().toInt());
            config.device = type == jocelin::config::DeviceType::Gpu
                ? jocelin::config::Device{type, 0, "cuda:0"}
                : jocelin::config::cpu_device();
            notify();
        });
    QObject::connect(width, qOverload<int>(&QSpinBox::valueChanged), editor,
        [&config, notify](int value) { config.training.image_width = value; notify(); });
    QObject::connect(height, qOverload<int>(&QSpinBox::valueChanged), editor,
        [&config, notify](int value) { config.training.image_height = value; notify(); });
    QObject::connect(training_batch, qOverload<int>(&QSpinBox::valueChanged), editor,
        [&config, notify](int value) { config.training.batch_size = value; notify(); });
    QObject::connect(epochs, qOverload<int>(&QSpinBox::valueChanged), editor,
        [&config, notify](int value) { config.training.epochs = value; notify(); });
    QObject::connect(learning_rate, qOverload<double>(&QDoubleSpinBox::valueChanged), editor,
        [&config, notify](double value) { config.training.learning_rate = static_cast<float>(value); notify(); });
    QObject::connect(training_output, &QLineEdit::editingFinished, editor,
        [&config, training_output, notify] { config.training.output_directory = training_output->text().trimmed().toStdString(); notify(); });
    QObject::connect(inference_batch, qOverload<int>(&QSpinBox::valueChanged), editor,
        [&config, notify](int value) { config.inference_batch_size = value; notify(); });
    layout->addStretch();
    return editor;
}

} // namespace

PropertiesEditor::PropertiesEditor(QWidget* parent) : QWidget(parent), layout_(new QVBoxLayout(this)) {
    layout_->setContentsMargins(0, 0, 0, 0);
}

void PropertiesEditor::show_workflow() {
    auto* editor = new QWidget;
    auto* layout = new QVBoxLayout(editor);
    layout->addStretch();
    auto* icon = new QLabel(editor);
    icon->setPixmap(IconProvider::icon("settings").pixmap(36, 36));
    icon->setAlignment(Qt::AlignCenter);
    icon->setAccessibleName(tr("Select a node to edit its configuration."));
    icon->setToolTip(icon->accessibleName());
    layout->addWidget(icon);
    layout->addStretch();
    replace_content(editor);
}

void PropertiesEditor::edit_node(WorkflowGraph* graph, int node_id, std::function<void()> changed) {
    if (!graph || graph->nodes().count(node_id) == 0) return;
    const auto kind = graph->node(node_id).kind;
    if (kind == NodeKind::DatasetInput) replace_content(dataset_editor(graph, node_id, std::move(changed)));
    else replace_content(task_editor(graph, node_id, std::move(changed)));
}

void PropertiesEditor::replace_content(QWidget* content) {
    if (content_) {
        layout_->removeWidget(content_);
        delete content_;
    }
    content_ = content;
    content_->setParent(this);
    layout_->addWidget(content_);
}

} // namespace studio
