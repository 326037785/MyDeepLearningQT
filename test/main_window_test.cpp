#include "main_window.h"
#include "task_workspace_pages.h"
#include "task_workspace.h"
#include "segmentation_annotation.h"
#include "image_result_canvas.h"
#include "project_storage.h"
#include "node_workspace.h"
#include <torch/serialize.h>
#include "workflow_scene.h"
#include <common/exception/pipeline_exception.h>
#include <iostream>

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QGraphicsView>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

namespace {

studio::NodeItem* node(studio::WorkflowScene* scene, int node_id) {
    for (auto* item : scene->items()) {
        auto* candidate = dynamic_cast<studio::NodeItem*>(item);
        if (candidate && candidate->data(0).toInt() == node_id) return candidate;
    }
    return nullptr;
}

void dataset_lifecycle_ui() {
    const auto require = [](bool value, const char* message) { if (!value) throw std::runtime_error(message); };
    QTemporaryDir fixtures;
    QImage input(8, 6, QImage::Format_RGB888); input.fill(Qt::red);
    const auto a = fixtures.filePath("a.png"), b = fixtures.filePath("b.png");
    require(input.save(a) && input.save(b), "cannot create lifecycle UI inputs");
    studio::MainWindow window;
    auto* view = window.findChild<QGraphicsView*>("workflowView");
    auto* scene = dynamic_cast<studio::WorkflowScene*>(view->scene());
    auto* workspace = window.findChild<studio::NodeWorkspace*>("nodeWorkspace");
    auto& graph = *scene->model();
    const int source = graph.add_node(studio::NodeKind::DatasetInput);
    std::get<studio::DatasetConfig>(graph.node(source).config).items = {{a.toStdString(), {}, true, "sample"}};
    const int owner = graph.add_task_node({"cls"}), peer = graph.add_task_node({"cls"}), detached = graph.add_task_node({"cls"});
    for (int id : {owner, peer, detached}) {
        graph.connect(source, 0, id, 0);
        auto& task = std::get<studio::TaskNodeConfig>(graph.node(id).config);
        task.dataset_binding.classes = {{"cat", 1, "cat", "#112233", true, false, true}};
        task.dataset_binding.samples.front().class_id = "cat";
        task.dataset_binding.samples.front().annotation_state = "annotated";
        task.runtime.results.resize(1);
    }
    graph.remove_connection({source, 0, detached, 0});
    scene->rebuild();
    bool accept = false;
    int confirmations = 0;
    QString error;
    QTimer dialogs;
    QObject::connect(&dialogs, &QTimer::timeout, [&] {
        if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            if (message->windowTitle() == "Delete / replace Dataset") {
                ++confirmations;
                if (!message->text().contains("#" + QString::number(owner)) ||
                    message->text().contains("#" + QString::number(detached))) error = "confirmation included detached Task";
                message->button(accept ? QMessageBox::Yes : QMessageBox::Cancel)->click();
            } else { error = message->text(); message->accept(); }
        }
    });
    dialogs.start(10);
    studio::DatasetConfig replacement;
    replacement.items = {{b.toStdString(), {}, true, "new"}};
    require(!workspace->dataset_replace_requested(source, replacement, 0, nullptr) && confirmations == 1 &&
        std::get<studio::TaskNodeConfig>(graph.node(owner).config).runtime.results.size() == 1 &&
        std::get<studio::DatasetConfig>(graph.node(source).config).items.front().path == a.toStdString(),
        "cancelled replacement changed state");
    node(scene, source)->setSelected(true);
    scene->delete_selected_items();
    require(confirmations == 2 && graph.nodes().count(source) == 1 && graph.connections().size() == 2,
        "cancelled DataNode deletion changed topology");
    accept = true;
    require(workspace->dataset_replace_requested(source, replacement, 0, nullptr) && error.isEmpty(),
        "confirmed Dataset replacement failed");
    for (int id : {owner, peer}) {
        const auto& task = std::get<studio::TaskNodeConfig>(graph.node(id).config);
        require(task.dataset_binding.samples.front().sample_id == "new" && task.runtime.results.empty() &&
            task.dataset_binding.samples.front().annotation_state == "unannotated" && task.dataset_binding.classes.front().id == "cat",
            "replacement failed to clear all connected Tasks or lost classes");
    }
    node(scene, source)->setSelected(true);
    scene->delete_selected_items();
    require(error.isEmpty() && graph.nodes().count(source) == 0 && graph.connections().empty() &&
        std::get<studio::TaskNodeConfig>(graph.node(owner).config).dataset_binding.samples.empty() &&
        std::get<studio::TaskNodeConfig>(graph.node(detached).config).dataset_binding.samples.front().class_id == "cat" &&
        std::get<studio::TaskNodeConfig>(graph.node(detached).config).runtime.results.size() == 1,
        "confirmed DataNode deletion damaged detached Task or retained connected annotations");
    dialogs.stop();
}

} // namespace

