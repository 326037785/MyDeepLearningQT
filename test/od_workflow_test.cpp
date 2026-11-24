#include "app/main_window.h"
#include "modules/model_management/application/model_actions.h"
#include "modules/project/application/project_manager.h"
#include "modules/task/application/task_workspace_service.h"
#include "modules/task/ui/task_workspace.h"
#include "modules/workflow/ui/workflow_scene.h"

#include <ATen/Parallel.h>
#include <infer_network/ov/common/openvino_runtime.h>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsView>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template<class Widget>
Widget* control(const QWidget& parent, const char* name) {
    Widget* result = parent.findChild<Widget*>(name);
    if (!result) {
        throw std::runtime_error(std::string("Missing OD workflow control: ") + name);
    }
    return result;
}

template<class Condition>
void wait_for(Condition done, const QString& dialog_error, const char* timeout) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!done() && dialog_error.isEmpty() && elapsed.elapsed() < 90000) {
        QApplication::processEvents();
        QThread::msleep(10);
    }
    if (!dialog_error.isEmpty()) {
        throw std::runtime_error(dialog_error.toStdString());
    }
    require(done(), timeout);
}

} // namespace

int main(int argc, char** argv) try {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    const bool yolo26 = application.arguments().contains("--yolo26");
    const int model_index = yolo26 ? 1 : 0;
    QTemporaryDir fixtures;
    require(fixtures.isValid(), "Could not create OD project fixture");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, fixtures.filePath("settings"));
    at::set_num_threads(2);

    studio::ProjectManager fixture;
    studio::WorkflowDocument& document = fixture.active_workflow().document;
    const int dataset_id = document.graph.add_node(studio::NodeKind::DatasetInput);
    const int task_id = document.graph.add_task_node({"od"});
    studio::DatasetConfig& dataset = std::get<studio::DatasetConfig>(document.graph.node(dataset_id).config);
    for (const std::string sample_id : {"train-box", "train-empty", "validation", "test"}) {
        const QString directory = fixtures.filePath(QString::fromStdString(sample_id));
        require(QDir().mkpath(directory), "Could not create OD sample directory");
        const QString path = QDir(directory).filePath("same-name.png");
        QImage image(96, 72, QImage::Format_RGB888);
        image.fill(QColor("#213647"));
        if (sample_id == "train-box" || sample_id == "validation") {
            QPainter painter(&image);
            painter.fillRect(QRect(24, 18, 32, 26), QColor("#F4B74F"));
        }
        require(image.save(path), "Could not save OD fixture image");
        dataset.items.push_back({path.toStdString(), {}, true, sample_id});
    }
    document.graph.connect(dataset_id, 0, task_id, 0);
    document.positions = {{dataset_id, {0, 0}}, {task_id, {330, 0}}};
    studio::TaskWorkspaceService setup(document.graph, task_id);
    setup.task().training.workers = 0;
    const std::string box_class = setup.add_class("z-box", "#50DE46");
    const std::string other_class = setup.add_class("a-other", "#EA9952");
    for (const studio::TaskSampleBinding& sample : setup.binding().samples) {
        if (sample.sample_id == "train-box" || sample.sample_id == "validation") {
            setup.set_detection_boxes(sample.sample_id, {{box_class, {0, 40, 31, 32, 26}, std::nullopt}});
        } else if (sample.sample_id == "train-empty") {
            setup.set_detection_boxes(sample.sample_id, {}, true);
        }
    }
    const QString project_path = fixtures.filePath("OD project");
    fixture.save_project(project_path);
    const QString project_file = fixture.project_file();

    studio::MainWindow window;
    window.show();
    QString dialog_error;
    QTimer dialogs;
    QObject::connect(&dialogs, &QTimer::timeout, [&] {
        QWidget* modal = QApplication::activeModalWidget();
        if (QFileDialog* file = qobject_cast<QFileDialog*>(modal)) {
            file->selectFile(project_file);
            QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
        } else if (QInputDialog* device = qobject_cast<QInputDialog*>(modal)) {
            QComboBox* options = device->findChild<QComboBox*>();
            const int cpu = options ? options->findText("CPU") : -1;
            if (cpu >= 0) {
                options->setCurrentIndex(cpu);
                device->accept();
            } else {
                dialog_error = device->labelText();
                device->reject();
            }
        } else if (QMessageBox* message = qobject_cast<QMessageBox*>(modal)) {
            dialog_error = message->text();
            message->accept();
        } else if (QDialog* augmentation = qobject_cast<QDialog*>(modal);
                   augmentation && augmentation->objectName() == "trainingAugmentationDialog") {
            QTreeWidget* operators = control<QTreeWidget>(*augmentation, "augmentationOperatorCatalog");
            QStringList ids;
            QTreeWidgetItem* flip = nullptr;
            for (int row = 0; row < operators->topLevelItemCount(); ++row) {
                QTreeWidgetItem* item = operators->topLevelItem(row);
                const QString id = item->data(0, Qt::UserRole).toString();
                ids.push_back(id);
                if (id == "flip_horizontal") {
                    flip = item;
                }
            }
            ids.sort();
            const QStringList expected{"brightness", "contrast", "crop", "flip_horizontal",
                "flip_vertical", "gaussian_noise", "scale", "translate"};
            if (ids != expected || !flip) {
                dialog_error = "OD augmentation did not list its eight operators";
                augmentation->reject();
                return;
            }
            flip->setCheckState(0, Qt::Checked);
            control<QDoubleSpinBox>(*augmentation, "augmentationProbability")->setValue(1.0);
            control<QPushButton>(*augmentation, "augmentationApply")->click();
        }
    });
    dialogs.start(10);
    control<QAction>(window, "openProjectAction")->trigger();
    require(dialog_error.isEmpty(), qPrintable(dialog_error));
    auto* scene = dynamic_cast<studio::WorkflowScene*>(control<QGraphicsView>(window, "workflowView")->scene());
    require(scene && scene->model()->nodes().count(task_id) != 0, "OD project did not open");
    scene->node_activated(task_id);
    const auto task = [&]() -> const studio::TaskNodeConfig& {
        return std::get<studio::TaskNodeConfig>(scene->model()->node(task_id).config);
    };

    control<QPushButton>(window, "workspaceStep3Button")->click();
    QTableWidget* split = control<QTableWidget>(window, "taskSplitTable");
    for (int row = 0; row < split->rowCount(); ++row) {
        const QString sample_id = split->item(row, 0)->data(Qt::UserRole).toString();
        const QString assignment = sample_id.startsWith("train") ? "train" : sample_id;
        QComboBox* field = qobject_cast<QComboBox*>(split->cellWidget(row, 3));
        require(field && field->findData(assignment) >= 0, "OD split did not expose its sample assignment");
        field->setCurrentIndex(field->findData(assignment));
    }
    QApplication::processEvents();
    control<QPushButton>(window, "workspaceStep4Button")->click();
    QComboBox* model_index_combo = control<QComboBox>(window, "modelIndexCombo");
    const int choice = model_index_combo->findData(model_index);
    require(choice >= 0, "OD training did not offer the requested model index");
    model_index_combo->setCurrentIndex(choice);
    QComboBox* size = control<QComboBox>(window, "trainingImageSizePreset");
    require(size->findData(320) >= 0, "OD training did not offer the 320 preset");
    size->setCurrentIndex(size->findData(320));
    control<QSpinBox>(window, "trainingBatchSize")->setValue(2);
    control<QSpinBox>(window, "trainingEpochs")->setValue(1);
    control<QPushButton>(window, "trainingAdvancedToggle")->click();
    QPushButton* augmentation = control<QPushButton>(window, "trainingAugmentationButton");
    require(augmentation->isVisible(), "OD training did not expose the augmentation dialog");
    augmentation->click();
    require(dialog_error.isEmpty(), qPrintable(dialog_error));
    require(task().training.augment.enable.flip_horizontal && task().training.augment.probability == 1.0f,
        "OD dialog did not apply the selected augmentation");
    QPushButton* train = control<QPushButton>(window, "trainingStartButton");
    require(train->isVisible() && train->isEnabled(), "OD training could not be started");
    train->click();
    const std::string model_id = "model_1";
    wait_for([&] {
        const auto* model = task().model_list.find(model_id);
        return model && model->training_status != jocelin::model::ModelTrainingStatus::Running;
    }, dialog_error, "OD training did not finish");
    const auto* model = task().model_list.find(model_id);
    require(model->training_status == jocelin::model::ModelTrainingStatus::Completed &&
        model->model_index == model_index && model->completed_epochs == 1 &&
        model->image_width == 320 && model->image_height == 320 &&
        model->classes.size() == 2 && model->classes[0].id == box_class &&
        model->classes[1].id == other_class && model->metric_name.find("mAP50-95") != std::string::npos,
        "Qt ModelList did not register the trained OD model and checkpoint schema");
    const auto* weight = jocelin::model::find_training_weight(*model);
    require(weight && QFileInfo(QString::fromStdString(weight->path)).isFile(),
        "Qt did not register the OD checkpoint Weight");
    const std::string weight_id = weight->id;

    control<QPushButton>(window, "workspaceStep5Button")->click();
    QComboBox* device = control<QComboBox>(window, "inferenceDeviceType");
    device->setCurrentIndex(device->findData("cpu"));
    QComboBox* scope = control<QComboBox>(window, "inferenceSampleSplitFilter");
    scope->setCurrentIndex(scope->findData("validation"));
    QWidget* browser = control<QWidget>(window, "inferenceProjectModelBrowser");
    QTreeWidget* rows = control<QTreeWidget>(*browser, "projectModelBrowserList");
    require(rows->topLevelItemCount() == 1, "OD model did not reach the Inference ModelList");
    QWidget* cell = rows->itemWidget(rows->topLevelItem(0), 1);
    require(cell != nullptr, "OD model did not expose its LibTorch branch");
    control<QCheckBox>(*cell, "projectModelWeightDeleteCheckBox")->click();
    control<QToolButton>(window, "inferenceFilterToggle")->click();
    QDoubleSpinBox* confidence = control<QDoubleSpinBox>(window, "odFilterConfidence");
    QDoubleSpinBox* nms_iou = control<QDoubleSpinBox>(window, "odFilterNmsIou");
    require(confidence->isVisible() && nms_iou->isVisible() == !yolo26,
        "OD Filter did not show the selected model's controls");
    confidence->setValue(0.33);
    if (!yolo26) {
        nms_iou->setValue(0.44);
    }
    control<QSpinBox>(window, "odFilterMaxDetections")->setValue(19);
    control<QPushButton>(window, "inferenceFilterApply")->click();
    require(task().inference_config.od_confidence_threshold == 0.33f &&
            task().inference_config.od_max_detections == 19,
        "OD Filter did not apply confidence and maximum detections to the Task");
    QPushButton* infer = control<QPushButton>(window, "inferenceStartButton");
    require(infer->isVisible() && infer->isEnabled(), "OD inference could not be started");
    infer->click();
    wait_for([&] { return task().runtime.inference_status != "running"; },
        dialog_error, "OD inference did not finish");
    require(task().runtime.inference_status == "completed", "OD inference did not commit a result");
    control<QPushButton>(window, "workspaceStep6Button")->click();
    QTableWidget* results = control<QTableWidget>(window, "taskResultsTable");
    require(results->isVisible() && results->rowCount() == 1, "OD Results did not show the selected inference scope");

    control<QPushButton>(window, "workspaceStep2Button")->click();
    QListWidget* samples = control<QListWidget>(window, "odAnnotationSamples");
    for (int row = 0; row < samples->count(); ++row) {
        if (samples->item(row)->data(Qt::UserRole).toString() == "validation") {
            samples->setCurrentRow(row);
            break;
        }
    }
    const QString details = control<QLabel>(window, "odAnnotationResultDetails")->text();
    if (!details.contains("0.33") || details.contains("NMS IoU") == yolo26) {
        throw std::runtime_error("OD Annotation showed unexpected details: " + details.toStdString());
    }

    const bool verify_conversion = !yolo26 && jocelin::Ov::cpu_runtime_available();
    const std::string converted_id = "openvino_fp32";
    if (verify_conversion) {
        control<QPushButton>(window, "workspaceStep5Button")->click();
        cell = rows->itemWidget(rows->topLevelItem(0), 1);
        QTimer::singleShot(0, [&] {
            QWidget* popup = QApplication::activePopupWidget();
            if (!popup) {
                popup = QApplication::activeModalWidget();
            }
            if (!popup) {
                dialog_error = "OD conversion popup did not open";
                return;
            }
            control<QPushButton>(*popup, "weightConversionOpenVino")->click();
            control<QPushButton>(*popup, "weightConversionConfirm")->click();
        });
        control<QToolButton>(*cell, "projectModelConvertButton")->click();
        wait_for([&] {
            return jocelin::model::find_weight(*task().model_list.find(model_id), converted_id) != nullptr;
        }, dialog_error, "OD OpenVINO conversion did not finish");
        require(rows->topLevelItemCount() == 1, "OD conversion created another ModelList row");
        cell = rows->itemWidget(rows->topLevelItem(0), 1);
        QCheckBox* checked = control<QCheckBox>(*cell, "projectModelWeightDeleteCheckBox");
        if (checked->isChecked()) {
            checked->click();
        }
        QWidget* ir_cell = rows->itemWidget(rows->topLevelItem(0), 3);
        require(ir_cell != nullptr, "OD OpenVINO branch is missing");
        control<QCheckBox>(*ir_cell, "projectModelWeightDeleteCheckBox")->click();
        require(infer->isEnabled() && device->currentData().toString() == "cpu" &&
                !control<QSpinBox>(window, "inferenceBatchSize")->isVisible(),
            "OD OpenVINO branch did not offer single-image CPU inference");
        infer->click();
        wait_for([&] { return task().runtime.inference_status != "running"; },
            dialog_error, "OD OpenVINO inference did not finish");
        require(task().runtime.inference_status == "completed", "OD OpenVINO inference did not commit a result");
    }

    studio::ProjectManager reopened;
    reopened.open_project(project_file);
    studio::TaskWorkspaceService recovered(reopened.active_workflow().document.graph, task_id);
    require(recovered.task().training.augment.enable.flip_horizontal &&
        recovered.task().training.augment.probability == 1.0f,
        "OD augmentation settings did not reopen with the project");
    const jocelin::training::TrainingRequest request =
        recovered.build_training_request(jocelin::config::cpu_device());
    require(request.config.model_index == model_index &&
        request.config.augment.enable.flip_horizontal && request.config.augment.probability == 1.0f,
        "OD training request did not receive the saved model and augmentation");
    const studio::InferenceResultSet* saved = recovered.inference_result(
        QString::fromStdString(model_id), QString::fromStdString(weight_id));
    require(saved && saved->samples.size() == 1 && saved->samples.front().source_sample_id == "validation" &&
        std::holds_alternative<jocelin::result::od::OdResult>(saved->samples.front().result) &&
        saved->detection_metrics.count("recall") == 1,
        "OD inference result and measured metrics did not reopen on the trained Weight branch");
    const auto& result = std::get<jocelin::result::od::OdResult>(saved->samples.front().result);
    const jocelin::config::AiConfig& filter = recovered.task().inference_config;
    require(filter.od_confidence_threshold == 0.33f && filter.od_max_detections == 19 &&
            filter.od_nms_iou_threshold == (yolo26 ? 0.50f : 0.44f) &&
            result.confidence_threshold == 0.33f && result.max_detections == 19 &&
            (yolo26 ? !result.nms_iou_threshold : result.nms_iou_threshold == 0.44f),
        "OD Filter settings or effective result values did not survive reopening");
    if (verify_conversion) {
        const studio::InferenceResultSet* converted = recovered.inference_result(
            QString::fromStdString(model_id), QString::fromStdString(converted_id));
        require(converted && converted->samples.size() == 1 && converted->samples.front().source_sample_id == "validation",
            "OD OpenVINO result did not reopen on the converted branch");
    }

    studio::ModelActions model_actions(reopened, &window);
    model_actions.log = [](const QString&) {};
    model_actions.status = [](const QString&, int) {};
    const QString export_directory = fixtures.filePath("exported");
    require(QDir().mkpath(export_directory), "Could not create OD export directory");
    QString selected_path = export_directory;
    dialogs.stop();
    QTimer model_dialogs;
    QObject::connect(&model_dialogs, &QTimer::timeout, [&] {
        QWidget* modal = QApplication::activeModalWidget();
        if (QFileDialog* file = qobject_cast<QFileDialog*>(modal)) {
            control<QLineEdit>(*file, "fileNameEdit")->setText(selected_path);
            QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
        } else if (QMessageBox* message = qobject_cast<QMessageBox*>(modal)) {
            dialog_error = message->text();
            message->accept();
        }
    });
    model_dialogs.start(5);
    model_actions.export_model(task_id, QString::fromStdString(model_id));
    model_dialogs.stop();
    const QStringList packages = QDir(export_directory).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    require(dialog_error.isEmpty() && packages.size() == 1, "OD model package export failed");
    selected_path = QDir(QDir(export_directory).filePath(packages.front())).filePath("model.json");
    model_dialogs.start(5);
    model_actions.import_model(task_id);
    model_dialogs.stop();
    require(dialog_error.isEmpty(), qPrintable(dialog_error));
    const jocelin::model::ModelEntry& imported = recovered.models().back();
    require(recovered.models().size() == 2 && imported.classes.size() == 2 &&
            imported.classes[0].id == box_class && imported.classes[1].id == other_class,
        "OD model package import did not preserve channel order or create a new row");
    const jocelin::model::ModelWeight* imported_weight = jocelin::model::find_training_weight(imported);
    require(imported_weight != nullptr, "OD model package import lost the LibTorch branch");
    const auto imported_response = jocelin::inference::infer(recovered.build_inference_request(
        jocelin::config::cpu_device(), imported, *imported_weight, {}));
    const auto& imported_report = std::get<jocelin::result::od::OdInferenceResult>(imported_response.report);
    require(imported_report.classes[0].id == box_class && imported_report.classes[1].id == other_class,
        "OD model package inference did not use the ModelList class order");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
