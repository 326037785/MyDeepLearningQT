#include "execution_module.h"
#include "model_actions.h"
#include "node_workspace.h"
#include "project_manager.h"
#include "project_storage.h"
#include "task_workspace_service.h"
#include "ui_theme.h"
#include "weight_conversion_popup.h"

#include <checkpoint/checkpoint_catalog.h>
#include <classification/cls_model_factory.h>
#include <inference/inference_service.h>
#include <segmentation/sg_model_factory.h>

#include <ATen/Parallel.h>
#include <torch/torch.h>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_same_result(const jocelin::inference::InferenceReport& expected,
                         const jocelin::inference::InferenceReport& actual) {
    require(expected.index() == actual.index(), "conversion changed the task result type");
    if (const auto* classification = std::get_if<jocelin::result::cls::ClsInferenceResult>(&expected)) {
        const auto& converted = std::get<jocelin::result::cls::ClsInferenceResult>(actual);
        require(classification->samples.size() == 1 && converted.samples.size() == 1,
                "classification did not produce a result for the input image");
        const auto& source = classification->samples.front();
        const auto& result = converted.samples.front();
        require(source.predicted_class == result.predicted_class &&
                source.probabilities.size() == result.probabilities.size(), "classification prediction changed");
        for (size_t index = 0; index < source.probabilities.size(); ++index) {
            require(std::abs(source.probabilities[index] - result.probabilities[index]) < 1e-4,
                    "classification probabilities changed");
        }
    } else {
        const auto& segmentation = std::get<jocelin::result::sg::SgInferenceResult>(expected);
        const auto& converted = std::get<jocelin::result::sg::SgInferenceResult>(actual);
        require(segmentation.samples.size() == 1 && converted.samples.size() == 1,
                "segmentation did not produce a result for the input image");
        const auto& source = segmentation.samples.front();
        const auto& result = converted.samples.front();
        require(!source.label_mask.empty() && source.label_mask.size() == result.label_mask.size() &&
                source.label_mask.type() == result.label_mask.type() &&
                cv::norm(source.label_mask, result.label_mask, cv::NORM_INF) == 0.0,
                "segmentation changed the mask in original image coordinates");
        require(!source.confidence_map.empty() && source.confidence_map.size() == result.confidence_map.size() &&
                cv::norm(source.confidence_map, result.confidence_map, cv::NORM_INF) < 1e-4,
                "segmentation confidence changed");
    }
}

} // namespace