int main(int argc, char** argv) try {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    QTemporaryDir settings_directory;
    if (!settings_directory.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_directory.path());
    dataset_lifecycle_ui();
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    QDir root(temporary.path());
    root.mkpath("dataset");
    QImage sample(96, 72, QImage::Format_RGB888);
    sample.fill(QColor("#C65B32"));
    const QString sample_path = root.filePath("dataset/sample.png");
    if (!sample.save(sample_path)) return 2;

    studio::MainWindow window;
    window.resize(1440, 900);
    auto* view = window.findChild<QGraphicsView*>("workflowView");
    auto* scene = view ? dynamic_cast<studio::WorkflowScene*>(view->scene()) : nullptr;
    auto* run = window.findChild<QAction*>("openTaskAction");
    if (!scene || !run || run->isEnabled()) return 3;
    auto* save = window.findChild<QAction*>("saveProjectAction");
    auto* save_as = window.findChild<QAction*>("saveProjectAsAction");
    if (!save || !save_as || save->shortcut() != QKeySequence::Save ||
        save_as->shortcut() != QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S))
        return 40;
    if (!window.findChild<QPushButton*>("addDatasetButton") ||
        !window.findChild<QPushButton*>("addTask_sgButton") ||
        !window.findChild<QPushButton*>("addTask_clsButton")) return 4;

    if (!scene->model()) return 47;
    if (!scene->model()->nodes().empty()) return 48;

    window.findChild<QPushButton*>("addDatasetButton")->click();
    window.findChild<QPushButton*>("addTask_sgButton")->click();
    application.processEvents();
    auto* dataset = node(scene, 1);
    auto* task = node(scene, 2);
    if (!dataset || !task) return 6;
    const auto& created_task = std::get<studio::TaskNodeConfig>(scene->model()->node(2).config);
    if (created_task.task != "sg" || !scene->model()->connections().empty())
        return 42;
    auto* property_batch = window.findChild<QSpinBox*>("propertiesTrainingBatchSize");
    if (!property_batch) return 43;
    property_batch->setValue(17);
    if (std::get<studio::TaskNodeConfig>(scene->model()->node(2).config)
            .training.batch_size != 17)
        return 44;
    scene->begin_connection_drag(
        dataset->port(studio::PortDirection::Output, 0),
        dataset->port(studio::PortDirection::Output, 0)->scenePos());
    scene->finish_connection_drag(
        dataset->port(studio::PortDirection::Output, 0),
        task->port(studio::PortDirection::Input, 0)->scenePos());

    scene->node_activated(1);
    application.processEvents();
    auto* import = window.findChild<QPushButton*>("datasetImportButton");
    if (!import) return 7;
    bool imported = false;
    QTimer::singleShot(0, [&imported, sample_path] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto* path = dialog->findChild<QLineEdit*>("datasetImportPathEdit");
        auto* add = dialog->findChild<QPushButton*>("datasetAddPathButton");
        if (!path || !add) return;
        path->setText(sample_path);
        add->click();
        imported = true;
        dialog->accept();
    });
    import->click();
    if (!imported) return 8;
    window.findChild<QPushButton*>("nodeWorkspaceBackButton")->click();

    scene->node_activated(2);
    application.processEvents();
    auto* pages = window.findChild<QStackedWidget*>("taskStepPages");
    if (!pages) return 10;
    for (int step = 1; step <= 6; ++step) {
        auto* button = window.findChild<QPushButton*>(
            QString("workspaceStep%1Button").arg(step));
        if (!button || !button->isEnabled()) return 11;
    }
    auto* data_table = window.findChild<QTableWidget*>("taskDataTable");
    auto* search = window.findChild<QLineEdit*>("taskDataSearch");
    if (!data_table || data_table->rowCount() != 1 || !search) return 12;
    if (!data_table->item(0, 1)->text().startsWith("img_00001.png") ||
        !data_table->item(0, 1)->toolTip().contains("Original name: sample.png"))
        return 25;
    search->setText("sample");
    application.processEvents();
    if (data_table->isRowHidden(0)) return 13;

    window.findChild<QPushButton*>("workspaceStep2Button")->click();
    application.processEvents();
    auto* canvas = window.findChild<QWidget*>("segmentationMaskCanvas");
    auto* class_combo = window.findChild<QComboBox*>("segmentationClassCombo");
    auto* add_class = window.findChild<QPushButton*>("segmentationAddClassButton");
    auto* pan = window.findChild<QToolButton*>("segmentationPanButton");
    auto* transform_region = window.findChild<QToolButton*>(
        "segmentationTransformRegionButton");
    auto* brush = window.findChild<QToolButton*>("segmentationBrushButton");
    auto* eraser = window.findChild<QToolButton*>("segmentationEraserButton");
    auto* rectangle = window.findChild<QToolButton*>("segmentationRectangleButton");
    auto* polygon = window.findChild<QToolButton*>("segmentationPolygonButton");
    auto* fill = window.findChild<QToolButton*>("segmentationFillButton");
    auto* pick = window.findChild<QToolButton*>("segmentationPickButton");
    if (!canvas || !class_combo || !add_class || !pan || !transform_region || !brush || !eraser ||
        !rectangle || !polygon || !fill || !pick) return 15;
    for (auto* tool : {pan, transform_region, brush, eraser, rectangle, polygon, fill, pick}) {
        if (tool->icon().isNull() || tool->accessibleName().isEmpty() ||
            tool->toolTip().isEmpty() || tool->toolTip().contains('\n')) return 16;
    }
    auto* data_page = dynamic_cast<studio::DatasetSelectionView*>(pages->widget(0));
    if (!data_page || !data_page->changed) return 70;
    auto& task_config = std::get<studio::TaskNodeConfig>(scene->model()->node(2).config);
    task_config.dataset_binding.classes = {
        studio::AnnotationClass{
            "imported-class", 1, "imported", "#1F8A9E",
            true, false, true}};
    data_page->changed();
    window.findChild<QPushButton*>("workspaceStep1Button")->click();
    window.findChild<QPushButton*>("workspaceStep2Button")->click();
    application.processEvents();
    if (class_combo->count() != 1 || class_combo->itemText(0) != "imported")
        return 71;
    task_config.dataset_binding.classes.clear();
    window.findChild<QPushButton*>("workspaceStep1Button")->click();
    data_page->changed();
    window.findChild<QPushButton*>("workspaceStep2Button")->click();
    application.processEvents();
    if (class_combo->count() != 0) return 72;
    class_combo->setEditText("foreground");
    add_class->click();
    application.processEvents();
    if (class_combo->count() != 1 ||
        !window.findChild<QListWidget*>("segmentationClassList")) return 17;

    window.show();
    application.processEvents();

    window.findChild<QPushButton*>("workspaceStep3Button")->click();
    application.processEvents();
    auto* split_table = window.findChild<QTableWidget*>("taskSplitTable");
    if (!split_table || split_table->rowCount() != 1)
        return 20;
    if (!split_table->item(0, 0)->text().startsWith("img_00001.png") ||
        !split_table->item(0, 0)->toolTip().contains("Original name: sample.png"))
        return 26;
    auto* assignment = qobject_cast<QComboBox*>(split_table->cellWidget(0, 3));
    if (!assignment) return 21;
    assignment->setCurrentIndex(assignment->findData("train"));
    window.findChild<QPushButton*>("applySmartSplitButton")->click();

    window.findChild<QPushButton*>("workspaceStep4Button")->click();
    application.processEvents();
    if (!window.findChild<QSpinBox*>("trainingBatchSize") ||
        !window.findChild<QSpinBox*>("trainingEpochs") ||
        !window.findChild<QLineEdit*>("trainingOutputDirectory") ||
        !window.findChild<QWidget*>("embeddedTrainingMonitor") ||
        !window.findChild<QWidget*>("trainingProjectModelBrowser") ||
        !window.findChild<QPushButton*>("trainingRunDetailsButton")) return 22;
    auto* loss_chart = window.findChild<QWidget*>("embeddedLossChart");
    auto* training_start =
        window.findChild<QPushButton*>("trainingStartButton");
    auto* stop_training =
        window.findChild<QPushButton*>("trainingStopButton");
    auto* pause_training =
        window.findChild<QPushButton*>("trainingPauseButton");
    auto* resume_training =
        window.findChild<QPushButton*>("trainingResumeButton");
    if (!loss_chart || !training_start || !stop_training ||
        !pause_training || !resume_training)
        return 29;
    auto* advanced_toggle =
        window.findChild<QPushButton*>("trainingAdvancedToggle");
    auto* advanced_panel =
        window.findChild<QWidget*>("trainingAdvancedPanel");
    if (!advanced_toggle || !advanced_panel) return 73;
    advanced_toggle->click();
    application.processEvents();
    if (!advanced_panel->isVisible()) return 74;
    advanced_toggle->click();

    window.findChild<QPushButton*>("workspaceStep5Button")->click();
    application.processEvents();
    auto* inference_images = window.findChild<QListWidget*>("inferenceImageList");
    auto* inference_device_type =
        window.findChild<QComboBox*>("inferenceDeviceType");
    auto* inference_device = window.findChild<QComboBox*>("inferenceDevice");
    auto* inference_weight_type =
        window.findChild<QComboBox*>("inferenceWeightType");
    auto* inference_weight_branch =
        window.findChild<QComboBox*>("inferenceWeightBranch");
    if (!window.findChild<QWidget*>("inferenceProjectModelBrowser") ||
        !inference_images ||
        !inference_device_type || !inference_device ||
        !inference_weight_type || !inference_weight_branch ||
        !window.findChild<QSpinBox*>("inferenceBatchSize")) return 23;
    if (inference_device_type->findText("CPU") < 0 ||
        inference_device_type->currentText() != "CPU" ||
        inference_device->count() < 1 ||
        inference_weight_type->count() != 0 ||
        inference_weight_branch->count() != 0)
        return 90;

    const auto create_weight_file = [&root](const QString& name) {
        QFile file(root.filePath(name));
        return file.open(QIODevice::WriteOnly) && file.write("route-test") > 0;
    };
    if (!create_weight_file("route.pt") ||
        !create_weight_file("route.onnx") ||
        !create_weight_file("route.xml") ||
        !create_weight_file("route.engine"))
        return 91;
    studio::ProjectModel route_model;
    route_model.id = "route_model";
    route_model.name = "Route model";
    route_model.task = jocelin::config::ToolType::Sg;
    const auto add_route_weight = [&](const std::string& id,
                                      jocelin::config::WeightType format,
                                      const QString& path) {
        jocelin::model::ModelWeight weight;
        weight.id = id;
        weight.format = format;
        weight.role = format == jocelin::config::WeightType::LibTorch
            ? jocelin::model::WeightRole::Training
            : jocelin::model::WeightRole::Inference;
        weight.path = path.toStdString();
        weight.supported_device_types =
            jocelin::model::default_supported_devices(format);
        route_model.weights.push_back(std::move(weight));
    };
    add_route_weight("libtorch_fp32", jocelin::config::WeightType::LibTorch,
                     root.filePath("route.pt"));
    add_route_weight("onnx_fp32", jocelin::config::WeightType::Onnx,
                     root.filePath("route.onnx"));
    add_route_weight("openvino_fp32", jocelin::config::WeightType::OpenVino,
                     root.filePath("route.xml"));
    add_route_weight("tensorrt_fp16", jocelin::config::WeightType::TensorRt,
                     root.filePath("route.engine"));
    studio::TaskWorkspaceService route_service(*scene->model(), 2);
    studio::InferenceWorkspace route_workspace;
    route_workspace.set_service(&route_service);
    route_service.task().model_list.restore({route_model}, 2);
    route_workspace.set_models({route_model});
    // All three model browsers follow their TaskNode, including same-type Nodes.
    studio::WorkflowGraph model_graph = *scene->model();
    const int second_model_node = model_graph.add_task_node({"sg"});
    model_graph.connect(1, 0, second_model_node, 0);
    std::get<studio::TaskNodeConfig>(model_graph.node(2).config).model_list.restore({route_model}, 2);
    auto independent_model = route_model;
    independent_model.name = "Independent Node model";
    std::get<studio::TaskNodeConfig>(model_graph.node(second_model_node).config).model_list.restore({independent_model}, 2);
    studio::TaskWorkspace owned_workspace;
    for (const int owner : {2, second_model_node}) {
        owned_workspace.bind(&model_graph, owner);
        const auto lists = owned_workspace.findChildren<QTreeWidget*>("projectModelBrowserList");
        if (lists.empty()) throw std::runtime_error("Task model browsers are missing");
        const QString expected = owner == 2 ? "Route model" : "Independent Node model";
        for (const auto* list : lists)
            if (list->topLevelItemCount() != 1 || !list->topLevelItem(0)->text(0).contains(expected))
                throw std::runtime_error("Training/Inference/Results browser used another TaskNode's models");
    }
    auto& result_task = std::get<studio::TaskNodeConfig>(model_graph.node(2).config);
    auto extra_branch = result_task.model_list.entries().front().weights.front();
    extra_branch.id = "libtorch_alternate";
    result_task.model_list.entries().front().weights.push_back(extra_branch);
    extra_branch.id = "no_result";
    result_task.model_list.entries().front().weights.push_back(extra_branch);
    auto another_model = result_task.model_list.entries().front();
    another_model.id = "another_model"; another_model.name = "Another model";
    result_task.model_list.entries().push_back(another_model);
    for (const auto& branch : {std::string("libtorch_fp32"), std::string("libtorch_alternate")}) {
        studio::InferenceResultSet result;
        result.model_id = "route_model"; result.weight_id = branch;
        const auto& sample = result_task.dataset_binding.samples.front();
        jocelin::result::sg::SgResult value;
        value.image_path = sample.image_paths.front();
        const QImage original_image(QString::fromStdString(value.image_path));
        value.image_width = original_image.width(); value.image_height = original_image.height();
        value.mean_confidence = branch == "libtorch_fp32" ? 0.25 : 0.75;
        jocelin::result::sg::SgResult::Component component;
        component.label = 1; component.interest = true;
        component.region.vRun.emplace_back(1, 1, 3, 1);
        value.components.push_back(component);
        result.samples.push_back({sample.sample_id, value});
        result_task.runtime.results.push_back(result);
    }
    owned_workspace.bind(&model_graph, 2);
    auto* annotation_branch = owned_workspace.findChild<QComboBox*>("annotationWeightBranch");
    auto* result_branch = owned_workspace.findChild<QComboBox*>("resultsWeightBranch");
    auto* inference_branch = owned_workspace.findChild<QComboBox*>("inferenceWeightBranch");
    auto* result_table = owned_workspace.findChild<QTableWidget*>("taskResultsTable");
    const auto result_confidence = [&result_table]() {
        const QTableWidgetItem* item = result_table ? result_table->item(0, 2) : nullptr;
        return item ? item->text().toDouble() : -1.0;
    };
    studio::ImageResultCanvas* infer_canvas = dynamic_cast<studio::ImageResultCanvas*>(
        owned_workspace.findChild<QWidget*>("inferenceSegmentationCanvas"));
    auto* annotation_canvas = dynamic_cast<studio::MaskCanvas*>(owned_workspace.findChild<QWidget*>("segmentationMaskCanvas"));
    if (!annotation_branch || !result_branch || !inference_branch || !result_table || !infer_canvas || !annotation_canvas)
        throw std::runtime_error("branch result selectors or previews missing");
    for (const auto& branch : {QString("libtorch_alternate"), QString("libtorch_fp32"), QString("no_result")}) {
        annotation_branch->setCurrentIndex(annotation_branch->findData(branch));
        const bool available = branch != "no_result";
        if (result_branch->currentData().toString() != branch || inference_branch->currentData().toString() != branch || result_table->rowCount() != (available ? 1 : 0) ||
            infer_canvas->has_prediction_overlay() != available || annotation_canvas->has_prediction_overlay() != available ||
            (available && result_confidence() != (branch == "libtorch_fp32" ? 0.25 : 0.75)))
            throw std::runtime_error("branch selection did not synchronize Results/Inference/Annotation or empty state");
    }
    inference_branch->setCurrentIndex(inference_branch->findData("libtorch_alternate"));
    if (result_branch->currentData().toString() != "libtorch_alternate" || result_table->rowCount() != 1)
        throw std::runtime_error("Inference branch selection did not update Results");
    auto* annotation_models = owned_workspace.findChild<QWidget*>("annotationProjectModelBrowser");
    auto* annotation_model_rows = annotation_models ? annotation_models->findChild<QTreeWidget*>("projectModelBrowserList") : nullptr;
    if (!annotation_model_rows || annotation_model_rows->topLevelItemCount() != 2)
        throw std::runtime_error("Annotation has no Node ModelList row selector");
    annotation_model_rows->setCurrentItem(annotation_model_rows->topLevelItem(1));
    if (result_table->rowCount() != 0 || infer_canvas->has_prediction_overlay() || annotation_canvas->has_prediction_overlay())
        throw std::runtime_error("Annotation model row selection did not clear missing results");
    annotation_model_rows->setCurrentItem(annotation_model_rows->topLevelItem(0));
    if (result_table->rowCount() != 1 || !annotation_canvas->has_prediction_overlay() || result_confidence() != 0.75)
        throw std::runtime_error("Annotation model row selection did not restore its exact branch");
    owned_workspace.bind(&model_graph, second_model_node);
    if (result_table->rowCount() != 0 || infer_canvas->has_prediction_overlay() || annotation_canvas->has_prediction_overlay())
        throw std::runtime_error("same-id result leaked into another TaskNode's previews");
    auto* route_device_type =
        route_workspace.findChild<QComboBox*>("inferenceDeviceType");
    auto* route_weight_type =
        route_workspace.findChild<QComboBox*>("inferenceWeightType");
    auto* route_weight_branch =
        route_workspace.findChild<QComboBox*>("inferenceWeightBranch");
    if (!route_device_type || route_device_type->currentText() != "CPU" ||
        !route_weight_type || route_weight_type->count() != 2 ||
        route_weight_type->findText("LibTorch") < 0 ||
        route_weight_type->findText("ONNX") < 0 ||
        route_weight_type->findText("OpenVINO") >= 0 ||
        route_weight_type->findText("TensorRT") >= 0)
        return 92;
    route_weight_type->setCurrentIndex(route_weight_type->findText("ONNX"));
    if (!route_weight_branch || route_weight_branch->count() != 1 ||
        route_weight_branch->currentData().toString() != "onnx_fp32")
        return 93;
    if (inference_images->count() != 1 ||
        !inference_images->item(0)->text().startsWith("img_00001.png") ||
        !inference_images->item(0)->toolTip().contains("Original name: sample.png"))
        return 27;
    if (inference_images->selectionMode() !=
            QAbstractItemView::ExtendedSelection)
        return 30;
    auto* inference_start =
        window.findChild<QPushButton*>("inferenceStartButton");
    auto* inference_scope =
        window.findChild<QLabel*>("inferenceScopeSummary");
    if (!inference_start || !inference_scope)
        return 76;
    const QString all_scope_text = inference_scope->text();
    const QString all_action_text = inference_start->text();
    if (all_scope_text.isEmpty() || all_action_text.isEmpty())
        return 76;
    if (!task_config.inference_use_all_images || !task_config.inference_images.empty())
        return 77;
    inference_images->item(0)->setSelected(true);
    application.processEvents();
    if (inference_start->text() == all_action_text ||
        inference_scope->text() == all_scope_text ||
        !task_config.inference_use_all_images ||
        !task_config.inference_images.empty())
        return 31;

    window.findChild<QPushButton*>("workspaceStep6Button")->click();
    application.processEvents();
    if (!window.findChild<QTableWidget*>("taskResultsTable")) return 24;
    auto* results_empty =
        window.findChild<QWidget*>("taskResultsEmptyState");
    auto* results_content =
        window.findChild<QWidget*>("taskResultsContent");
    if (!results_empty || !results_empty->isVisible() ||
        !results_content || results_content->isVisible())
        return 32;

    window.findChild<QToolButton*>("taskWorkspaceBackButton")->click();
    application.processEvents();
    window.findChild<QPushButton*>("addTask_clsButton")->click();
    const auto& classification = std::get<studio::TaskNodeConfig>(scene->model()->node(3).config);
    if (classification.task != "cls" ||
        classification.model_index != 1 || classification.model_scale != "s" ||
        scene->model()->connections().size() != 1)
        return 45;
    studio::ProjectManager import_project;
    const int import_node_id = import_project.active_workflow().document.graph.add_task_node({"cls"});
    import_project.active_workflow().document.positions.emplace(import_node_id, QPointF(0, 0));
    const int independent_node_id = import_project.active_workflow().document.graph.add_task_node({"cls"});
    import_project.active_workflow().document.positions.emplace(independent_node_id, QPointF(200, 0));
    const QString import_project_root = root.filePath("import-project");
    import_project.save_project(import_project_root);
    studio::MainWindow import_window;
    import_window.show();
    QString selected_file = import_project.project_file();
    bool specified_target = false;
    QString import_error;
    QTimer import_dialog_driver;
    int dialog_ticks = 0;
    QObject::connect(&import_dialog_driver, &QTimer::timeout, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        if (++dialog_ticks > 500) {
            import_error = "model import dialog timed out";
            dialog->reject();
            return;
        }
        if (auto* files = qobject_cast<QFileDialog*>(dialog)) {
            files->selectFile(selected_file);
            QMetaObject::invokeMethod(files, "accept", Qt::DirectConnection);
        } else if (dialog->objectName() == "importModelSpecDialog") {
            auto* spec = dialog->findChild<QComboBox*>("importModelSpec");
            const int index = spec ? spec->findData(2) : -1;
            if (index < 0) { dialog->reject(); return; }
            spec->setCurrentIndex(index);
            dialog->findChild<QSpinBox*>("importImageWidth")->setValue(64);
            dialog->findChild<QSpinBox*>("importImageHeight")->setValue(64);
            dialog->findChild<QSpinBox*>("importClassCount")->setValue(2);
            specified_target = true;
            dialog->accept();
        } else if (auto* message = qobject_cast<QMessageBox*>(dialog)) {
            import_error = message->text();
            message->accept();
        }
    });
    import_dialog_driver.start(10);
    import_window.findChild<QAction*>("openProjectAction")->trigger();
    import_dialog_driver.stop();
    studio::NodeWorkspace* import_workspace = nullptr;
    for (auto* widget : import_window.findChildren<QWidget*>())
        if (auto* candidate = dynamic_cast<studio::NodeWorkspace*>(widget))
            import_workspace = candidate;
    if (!import_workspace || !import_error.isEmpty())
        throw std::runtime_error("cannot open model-import project: " + import_error.toStdString());
    for (const bool matching : {true, false}) {
        const QString source = root.filePath(matching ? "external.pt" : "unmatched.pt");
        torch::serialize::OutputArchive weights;
        if (matching) {
            torch::serialize::OutputArchive classifier;
            classifier.write("bias", torch::full({2}, 4.5));
            weights.write("classifier", classifier);
        } else {
            weights.write("external_only", torch::ones({1}));
        }
        weights.save_to(source.toStdString());
        selected_file = source;
        specified_target = false;
        dialog_ticks = 0;
        import_dialog_driver.start(10);
        import_workspace->import_model_requested(import_node_id);
        import_dialog_driver.stop();
        if (!specified_target || !import_error.isEmpty())
            throw std::runtime_error("target specification/import failed: " + import_error.toStdString());
        studio::ProjectManager reopened;
        reopened.open_project(import_project.project_file());
        if (!studio::TaskWorkspaceService(reopened.active_workflow().document.graph, independent_node_id).models().empty())
            throw std::runtime_error("model import changed another TaskNode's ModelList");
        if (studio::TaskWorkspaceService(reopened.active_workflow().document.graph, import_node_id).models().size() != (matching ? 1 : 2))
            throw std::runtime_error("external import did not create an independent model row");
        const auto& imported_model = studio::TaskWorkspaceService(reopened.active_workflow().document.graph, import_node_id).models().back();
        if (imported_model.task != jocelin::config::ToolType::Cls || imported_model.model_index != 2 ||
            imported_model.model_scale != jocelin::config::ModelScale::N ||
            imported_model.training_status != jocelin::model::ModelTrainingStatus::Imported || imported_model.classes.size() != 2 ||
            imported_model.weights.size() != 1 || imported_model.weights.front().format != jocelin::config::WeightType::LibTorch)
            throw std::runtime_error("imported model identity or weight branch changed: " +
                jocelin::config::to_string(imported_model.task) + "/" + std::to_string(imported_model.model_index) +
                "/" + jocelin::config::to_string(imported_model.model_scale) + " classes=" +
                std::to_string(imported_model.classes.size()) + " weights=" + std::to_string(imported_model.weights.size()) +
                " status=" + jocelin::model::to_string(imported_model.training_status));
        const auto& weight = imported_model.weights.front();
        const auto metadata = jocelin::checkpoint::inspect_checkpoint(weight.path);
        if (metadata.resumable() || metadata.num_classes != 2 || metadata.model_spec() != "cls-2-n")
            throw std::runtime_error("external import fabricated training state or lost the target");
        QFile load_report(QDir(QString::fromStdString(imported_model.asset_directory)).filePath("libtorch_fp32-load-report.txt"));
        if (!load_report.open(QIODevice::ReadOnly) ||
            !load_report.readAll().startsWith(matching ? "loaded=1 " : "loaded=0 "))
            throw std::runtime_error("external import report was not persisted");
        if (matching) {
            torch::serialize::InputArchive saved, classifier;
            saved.load_from(weight.path);
            saved.read("classifier", classifier);
            torch::Tensor bias;
            classifier.read("bias", bias);
            if (!bias.eq(4.5).all().item<bool>())
                throw std::runtime_error("Qt import did not use Core name/shape matching");
        }
    }
    // A second Node independently creates model_1; deletion must resolve its owner first.
    specified_target = false;
    dialog_ticks = 0;
    import_dialog_driver.start(10);
    import_workspace->import_model_requested(independent_node_id);
    import_dialog_driver.stop();
    if (!specified_target || !import_error.isEmpty())
        throw std::runtime_error("independent Node model import failed: " + import_error.toStdString());
    studio::ProjectManager managed;
    managed.open_project(import_project.project_file());
    const auto* independent = studio::TaskWorkspaceService(managed.active_workflow().document.graph, independent_node_id)
        .find_model("model_1");
    const auto* original = studio::TaskWorkspaceService(managed.active_workflow().document.graph, import_node_id)
        .find_model("model_1");
    if (!independent || !original || independent->asset_directory == original->asset_directory)
        throw std::runtime_error("same-id models did not get separate Node assets");
    const QString independent_asset = QString::fromStdString(independent->asset_directory);
    const QString original_asset = QString::fromStdString(original->asset_directory);
    const auto original_inference_root = QFileInfo(managed.node_models_directory(import_node_id)).absolutePath() + "/inference";
    const auto independent_inference_root = QFileInfo(managed.node_models_directory(independent_node_id)).absolutePath() + "/inference";
    const auto original_inference = original_inference_root + "/model_1/libtorch_fp32";
    const auto independent_inference = independent_inference_root + "/model_1/libtorch_fp32";
    if (!QDir().mkpath(original_inference) || !QDir().mkpath(independent_inference))
        throw std::runtime_error("cannot create model inference cleanup fixtures");
    studio::ProjectStorage::write_atomic(original_inference + "/marker", "original");
    studio::ProjectStorage::write_atomic(independent_inference + "/marker", "peer");
    // Request preparation can fail after a branch is selected; its previous files must survive.
    QImage old_prediction(6, 4, QImage::Format_RGB888); old_prediction.fill(Qt::green);
    const auto old_heatmap = original_inference + "/old_heatmap.png";
    const auto old_input = root.filePath("inference-input.png");
    if (!old_prediction.save(old_heatmap) || !old_prediction.save(old_input))
        throw std::runtime_error("cannot prepare previous inference artifacts");
    auto& failed_task = std::get<studio::TaskNodeConfig>(managed.active_workflow().document.graph.node(import_node_id).config);
    studio::TaskSampleBinding old_sample; old_sample.sample_id = "old-sample";
    failed_task.dataset_binding.samples = {old_sample};
    studio::InferenceResultSet old_result;
    old_result.model_id = "model_1"; old_result.weight_id = "libtorch_fp32";
    old_result.result_directory = original_inference.toStdString();
    jocelin::result::cls::ClsResult old_value;
    old_value.image_path = old_input.toStdString(); old_value.save_dir = old_result.result_directory;
    old_value.heatmap_path = old_heatmap.toStdString(); old_value.predicted_class = 1;
    old_value.confidence = 0.8; old_value.probabilities = {0.8, 0.2};
    old_result.samples.push_back({"old-sample", old_value});
    failed_task.runtime.results = {old_result}; failed_task.runtime.inference_status = "completed";
    managed.save_project();
    const auto previous_json = studio::ProjectStorage::read(original_inference + "/result.json");
    const auto previous_heatmap = studio::ProjectStorage::read(old_heatmap);
    selected_file = import_project.project_file();
    import_dialog_driver.start(10);
    import_window.findChild<QAction*>("openProjectAction")->trigger();
    import_dialog_driver.stop();
    if (!import_error.isEmpty()) throw std::runtime_error("cannot reopen failure fixture");
    import_dialog_driver.start(10);
    import_workspace->infer_requested(import_node_id,
        {"model_1", "libtorch_fp32", jocelin::config::cpu_device()});
    import_dialog_driver.stop();
    if (import_error.isEmpty() || studio::ProjectStorage::read(original_inference + "/result.json") != previous_json ||
        studio::ProjectStorage::read(old_heatmap) != previous_heatmap)
        throw std::runtime_error("failed inference request destroyed previous branch files");
    import_error.clear();
    managed.open_project(import_project.project_file());
    if (std::get<studio::TaskNodeConfig>(managed.active_workflow().document.graph.node(import_node_id).config)
            .runtime.inference_status != "completed" ||
        !QDir(QFileInfo(original_inference).absolutePath()).entryList(
            {".libtorch_fp32.pending-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty())
        throw std::runtime_error("failed inference changed the persisted result status or leaked pending output");
    QTimer confirm_model_change;
    QObject::connect(&confirm_model_change, &QTimer::timeout, [&] {
        if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            if (auto* yes = message->button(QMessageBox::Yes)) yes->click();
            else { import_error = message->text(); message->accept(); }
        }
    });
    confirm_model_change.start(10);
    import_workspace->delete_models_requested(import_node_id, {"model_1"});
    confirm_model_change.stop();
    managed.open_project(import_project.project_file());
    if (!import_error.isEmpty() || QFileInfo::exists(original_asset) || !QFileInfo::exists(independent_asset) ||
        QDir(original_inference).exists() || !QDir(independent_inference).exists() ||
        studio::TaskWorkspaceService(managed.active_workflow().document.graph, import_node_id).models().size() != 1 ||
        !studio::TaskWorkspaceService(managed.active_workflow().document.graph, independent_node_id).find_model("model_1"))
        throw std::runtime_error("Node model deletion touched another Node or failed to persist");
    if (!QDir().mkpath(original_inference_root + "/model_2/libtorch_fp32"))
        throw std::runtime_error("cannot create clear inference fixture");
    studio::ProjectStorage::write_atomic(original_inference_root + "/model_2/libtorch_fp32/marker", "clear me");
    confirm_model_change.start(10);
    import_workspace->clear_models_requested(import_node_id);
    confirm_model_change.stop();
    managed.open_project(import_project.project_file());
    if (!import_error.isEmpty() ||
        !studio::TaskWorkspaceService(managed.active_workflow().document.graph, import_node_id).models().empty() ||
        !studio::TaskWorkspaceService(managed.active_workflow().document.graph, independent_node_id).find_model("model_1") ||
        !QFileInfo::exists(independent_asset) || QDir(original_inference_root).exists() ||
        studio::ProjectStorage::read(independent_inference + "/marker") != "peer")
        throw std::runtime_error("Node model clear touched another Node or failed to persist");

    auto& peer_task = std::get<studio::TaskNodeConfig>(managed.active_workflow().document.graph.node(independent_node_id).config);
    auto* peer_model = peer_task.model_list.find("model_1");
    const QString peer_weight_path = QString::fromStdString(peer_model->weights.front().path);
    auto converted_weight = peer_model->weights.front();
    converted_weight.id = "onnx_fp32";
    converted_weight.format = jocelin::config::WeightType::Onnx;
    converted_weight.role = jocelin::model::WeightRole::Inference;
    converted_weight.parent_weight_id = "libtorch_fp32";
    converted_weight.path = QDir(independent_asset).filePath("model.onnx").toStdString();
    studio::ProjectStorage::write_atomic(QString::fromStdString(converted_weight.path), "onnx fixture");
    peer_task.model_list.add_weight("model_1", converted_weight);
    auto peer_result = old_result;
    peer_result.result_directory = independent_inference.toStdString();
    auto& peer_value = std::get<jocelin::result::cls::ClsResult>(peer_result.samples.front().result);
    peer_value.save_dir = peer_result.result_directory;
    peer_value.heatmap_path.clear();
    peer_task.runtime.results = {peer_result};
    managed.save_project();
    const QString peer_catalog = QDir(managed.node_models_directory(independent_node_id)).filePath("model_list.json");
    const QByteArray catalog_before = studio::ProjectStorage::read(peer_catalog);
    const QString workflow_path = QDir(managed.root_directory()).filePath("workflows/workflow_1.json");
    const QByteArray workflow_before = studio::ProjectStorage::read(workflow_path);
    if (!QFile::remove(peer_weight_path)) {
        throw std::runtime_error("cannot remove a registered weight fixture");
    }
    bool cleanup = false;
    int missing_prompts = 0;
    QTimer missing_dialogs;
    QObject::connect(&missing_dialogs, &QTimer::timeout, [&] {
        if (auto* files = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            files->selectFile(import_project.project_file());
            QMetaObject::invokeMethod(files, "accept", Qt::DirectConnection);
        } else if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            if (message->windowTitle() == "Missing model assets") {
                ++missing_prompts;
                message->button(cleanup ? QMessageBox::Yes : QMessageBox::Cancel)->click();
            } else {
                import_error = message->text();
                message->accept();
            }
        }
    });
    for (bool accept_cleanup : {false, true}) {
        cleanup = accept_cleanup;
        missing_dialogs.start(10);
        import_window.findChild<QAction*>("openProjectAction")->trigger();
        missing_dialogs.stop();
        if (!cleanup && (studio::ProjectStorage::read(peer_catalog) != catalog_before ||
            studio::ProjectStorage::read(workflow_path) != workflow_before || !QDir(independent_inference).exists())) {
            throw std::runtime_error("cancelled missing-asset cleanup modified Project data");
        }
    }
    managed.open_project(import_project.project_file());
    peer_model = std::get<studio::TaskNodeConfig>(managed.active_workflow().document.graph.node(independent_node_id).config)
        .model_list.find("model_1");
    if (missing_prompts != 2 || !import_error.isEmpty() || !peer_model || peer_model->weights.size() != 1 ||
        peer_model->weights.front().id != "onnx_fp32" || QDir(independent_inference).exists()) {
        throw std::runtime_error("confirmed missing training weight cleanup lost another branch or retained inference");
    }
    // The normal Qt single-weight command also persists a valid empty model row.
    confirm_model_change.start(10);
    import_workspace->delete_model_weight_requested(independent_node_id, "model_1", "onnx_fp32");
    confirm_model_change.stop();
    managed.open_project(import_project.project_file());
    if (!std::get<studio::TaskNodeConfig>(managed.active_workflow().document.graph.node(independent_node_id).config)
        .model_list.find("model_1")->weights.empty()) {
        throw std::runtime_error("Qt weight deletion did not persist an empty model row");
    }

    const int next_node_before_clear = scene->model()->next_node_id();
    QTimer::singleShot(0, [] {
        if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            if (auto* yes = dialog->button(QMessageBox::Yes)) yes->click();
    });
    window.findChild<QAction*>("clearWorkflowAction")->trigger();
    if (!scene->model()->nodes().empty()) throw std::runtime_error("clear did not remove Nodes");
    window.findChild<QPushButton*>("addDatasetButton")->click();
    if (scene->model()->nodes().begin()->first != next_node_before_clear)
        throw std::runtime_error("clearing the workflow recycled a persistent Node id");
    return 0;
} catch (...) {
    std::cerr << jocelin::exception::message(std::current_exception()) << '\n';
    return 100;
}
