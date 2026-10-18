#include "main_window.h"
#include "task_workspace_pages.h"
#include "node_workspace.h"
#include <torch/serialize.h>
#include "workflow_scene.h"
#include <common/exception/pipeline_exception.h>
#include <iostream>

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QGraphicsView>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

namespace {

studio::NodeItem* node(studio::WorkflowScene* scene, int id) {
    for (auto* item : scene->items()) {
        auto* candidate = dynamic_cast<studio::NodeItem*>(item);
        if (candidate && candidate->data(0).toInt() == id) return candidate;
    }
    return nullptr;
}

bool capture(QWidget& widget, const char* variable) {
    const QString path = qEnvironmentVariable(variable);
    if (path.isEmpty()) return true;
    const int width = qEnvironmentVariableIntValue("WORKFLOW_UI_CAPTURE_WIDTH");
    const int height = qEnvironmentVariableIntValue("WORKFLOW_UI_CAPTURE_HEIGHT");
    if (width > 0 && height > 0) widget.resize(width, height);
    widget.show();
    QEventLoop settle;
    QTimer::singleShot(50, &settle, &QEventLoop::quit);
    settle.exec();
    return widget.grab().save(path);
}

} // namespace

int main(int argc, char** argv) try {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
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
        !window.findChild<QPushButton*>("addTaskButton")) return 4;
    if (!capture(window, "WORKFLOW_HOME_UI_CAPTURE")) return 5;

    const QString pristine_title = window.windowTitle();
    bool task_dialog_checked = false;
    QTimer::singleShot(0, [&task_dialog_checked] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto* modules = dialog->findChild<QTableWidget*>("taskModuleTable");
        auto* create = dialog->findChild<QPushButton*>("createTaskModuleButton");
        if (!modules || !create || modules->currentRow() != -1 || create->isEnabled()) return;
        task_dialog_checked = true;
        dialog->reject();
    });
    window.findChild<QPushButton*>("addTaskButton")->click();
    if (!task_dialog_checked) return 41;
    if (!scene->model()) return 47;
    if (!scene->model()->nodes().empty()) return 48;
    if (window.windowTitle() != pristine_title) return 49;

    window.findChild<QPushButton*>("addDatasetButton")->click();
    bool task_created = false;
    QTimer::singleShot(0, [&task_created] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto* modules = dialog->findChild<QTableWidget*>("taskModuleTable");
        auto* create = dialog->findChild<QPushButton*>("createTaskModuleButton");
        if (!modules || !create) return;
        for (int row = 0; row < modules->rowCount(); ++row) {
            if (modules->item(row, 1)->text() != "sg") continue;
            modules->setCurrentCell(row, 0);
            task_created = create->isEnabled();
            create->click();
            return;
        }
    });
    window.findChild<QPushButton*>("addTaskButton")->click();
    application.processEvents();
    auto* dataset = node(scene, 1);
    auto* task = node(scene, 2);
    if (!task_created || !dataset || !task) return 6;
    const auto& created_task = std::get<studio::TaskNodeConfig>(scene->model()->node(2).config);
    if (created_task.task != "sg" || !scene->model()->connections().empty() ||
        !window.findChild<QLabel*>("propertiesTaskTypeReadOnly") ||
        !window.findChild<QLabel*>("propertiesTaskModuleReadOnly") ||
        window.findChild<QComboBox*>("taskTypeCombo"))
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
    if (!capture(window, "DATASET_WORKSPACE_CAPTURE")) return 9;
    window.findChild<QPushButton*>("nodeWorkspaceBackButton")->click();

    scene->node_activated(2);
    application.processEvents();
    auto* pages = window.findChild<QStackedWidget*>("taskStepPages");
    if (!pages || pages->count() != 6 || pages->currentIndex() != 0) return 10;
    auto* task_identity = window.findChild<QLabel*>("taskWorkspaceIdentity");
    auto* task_context = window.findChild<QLabel*>("taskWorkspaceContext");
    if (!task_identity || task_identity->text() != "SG" ||
        !task_context || task_context->isVisible() || !task_context->text().isEmpty())
        return 79;
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
    if (pages->currentIndex() != 1) return 14;
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
    if (pan->toolTip() != "Pan (Alt+S)" ||
        brush->toolTip() != "Brush (Alt+B)" ||
        polygon->toolTip() != "Polygon (Alt+P)") return 28;
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
        !window.findChild<QListWidget*>("segmentationClassList") ||
        window.findChild<QCheckBox*>("roiLayerCheck")) return 17;

    const int capture_width = qEnvironmentVariableIntValue("WORKFLOW_UI_CAPTURE_WIDTH");
    const int capture_height = qEnvironmentVariableIntValue("WORKFLOW_UI_CAPTURE_HEIGHT");
    window.resize(capture_width > 0 ? capture_width : 1440,
                  capture_height > 0 ? capture_height : 900);
    window.show();
    application.processEvents();
    if (window.width() >= 760 && window.height() >= 480 &&
        (canvas->width() < 260 || canvas->height() < 130)) return 18;
    if (!capture(window, "TASK_WORKSPACE_CAPTURE")) return 19;

    window.findChild<QPushButton*>("workspaceStep3Button")->click();
    application.processEvents();
    auto* split_table = window.findChild<QTableWidget*>("taskSplitTable");
    if (pages->currentIndex() != 2 || !split_table || split_table->rowCount() != 1)
        return 20;
    if (!split_table->item(0, 0)->text().startsWith("img_00001.png") ||
        !split_table->item(0, 0)->toolTip().contains("Original name: sample.png"))
        return 26;
    auto* assignment = qobject_cast<QComboBox*>(split_table->cellWidget(0, 3));
    if (!assignment) return 21;
    assignment->setCurrentIndex(assignment->findData("train"));
    window.findChild<QPushButton*>("applySmartSplitButton")->click();
    if (!capture(window, "SPLIT_WORKSPACE_CAPTURE")) return 80;

    window.findChild<QPushButton*>("workspaceStep4Button")->click();
    application.processEvents();
    if (pages->currentIndex() != 3 ||
        !window.findChild<QSpinBox*>("trainingBatchSize") ||
        !window.findChild<QSpinBox*>("trainingEpochs") ||
        !window.findChild<QLineEdit*>("trainingOutputDirectory") ||
        !window.findChild<QWidget*>("embeddedTrainingMonitor") ||
        !window.findChild<QWidget*>("trainingProjectModelBrowser") ||
        window.findChild<QComboBox*>("trainingSourceMode") ||
        !window.findChild<QPushButton*>("trainingRunDetailsButton") ||
        window.findChild<QPlainTextEdit*>("trainingLog")) return 22;
    auto* loss_chart = window.findChild<QWidget*>("embeddedLossChart");
    auto* training_start =
        window.findChild<QPushButton*>("trainingStartButton");
    auto* stop_training =
        window.findChild<QPushButton*>("trainingStopButton");
    auto* pause_training =
        window.findChild<QPushButton*>("trainingPauseButton");
    auto* resume_training =
        window.findChild<QPushButton*>("trainingResumeButton");
    auto* monitor_layout = stop_training && stop_training->parentWidget()
        ? stop_training->parentWidget()->layout()
        : nullptr;
    auto* monitor_header = monitor_layout && monitor_layout->count() > 0
        ? monitor_layout->itemAt(0)->layout()
        : nullptr;
    if (!loss_chart || loss_chart->minimumHeight() < 220 ||
        !training_start || !training_start->property("primary").toBool() ||
        !stop_training || stop_training->text() != "Stop" ||
        !pause_training || pause_training->text() != "Pause" ||
        !resume_training || resume_training->text() != "Resume" ||
        stop_training->parentWidget() != pause_training->parentWidget() ||
        pause_training->parentWidget() != resume_training->parentWidget() ||
        !monitor_header ||
        !(monitor_header->indexOf(stop_training) <
              monitor_header->indexOf(pause_training) &&
          monitor_header->indexOf(pause_training) <
              monitor_header->indexOf(resume_training)))
        return 29;
    auto* advanced_toggle =
        window.findChild<QPushButton*>("trainingAdvancedToggle");
    auto* advanced_panel =
        window.findChild<QWidget*>("trainingAdvancedPanel");
    auto* settings_panel =
        window.findChild<QWidget*>("trainingSettingsPanel");
    auto* training_scroll =
        window.findChild<QScrollArea*>("trainingSettingsScrollArea");
    if (!advanced_toggle || !advanced_panel || !settings_panel ||
        !training_scroll || settings_panel->findChild<QScrollArea*>() ||
        !advanced_toggle->text().contains("Adam") ||
        !advanced_toggle->text().contains("Cosine") ||
        !advanced_toggle->text().contains("4 workers") ||
        !advanced_toggle->text().contains("›"))
        return 73;
    advanced_toggle->click();
    application.processEvents();
    if (!advanced_panel->isVisible() ||
        !advanced_toggle->text().contains("Advanced") ||
        !advanced_toggle->text().contains("⌄"))
        return 74;
    advanced_toggle->click();

    auto* training_splitter =
        window.findChild<QSplitter*>("trainingWorkspaceSplitter");
    auto* configuration_handle =
        window.findChild<QSplitterHandle*>("trainingConfigurationHandle");
    auto* models_handle =
        window.findChild<QSplitterHandle*>("trainingModelsHandle");
    auto* training_models_panel =
        window.findChild<QWidget*>("trainingModelsPanel");
    if (!training_splitter || !configuration_handle || !models_handle ||
        !training_models_panel || training_splitter->sizes().size() != 3 ||
        training_splitter->sizes()[0] < 220 || training_splitter->sizes()[2] > 1)
        return 81;
    const auto click_handle = [](QSplitterHandle* handle) {
        const QPoint local = handle->rect().center();
        const QPoint global = handle->mapToGlobal(local);
        QMouseEvent press(
            QEvent::MouseButtonPress, QPointF(local), QPointF(local), QPointF(global),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(handle, &press);
        QMouseEvent release(
            QEvent::MouseButtonRelease, QPointF(local), QPointF(local), QPointF(global),
            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(handle, &release);
    };
    const auto drag_handle = [](QSplitterHandle* handle, int dx) {
        const QPoint start = handle->rect().center();
        const QPoint finish = start + QPoint(dx, 0);
        QMouseEvent press(
            QEvent::MouseButtonPress, QPointF(start), QPointF(start),
            QPointF(handle->mapToGlobal(start)),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(handle, &press);
        QMouseEvent move(
            QEvent::MouseMove, QPointF(finish), QPointF(finish),
            QPointF(handle->mapToGlobal(finish)),
            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(handle, &move);
        QMouseEvent release(
            QEvent::MouseButtonRelease, QPointF(finish), QPointF(finish),
            QPointF(handle->mapToGlobal(finish)),
            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(handle, &release);
    };
    const int initial_left = training_splitter->sizes()[0];
    drag_handle(configuration_handle, 80);
    application.processEvents();
    const int resized_left = training_splitter->sizes()[0];
    if (resized_left <= initial_left + 30) return 82;

    click_handle(models_handle);
    application.processEvents();
    const int initial_right = training_splitter->sizes()[2];
    if (initial_right < 260) return 83;
    drag_handle(models_handle, -80);
    application.processEvents();
    const int resized_right = training_splitter->sizes()[2];
    if (resized_right <= initial_right + 30) return 84;

    click_handle(configuration_handle);
    click_handle(models_handle);
    application.processEvents();
    if (training_splitter->sizes()[0] > 1 || training_splitter->sizes()[2] > 1)
        return 85;
    click_handle(configuration_handle);
    click_handle(models_handle);
    application.processEvents();
    if (training_splitter->sizes()[0] < resized_left - 5 ||
        training_splitter->sizes()[2] < resized_right - 5)
        return 86;
    if (!capture(window, "TRAINING_WORKSPACE_CAPTURE")) return 84;

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
    if (pages->currentIndex() != 4 ||
        !window.findChild<QWidget*>("inferenceProjectModelBrowser") ||
        window.findChild<QComboBox*>("inferenceCheckpoint") ||
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
    route_workspace.set_models({route_model});
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
            QAbstractItemView::ExtendedSelection ||
        window.findChild<QPushButton*>("inferenceSelectedButton"))
        return 30;
    auto* inference_start =
        window.findChild<QPushButton*>("inferenceStartButton");
    auto* inference_scope =
        window.findChild<QLabel*>("inferenceScopeSummary");
    if (!inference_start || !inference_scope ||
        !inference_start->property("primary").toBool() ||
        inference_start->text() != "Infer all 1 image" ||
        inference_scope->text() != "All 1 image")
        return 76;
    auto* inference_preview =
        window.findChild<QLabel*>("inferencePreviewHint");
    auto* inference_current =
        window.findChild<QLabel*>("inferenceCurrentImage");
    auto* inference_samples_panel =
        window.findChild<QWidget*>("inferenceSamplesPanel");
    auto* inference_splitter =
        window.findChild<QSplitter*>("inferenceWorkspaceSplitter");
    auto* inference_samples_handle =
        window.findChild<QSplitterHandle*>("inferenceSamplesHandle");
    auto* inference_models_handle =
        window.findChild<QSplitterHandle*>("inferenceModelsHandle");
    auto* inference_canvas_stack = window.findChild<QWidget*>("inferenceCanvasStack");
    auto* inference_segmentation_canvas =
        window.findChild<QWidget*>("inferenceSegmentationCanvas");
    auto* inference_classification_canvas =
        window.findChild<QWidget*>("inferenceClassificationCanvas");
    auto* inference_overlay =
        window.findChild<QCheckBox*>("inferenceOverlayLayerCheck");
    if (inference_preview || !inference_current || !inference_current->wordWrap() ||
        !inference_samples_panel || !inference_splitter || !inference_samples_handle ||
        !inference_models_handle || !inference_canvas_stack ||
        !inference_segmentation_canvas || !inference_classification_canvas || !inference_overlay ||
        inference_splitter->sizes().size() != 3 ||
        !task_config.inference_use_all_images || !task_config.inference_images.empty())
        return 77;
    const int inference_models_width = inference_splitter->sizes()[2];
    drag_handle(inference_models_handle, -70);
    application.processEvents();
    if (inference_splitter->sizes()[2] <= inference_models_width + 25) return 87;
    click_handle(inference_models_handle);
    application.processEvents();
    if (inference_splitter->sizes()[2] > 1) return 88;
    click_handle(inference_models_handle);
    application.processEvents();
    if (inference_splitter->sizes()[2] < inference_models_width + 20) return 89;
    if (!capture(window, "INFERENCE_WORKSPACE_CAPTURE")) return 85;
    window.resize(760, 480);
    application.processEvents();
    if (inference_samples_panel->width() <= 0 || inference_current->width() <= 0)
        return 78;
    window.resize(1440, 900);
    application.processEvents();
    inference_images->item(0)->setSelected(true);
    application.processEvents();
    if (inference_start->text() != "Infer 1 image" ||
        inference_scope->text() != "1 image selected" ||
        !task_config.inference_use_all_images ||
        !task_config.inference_images.empty())
        return 31;

    window.findChild<QPushButton*>("workspaceStep6Button")->click();
    application.processEvents();
    if (pages->currentIndex() != 5 ||
        !window.findChild<QTableWidget*>("taskResultsTable") ||
        window.findChild<QLabel*>("taskResultPreview")) return 24;
    auto* results_empty =
        window.findChild<QWidget*>("taskResultsEmptyState");
    auto* results_content =
        window.findChild<QWidget*>("taskResultsContent");
    auto* results_summary =
        window.findChild<QWidget*>("taskResultsSummary");
    auto* results_splitter = qobject_cast<QSplitter*>(results_content);
    auto* results_samples_handle =
        window.findChild<QSplitterHandle*>("resultsSamplesHandle");
    auto* results_detail_handle =
        window.findChild<QSplitterHandle*>("resultsDetailHandle");
    auto* results_canvas_stack = window.findChild<QWidget*>("resultsCanvasStack");
    auto* results_segmentation_canvas =
        window.findChild<QWidget*>("resultsSegmentationCanvas");
    auto* results_classification_canvas =
        window.findChild<QWidget*>("resultsClassificationCanvas");
    auto* results_overlay =
        window.findChild<QCheckBox*>("resultsOverlayLayerCheck");
    if (!results_empty || !results_empty->isVisible() ||
        !results_content || results_content->isVisible() ||
        !results_summary || results_summary->isVisible() ||
        !results_splitter || results_splitter->sizes().size() != 3 ||
        !results_samples_handle || !results_detail_handle ||
        !results_canvas_stack || !results_segmentation_canvas ||
        !results_classification_canvas || !results_overlay)
        return 32;
    if (!capture(window, "RESULTS_WORKSPACE_CAPTURE")) return 86;

    window.resize(760, 480);
    application.processEvents();
    auto* task_workspace = window.findChild<QWidget*>("taskWorkspace");
    if (!task_workspace || task_workspace->width() < 700 ||
        task_workspace->height() < 350) return 25;
    window.resize(1920, 1080);
    application.processEvents();
    if (task_workspace->width() < 1500) return 26;

    window.findChild<QToolButton*>("taskWorkspaceBackButton")->click();
    application.processEvents();
    if (!capture(window, "WORKFLOW_UI_CAPTURE")) return 27;
    auto* library_dock = window.findChild<QDockWidget*>("nodeLibraryDock");
    auto* project_dock = window.findChild<QDockWidget*>("projectDock");
    if (!library_dock || !project_dock || library_dock->isFloating() ||
        project_dock->isFloating() || view->width() < 380) return 28;
    bool classification_created = false;
    QTimer::singleShot(0, [&classification_created] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto* modules = dialog->findChild<QTableWidget*>("taskModuleTable");
        auto* create = dialog->findChild<QPushButton*>("createTaskModuleButton");
        if (!modules || !create) return;
        for (int row = 0; row < modules->rowCount(); ++row) {
            if (modules->item(row, 1)->text() != "cls") continue;
            modules->setCurrentCell(row, 0);
            classification_created = create->isEnabled();
            create->click();
            return;
        }
    });
    window.findChild<QPushButton*>("addTaskButton")->click();
    const auto& classification = std::get<studio::TaskNodeConfig>(scene->model()->node(3).config);
    if (!classification_created || classification.task != "cls" ||
        classification.model_index != 1 || classification.model_scale != "s" ||
        scene->model()->connections().size() != 1)
        return 45;
    studio::ProjectManager import_project;
    const int import_task_id = import_project.active_workflow().document.graph.add_task_node({"cls"});
    import_project.active_workflow().document.positions.emplace(import_task_id, QPointF(0, 0));
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
        import_workspace->import_model_requested(import_task_id);
        import_dialog_driver.stop();
        if (!specified_target || !import_error.isEmpty())
            throw std::runtime_error("target specification/import failed: " + import_error.toStdString());
        studio::ProjectManager reopened;
        reopened.open_project(import_project.project_file());
        if (reopened.models().size() != (matching ? 1 : 2))
            throw std::runtime_error("external import did not create an independent model row");
        const auto& imported_model = reopened.models().back();
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
    return 0;
} catch (...) {
    std::cerr << jocelin::exception::message(std::current_exception()) << '\n';
    return 100;
}