int main(int argc, char** argv) try {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    studio::apply_ui_theme(app);
    at::set_num_threads(1);
    torch::manual_seed(42);
    using jocelin::config::ModelScale;
    using jocelin::config::ToolType;
    using jocelin::config::WeightType;
    struct Scenario {
        ToolType task;
        int index;
        ModelScale scale;
        bool normalize;
        bool project_actions;
    };
    const Scenario scenarios[] = {
        {ToolType::Cls, 2, ModelScale::N, true, true},
        {ToolType::Cls, 2, ModelScale::N, false, false},
        {ToolType::Cls, 1, ModelScale::S, true, false},
        {ToolType::Sg, 0, ModelScale::S, true, false},
        {ToolType::Sg, 1, ModelScale::M, true, false},
    };
    const auto capabilities = jocelin::runtime::available_runtime_capabilities();
    const auto gpu = std::find_if(capabilities.devices.begin(), capabilities.devices.end(), [](const auto& device) {
        return jocelin::config::is_gpu(device);
    });
    const bool cuda_available = gpu != capabilities.devices.end() &&
        jocelin::runtime::runtime_supports_device(WeightType::Onnx, gpu->type, capabilities);
    for (const auto& scenario : scenarios) {
        const bool normalize = scenario.normalize;
        const bool classification = scenario.task == ToolType::Cls;
        QTemporaryDir temporary;
        require(temporary.isValid(), "cannot create conversion fixture");

        studio::ProjectManager project;
        project.new_project("Conversion");
        studio::WorkflowGraph& graph = project.active_workflow().document.graph;
        const int node = graph.add_task_node({classification ? "cls" : "sg"});
        const int dataset = graph.add_node(studio::NodeKind::DatasetInput);
        const QString image_path = temporary.filePath("input.png");
        QImage image(224, 256, QImage::Format_RGB888);
        image.fill(Qt::white);
        require(image.save(image_path), "cannot create input image");
        std::get<studio::DatasetConfig>(graph.node(dataset).config).items = {
            {image_path.toStdString(), {}, true, "sample"}};
        graph.connect(dataset, 0, node, 0);
        project.active_workflow().document.positions[node] = {0, 0};
        project.active_workflow().document.positions[dataset] = {-200, 0};
        project.save_project(temporary.filePath(QString::fromUtf8("项目")));

        const QString model_directory = QDir(project.node_models_directory(node)).filePath("fixture");
        require(QDir().mkpath(model_directory), "cannot create model directory");
        const QString checkpoint = QDir(model_directory).filePath(QString::fromUtf8("源权重.pt"));
        jocelin::config::AiConfig config;
        config.tool_type = scenario.task;
        config.model_index = scenario.index;
        config.model_scale = scenario.scale;
        config.image_width = classification ? 64 : 128;
        config.image_height = 64;
        config.num_classes = 2;
        config.normalize = normalize;
        config.mean[0] = 0.12345679f;
        config.mean[1] = 0.3456789f;
        config.mean[2] = 0.654321f;
        config.std[0] = 0.32198765f;
        config.std[1] = 1.2345679f;
        config.std[2] = 0.7777778f;
        std::cout << "Verifying " << config.model_spec() << " normalize=" << normalize << std::endl;
        {
            const std::shared_ptr<torch::nn::Module> network = classification
                ? std::static_pointer_cast<torch::nn::Module>(jocelin::cls_net::make_cls_model(config))
                : std::static_pointer_cast<torch::nn::Module>(jocelin::seg_net::make_sg_model(config));
            if (classification && scenario.index == 2) {
                torch::NoGradGuard guard;
                for (const auto& parameter : network->named_parameters(true)) {
                    torch::Tensor tensor = parameter.value();
                    if (tensor.dim() == 4) {
                        tensor.fill_(1.0f / static_cast<float>(tensor.size(1) * tensor.size(2) * tensor.size(3)));
                    } else if (tensor.dim() == 1) {
                        tensor.fill_(parameter.key().find(".weight") != std::string::npos ? 1.0f : 0.0f);
                    } else if (tensor.dim() == 2) {
                        tensor[0].fill_(0.001f);
                        tensor[1].fill_(-0.001f);
                    }
                }
            }
            torch::serialize::OutputArchive archive;
            network->save(archive);
            jocelin::checkpoint::write_training_metadata(archive, config, 0, false, false);
            std::ofstream stream(std::filesystem::u8path(checkpoint.toStdString()), std::ios::binary);
            require(stream.is_open(), "cannot create checkpoint");
            archive.save_to(stream);
        }

        studio::ProjectModel model;
        model.training_run_id = "fixture";
        model.created_at = "2026-01-01T00:00:00Z";
        model.task = config.tool_type;
        model.model_index = config.model_index;
        model.model_scale = config.model_scale;
        model.image_width = config.image_width;
        model.image_height = config.image_height;
        model.asset_directory = model_directory.toStdString();
        if (classification) {
            model.classes = {{"one", "One", 0}, {"two", "Two", 0}};
        } else {
            model.classes = {{"one", "One", 1}};
        }
        jocelin::model::ModelWeight source;
        source.id = "libtorch_fp32";
        source.path = checkpoint.toStdString();
        source.created_at = model.created_at;
        model.weights.push_back(source);
        source.id = "alternate";
        source.role = jocelin::model::WeightRole::Inference;
        model.weights.push_back(source);
        if (scenario.project_actions) {
            studio::ProjectModel unsupported = model;
            unsupported.model_index = -1;
            studio::WeightConversionPopup card(unsupported, source);
            QPushButton* unavailable = card.findChild<QPushButton*>("weightConversionConfirm");
            require(unavailable && !unavailable->isEnabled(), "unsupported model offered ONNX conversion");
        }
        studio::TaskWorkspaceService service(graph, node);
        const studio::ProjectModel added = service.add_model(model);
        const QString model_id = QString::fromStdString(added.id);
        project.mark_active_dirty();
        project.save_project();

        QWidget host;
        studio::NodeWorkspace workspace(&host);
        studio::ExecutionModule execution(project, workspace, &host);
        QString error_log;
        bool locked = false;
        execution.editing_changed = [&](bool enabled) { locked = !enabled; };
        execution.log = [&](const QString& text) {
            if (text.startsWith("Weight conversion failed:")) {
                error_log = text;
            }
        };
        execution.status = [](const QString&, int) {};
        execution.show_log = [] {};
        execution.runtime_status = [](const QString&, const char*) {};
        QEventLoop completed;
        execution.changed = [&] { completed.quit(); };
        workspace.edit_node(&graph, node, {});
        if (scenario.project_actions) {
            host.resize(1280, 800);
            workspace.resize(1280, 800);
            host.show();
            workspace.show();
            workspace.show_inference();
            QApplication::processEvents();

            QWidget* training = workspace.findChild<QWidget*>("trainingProjectModelBrowser");
            QWidget* browser = workspace.findChild<QWidget*>("inferenceProjectModelBrowser");
            require(training && browser && !training->findChild<QToolButton*>("projectModelConvertButton"),
                    "conversion appeared outside the inference browser");
            QTreeWidget* list = browser->findChild<QTreeWidget*>("projectModelBrowserList");
            require(list && list->topLevelItemCount() == 1, "model row is missing");
            QWidget* cell = list->itemWidget(list->topLevelItem(0), 1);
            QComboBox* branches = cell->findChild<QComboBox*>("projectModelWeightBranch");
            QToolButton* convert = cell->findChild<QToolButton*>("projectModelConvertButton");
            require(branches && convert && branches->findData("alternate") >= 0, "source branches are missing");
            const int alternate = branches->findData("alternate");
            branches->setCurrentIndex(alternate);
            QMetaObject::invokeMethod(branches, "activated", Qt::DirectConnection, Q_ARG(int, alternate));
            bool card_ok = false;
            QTimer::singleShot(0, [&] {
                QDialog* popup = qobject_cast<QDialog*>(QApplication::activePopupWidget());
                if (!popup) {
                    return;
                }
                QPushButton* onnx = popup->findChild<QPushButton*>("weightConversionOnnx");
                QPushButton* trt = popup->findChild<QPushButton*>("weightConversionTensorRt");
                QPushButton* ov = popup->findChild<QPushButton*>("weightConversionOpenVino");
                QPushButton* cpu = popup->findChild<QPushButton*>("weightConversionCpu");
                QPushButton* gpu = popup->findChild<QPushButton*>("weightConversionGpu");
                QPushButton* confirm = popup->findChild<QPushButton*>("weightConversionConfirm");
                QWidget* devices = popup->findChild<QWidget*>("weightConversionDevices");
                if (!onnx || !trt || !ov || !cpu || !gpu || !confirm || !devices) {
                    popup->reject();
                    return;
                }
                card_ok = onnx->isChecked() && devices->isHidden() && confirm->isEnabled();
                trt->click();
                card_ok = card_ok && !devices->isHidden() && !cpu->isEnabled() && gpu->isChecked() && !confirm->isEnabled();
                ov->click();
                card_ok = card_ok && devices->isHidden() && !confirm->isEnabled();
                onnx->click();
                card_ok = card_ok && devices->isHidden() && confirm->isEnabled();
                confirm->click();
            });
            convert->click();
            require(card_ok && locked && execution.is_running(), "conversion card or startup lock failed");
        } else {
            execution.start_weight_conversion(node, model_id, "alternate");
            require(locked && execution.is_running(), "conversion did not start");
        }
        QTimer::singleShot(120000, &completed, &QEventLoop::quit);
        completed.exec();
        if (!error_log.isEmpty()) {
            throw std::runtime_error(error_log.toStdString());
        }
        require(!execution.is_running() && !locked, "conversion did not unlock");
        const studio::ProjectModel* current = service.find_model(model_id);
        const jocelin::model::ModelWeight* result = current
            ? jocelin::model::find_weight(*current, "onnx_fp32") : nullptr;
        require(result && result->normalization && result->normalization->normalize == normalize &&
                result->normalization->mean[0] == config.mean[0] && result->normalization->std[2] == config.std[2] &&
                result->parent_weight_id == "alternate" &&
                result->role == jocelin::model::WeightRole::Inference &&
                result->format == WeightType::Onnx && result->onnx_opset == 18 &&
                jocelin::model::find_training_weight(*current) &&
                jocelin::model::find_training_weight(*current)->id == "libtorch_fp32" &&
                QFileInfo(QString::fromStdString(result->path)).isFile(),
                "conversion did not register the selected source as an inference branch");
        const jocelin::model::ModelWeight converted = *result;

        const jocelin::model::ModelWeight* training_weight = jocelin::model::find_training_weight(*current);
        const auto source_request = service.build_inference_request(jocelin::config::cpu_device(), *current,
            *training_weight, temporary.filePath("libtorch_inference").toStdString());
        const auto source_response = jocelin::inference::infer(source_request);

        if (scenario.project_actions) {
            QComboBox* device_type = workspace.findChild<QComboBox*>("inferenceDeviceType");
            QComboBox* weight_type = workspace.findChild<QComboBox*>("inferenceWeightType");
            require(device_type && weight_type && device_type->findData("cpu") >= 0, "CPU inference is unavailable");
            device_type->setCurrentIndex(device_type->findData("cpu"));
            require(weight_type->findData("onnx") >= 0, "converted weight is not selectable for CPU inference");
            if (cuda_available) {
                const int gpu_index = device_type->findData("gpu");
                require(gpu_index >= 0, "CUDA is absent from the device choices");
                device_type->setCurrentIndex(gpu_index);
                require(weight_type->findData("onnx") >= 0, "converted weight is not selectable for CUDA inference");
            }
            execution.start_weight_conversion(node, model_id, "libtorch_fp32");
            require(execution.is_running(), "second conversion did not start");
            completed.exec();
            require(!execution.is_running() && error_log.isEmpty(), "second conversion failed");
            current = service.find_model(model_id);
            require(jocelin::model::find_weight(*current, "onnx_fp32_2") &&
                    QFileInfo(QString::fromStdString(converted.path)).isFile(),
                    "repeated conversion overwrote an existing branch");
        }

        studio::ProjectManager reopened;
        require(reopened.open_project(project.project_file()).empty(), "converted project has missing assets");
        studio::TaskWorkspaceService restored(reopened.active_workflow().document.graph, node);
        const studio::ProjectModel* saved = restored.find_model(model_id);
        const jocelin::model::ModelWeight* saved_weight = saved
            ? jocelin::model::find_weight(*saved, "onnx_fp32") : nullptr;
        require(saved_weight && saved_weight->parent_weight_id == "alternate" && saved_weight->onnx_opset == 18 &&
                saved_weight->normalization && saved_weight->normalization->normalize == normalize &&
                saved_weight->normalization->mean == converted.normalization->mean &&
                saved_weight->normalization->std == converted.normalization->std,
                "converted branch did not survive project reopening");
        const auto infer_restored = [&](const jocelin::config::Device& device, const QString& folder) {
            const auto request = restored.build_inference_request(device, *saved, *saved_weight,
                temporary.filePath(folder).toStdString());
            const auto response = jocelin::inference::infer(request);
            require(!response.fallback.occurred && response.fallback.effective_device == device,
                    "ONNX inference did not use the selected device");
            return response.report;
        };
        const auto cpu_report = infer_restored(jocelin::config::cpu_device(), "onnx_cpu");
        require_same_result(source_response.report, cpu_report);
        if (cuda_available) {
            require_same_result(cpu_report, infer_restored(*gpu, "onnx_cuda"));
            std::cout << "CUDA inference passed for " << config.model_spec() << '\n';
        }
        if (!scenario.project_actions) {
            continue;
        }
        require(jocelin::model::find_weight(*saved, "onnx_fp32_2"), "second converted branch did not reopen");

        const QString bad_path = QDir(model_directory).filePath("invalid.pt");
        QFile bad_file(bad_path);
        require(bad_file.open(QIODevice::WriteOnly), "cannot create invalid checkpoint fixture");
        bad_file.write("invalid");
        bad_file.close();
        model.training_run_id = "invalid";
        model.weights.resize(1);
        model.weights.front().path = bad_path.toStdString();
        const studio::ProjectModel invalid = service.add_model(model);
        project.mark_active_dirty();
        project.save_project();
        execution.start_weight_conversion(node, QString::fromStdString(invalid.id), "libtorch_fp32");
        require(execution.is_running(), "invalid checkpoint did not reach the worker");
        completed.exec();
        require(!execution.is_running() && !locked && !error_log.isEmpty() &&
                service.find_model(QString::fromStdString(invalid.id))->weights.size() == 1 &&
                !QFileInfo::exists(QDir(model_directory).filePath("onnx_fp32_3.onnx")),
                "failed conversion registered or left an output branch");
        for (QMessageBox* message : host.findChildren<QMessageBox*>()) {
            message->accept();
        }
        studio::ModelActions models(project, &host);
        models.log = [](const QString&) {};
        models.status = [](const QString&, int) {};
        models.models_changed = [] {};
        const QString export_directory = temporary.filePath("exports");
        require(QDir().mkpath(export_directory), "cannot create model export directory");
        QString selected_path = export_directory;
        QString dialog_error;
        QTimer file_dialogs;
        QObject::connect(&file_dialogs, &QTimer::timeout, [&] {
            QFileDialog* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (dialog) {
                dialog->findChild<QLineEdit*>("fileNameEdit")->setText(selected_path);
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            } else if (QMessageBox* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                dialog_error = message->text();
                message->accept();
            }
        });
        file_dialogs.start(5);
        models.export_model(node, model_id);
        file_dialogs.stop();
        const QStringList bundles = QDir(export_directory).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        require(dialog_error.isEmpty() && bundles.size() == 1, "model bundle export failed");
        selected_path = QDir(QDir(export_directory).filePath(bundles.front())).filePath("model.json");
        file_dialogs.start(5);
        models.import_model(node);
        file_dialogs.stop();
        require(dialog_error.isEmpty(), "model bundle import failed");
        const studio::ProjectModel& imported = service.models().back();
        const jocelin::model::ModelWeight* imported_weight = jocelin::model::find_weight(imported, converted.id);
        require(imported_weight && imported_weight->normalization &&
                imported_weight->normalization->normalize == normalize &&
                imported_weight->normalization->mean == converted.normalization->mean &&
                imported_weight->normalization->std == converted.normalization->std,
                "model bundle lost ONNX normalization");
        const auto imported_request = service.build_inference_request(jocelin::config::cpu_device(), imported,
            *imported_weight, temporary.filePath("imported_inference").toStdString());
        const auto imported_response = jocelin::inference::infer(imported_request);
        require_same_result(source_response.report, imported_response.report);
        QFile bundle_file(selected_path);
        require(bundle_file.open(QIODevice::ReadOnly), "cannot read exported model bundle");
        const QJsonObject bundle = QJsonDocument::fromJson(bundle_file.readAll()).object();
        bundle_file.close();
        const size_t imported_count = service.models().size();
        for (int omission = 0; omission < 3; ++omission) {
            QJsonObject invalid_bundle = bundle;
            QJsonArray weights = invalid_bundle.value("weights").toArray();
            if (omission == 0) {
                invalid_bundle.remove("weights");
                invalid_bundle.insert("checkpoint", weights.first().toObject().value("file"));
            } else {
                for (int index = 0; index < weights.size(); ++index) {
                    QJsonObject weight = weights[index].toObject();
                    if (omission == 1) {
                        weight.remove("role");
                    } else if (weight.value("format").toString() == "onnx") {
                        weight.remove("normalization");
                    }
                    weights[index] = weight;
                }
                invalid_bundle.insert("weights", weights);
            }
            studio::ProjectStorage::write_atomic(selected_path, QJsonDocument(invalid_bundle).toJson());
            dialog_error.clear();
            file_dialogs.start(5);
            models.import_model(node);
            file_dialogs.stop();
            require(!dialog_error.isEmpty() && service.models().size() == imported_count,
                    "model import accepted an obsolete or incomplete manifest");
        }
        studio::ProjectStorage::write_atomic(selected_path, QJsonDocument(bundle).toJson());
        // The ONNX branch must remain usable after both LibTorch sources are deleted.
        reopened.remove_models(node, {model_id}, "alternate");
        reopened.remove_models(node, {model_id}, "libtorch_fp32");
        require(!QFileInfo::exists(checkpoint), "source deletion retained the checkpoint");
        studio::ProjectManager independent;
        require(independent.open_project(reopened.project_file()).empty(), "independent ONNX branch did not reopen");
        studio::TaskWorkspaceService independent_service(independent.active_workflow().document.graph, node);
        const studio::ProjectModel* independent_model = independent_service.find_model(model_id);
        const jocelin::model::ModelWeight* independent_weight = independent_model
            ? jocelin::model::find_weight(*independent_model, converted.id) : nullptr;
        require(independent_weight, "independent ONNX branch is missing");
        const auto independent_request = independent_service.build_inference_request(jocelin::config::cpu_device(),
            *independent_model, *independent_weight, temporary.filePath("independent_inference").toStdString());
        const auto independent_response = jocelin::inference::infer(independent_request);
        require_same_result(source_response.report, independent_response.report);
        const QString catalog_path = QDir(independent.node_models_directory(node)).filePath("model_list.json");
        QFile catalog_file(catalog_path);
        require(catalog_file.open(QIODevice::ReadOnly), "cannot read saved model catalog");
        QJsonObject catalog = QJsonDocument::fromJson(catalog_file.readAll()).object();
        catalog_file.close();
        QJsonArray catalog_models = catalog.value("models").toArray();
        for (int row = 0; row < catalog_models.size(); ++row) {
            QJsonObject model_object = catalog_models[row].toObject();
            QJsonArray weights = model_object.value("weights").toArray();
            for (int column = 0; column < weights.size(); ++column) {
                QJsonObject weight = weights[column].toObject();
                weight.remove("normalization");
                weights[column] = weight;
            }
            model_object.insert("weights", weights);
            catalog_models[row] = model_object;
        }
        catalog.insert("models", catalog_models);
        studio::ProjectStorage::write_atomic(catalog_path, QJsonDocument(catalog).toJson());
        bool incomplete_catalog_rejected = false;
        try {
            studio::ProjectManager incomplete;
            incomplete.open_project(independent.project_file());
        } catch (const std::runtime_error& error) {
            incomplete_catalog_rejected = std::string(error.what()).find("normalization") != std::string::npos;
        }
        require(incomplete_catalog_rejected, "project loading accepted ONNX weights without normalization");
    }
    std::cout << "Weight conversion contract passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
