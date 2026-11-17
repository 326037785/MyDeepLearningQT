#include "modules/project/application/project_manager.h"
#include "modules/project/application/project_path_validator.h"
#include "modules/project/storage/recent_project_store.h"
#include "modules/task/integration/storage/model_list_storage.h"
#include "modules/project/storage/training_run_store.h"
#include "modules/project/storage/project_storage.h"
#include "modules/task/application/task_workspace_service.h"
#include "modules/task/application/annotation/segmentation_annotation_state.h"
#include "modules/task/application/task_dataset_binding.h"
#include <QImage>
#include <QFileInfo>
#include <QJsonArray>
#include <iostream>

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include <map>
#include <stdexcept>

namespace {
void dataset_lifecycle_roundtrip(const studio::ProjectModel& prototype) {
    const auto require = [](bool value, const char* message) { if (!value) throw std::runtime_error(message); };
    QTemporaryDir fixtures;
    const QString root = fixtures.filePath("Project");
    require(QDir().mkpath(root + "/dataset"), "cannot create lifecycle inputs");
    QImage input(8, 6, QImage::Format_RGB888); input.fill(Qt::red);
    const auto a = root + "/dataset/a.png", b = root + "/dataset/b.png";
    require(input.save(a) && input.save(b), "cannot write lifecycle inputs");
    studio::ProjectManager project;
    auto& graph = project.active_workflow().document.graph;
    const int source = graph.add_node(studio::NodeKind::DatasetInput);
    studio::DatasetConfig dataset;
    dataset.bucket_capacity = 2;
    dataset.items = {{a.toStdString(), {}, true, "bucket"}, {b.toStdString(), {}, true, "bucket"}};
    std::get<studio::DatasetConfig>(graph.node(source).config) = dataset;
    const int owner = graph.add_task_node({"sg"}), peer = graph.add_task_node({"sg"}), detached = graph.add_task_node({"sg"});
    for (int id : {owner, peer, detached}) {
        graph.connect(source, 0, id, 0);
        studio::TaskWorkspaceService service(graph, id);
        service.binding().classes = {{"foreground", 1, "foreground", "#112233", true, false}};
        service.binding().split_settings = {60, 30, 10, 17, "stratified"};
        service.binding().preprocessing = {false, false};
        auto& sample = service.binding().samples.front();
        sample.regions = {jocelin::geo::Region({{1, 1, 3, 1}})};
        sample.annotation_state = "annotated"; sample.selected = false;
        sample.split = "test"; sample.split_manual = true;
        sample.training_mask_path = b.toStdString();
        auto model = prototype;
        const auto directory = root + "/nodes/workflow_1/" + QString::number(id);
        model.asset_directory = (directory + "/models/owned").toStdString();
        model.weights.front().path = (directory + "/models/owned/best.pt").toStdString();
        require(QDir().mkpath(directory + "/models/owned"), "cannot create lifecycle model");
        studio::ProjectStorage::write_atomic(QString::fromStdString(model.weights.front().path), "retained weights");
        service.add_model(model);
        studio::InferenceResultSet result;
        result.model_id = "model_1"; result.weight_id = "libtorch_fp32";
        result.result_directory = (directory + "/inference/model_1/libtorch_fp32").toStdString();
        require(QDir().mkpath(QString::fromStdString(result.result_directory)), "cannot create lifecycle result");
        studio::ProjectStorage::write_atomic(QString::fromStdString(result.result_directory) + "/prediction", "retained prediction");
        service.task().runtime.results = {result}; service.task().runtime.inference_status = "completed";
        service.task().training.batch_size = 19;
    }
    for (const auto& [id, unused] : graph.nodes()) project.active_workflow().document.positions[id] = QPointF(id * 100, 0);
    graph.remove_connection({source, 0, detached, 0});
    project.save_project(root);
    const auto detached_directory = root + "/nodes/workflow_1/" + QString::number(detached);
    const auto detached_region = QDir(detached_directory + "/annotations").entryList({"*.json"}, QDir::Files).front();
    const auto old_region = studio::ProjectStorage::read(detached_directory + "/annotations/" + detached_region);
    studio::ProjectManager reopened; reopened.open_project(project.project_file());
    auto& restored = reopened.active_workflow().document.graph;
    auto& snapshot = std::get<studio::TaskNodeConfig>(restored.node(detached).config);
    require(snapshot.dataset_binding.source_node_id == 0 && snapshot.dataset_binding.bucket_capacity == 2 &&
        snapshot.dataset_binding.samples.front().image_paths == std::vector<std::string>{
            QDir::toNativeSeparators(a).toStdString(), QDir::toNativeSeparators(b).toStdString()} &&
        snapshot.dataset_binding.samples.front().annotation_state == "annotated" &&
        !snapshot.dataset_binding.samples.front().selected && snapshot.runtime.results.size() == 1,
        "detached snapshot/assets changed on reopen");
    studio::TaskWorkspaceService(restored, detached).synchronize();
    require(snapshot.dataset_binding.samples.front().split == "test" &&
        snapshot.dataset_binding.samples.front().split_manual, "detached synchronization mutated assignments");
    const int candidate = restored.add_node(studio::NodeKind::DatasetInput);
    reopened.active_workflow().document.positions[candidate] = {500, 0};
    auto& replacement = std::get<studio::DatasetConfig>(restored.node(candidate).config);
    replacement = dataset;
    const auto reject = [&](studio::DatasetConfig bad) {
        replacement = std::move(bad);
        bool failed = false;
        const auto before = studio::serialize_workflow(restored, reopened.active_workflow().document.positions).json;
        try { restored.connect(candidate, 0, detached, 0); } catch (const std::runtime_error&) { failed = true; }
        require(failed && before == studio::serialize_workflow(restored, reopened.active_workflow().document.positions).json,
            "rebind mismatch changed topology or retained assets");
    };
    auto bad = dataset; bad.bucket_capacity = 1; reject(bad);
    bad = dataset; std::swap(bad.items[0], bad.items[1]); reject(bad);
    bad = dataset; bad.items.pop_back(); reject(bad);
    bad = dataset; bad.items[0].sample_id = "different"; reject(bad);
    bad = dataset; bad.items[0].path = (root + "/dataset/other.png").toStdString(); reject(bad);
    replacement = dataset;
    replacement.items[0].path = (root + "/dataset/./a.png").toStdString();
    require(QFile::remove(a), "cannot temporarily remove input");
    // Matching identity must reconnect even when input quality is currently bad.
    restored.connect(candidate, 0, detached, 0);
    studio::TaskWorkspaceService(restored, detached).synchronize();
    require(snapshot.dataset_binding.source_node_id == candidate &&
        !snapshot.dataset_binding.samples.front().quality_issues.empty() && snapshot.runtime.results.size() == 1,
        "rebind duplicated availability checks or lost results");
    require(input.save(a), "cannot restore input");
    restored.remove_connection({candidate, 0, detached, 0});
    reopened.save_project();
    reopened.save_project_as("DetachedCopy", fixtures.path());
    studio::ProjectManager copied; copied.open_project(reopened.project_file());
    const auto& copied_snapshot = std::get<studio::TaskNodeConfig>(copied.active_workflow().document.graph.node(detached).config);
    require(copied_snapshot.dataset_binding.source_node_id == 0 &&
        QFileInfo(QString::fromStdString(copied_snapshot.dataset_binding.samples.front().image_paths.front())).isFile() &&
        QString::fromStdString(copied_snapshot.dataset_binding.samples.front().image_paths.front()).contains("DetachedCopy"),
        "Save As failed to relocate detached bucket snapshot");
    // Return to the original storage for destructive lifecycle checks.
    reopened.open_project(project.project_file());
    auto& deleting = reopened.active_workflow().document.graph;
    // A failed reference save must leave all prediction and annotation files.
    auto unsavable = reopened;
    unsavable.add_workflow("Missing layout");
    unsavable.active_workflow().document.graph.add_node(studio::NodeKind::DatasetInput);
    unsavable.set_active_index(0);
    bool save_failed = false;
    try { unsavable.clear_dataset_dependents({source}); } catch (const std::runtime_error&) { save_failed = true; }
    const auto owner_directory = root + "/nodes/workflow_1/" + QString::number(owner);
    require(save_failed && QFileInfo::exists(owner_directory + "/inference/model_1/libtorch_fp32/prediction") &&
        QDir(owner_directory + "/annotations").exists(), "failed reference save removed valid assets");
#ifdef Q_OS_WIN
    // A locked prediction prevents directory removal, after reference commit.
    QFile locked(owner_directory + "/inference/model_1/libtorch_fp32/prediction");
    require(locked.open(QIODevice::ReadOnly), "cannot lock cleanup fixture");
    QString cleanup_error;
    try { reopened.clear_dataset_dependents({source}); }
    catch (const std::runtime_error& error) { cleanup_error = QString::fromUtf8(error.what()); }
    require(cleanup_error.contains(owner_directory + "/inference"), "cleanup failure did not report its scoped path");
    studio::ProjectManager after_cleanup_failure; after_cleanup_failure.open_project(reopened.project_file());
    require(std::get<studio::TaskNodeConfig>(after_cleanup_failure.active_workflow().document.graph.node(owner).config)
        .runtime.results.empty(), "cleanup failure left persisted inference references");
    locked.close();
#endif
    reopened.clear_dataset_dependents({source});
    for (int id : {owner, peer}) {
        const auto& task = std::get<studio::TaskNodeConfig>(deleting.node(id).config);
        const auto directory = root + "/nodes/workflow_1/" + QString::number(id);
        require(task.dataset_binding.samples.empty() && task.dataset_binding.bucket_capacity == 0 && task.runtime.results.empty() &&
            task.dataset_binding.classes.front().id == "foreground" && task.dataset_binding.split_settings.seed == 17 &&
            !task.dataset_binding.preprocessing.normalize && !task.dataset_binding.preprocessing.bgr_to_rgb &&
            task.training.batch_size == 19 && task.model_list.entries().size() == 1 &&
            !QDir(directory + "/annotations").exists() && !QDir(directory + "/inference").exists() &&
            studio::ProjectStorage::read(directory + "/models/owned/best.pt") == "retained weights",
            "Dataset cleanup scope lost Task configuration/models or retained dependent assets");
    }
    require(studio::ProjectStorage::read(detached_directory + "/annotations/" + detached_region) == old_region &&
        studio::ProjectStorage::read(detached_directory + "/inference/model_1/libtorch_fp32/prediction") == "retained prediction" &&
        QFileInfo::exists(a) && QFileInfo::exists(b), "cleanup touched detached assets or source images");
    // Cleared bindings use ordinary synchronization for the replacement Dataset.
    auto fresh = dataset; fresh.bucket_capacity = 1; fresh.items = {{b.toStdString(), {}, true, "new-sample"}};
    std::get<studio::DatasetConfig>(deleting.node(source).config) = fresh;
    studio::synchronize_task_bindings_for_dataset(deleting, source);
    require(std::get<studio::TaskNodeConfig>(deleting.node(owner).config).dataset_binding.samples.front().sample_id == "new-sample" &&
        std::get<studio::TaskNodeConfig>(deleting.node(owner).config).dataset_binding.samples.front().annotation_state == "unannotated",
        "replacement reused previous annotation");
    reopened.save_project();
    reopened.clear_dataset_dependents({source});
    deleting.remove_node(source);
    reopened.save_project();
    studio::ProjectManager verified; verified.open_project(reopened.project_file());
    const auto& remaining = verified.active_workflow().document.graph;
    require(remaining.nodes().count(source) == 0 &&
        std::get<studio::TaskNodeConfig>(remaining.node(owner).config).dataset_binding.samples.empty() &&
        std::get<studio::TaskNodeConfig>(remaining.node(detached).config).runtime.results.size() == 1 &&
        std::get<studio::TaskNodeConfig>(remaining.node(detached).config).dataset_binding.samples.front().annotation_state == "annotated",
        "delete/reopen damaged detached Task or restored cleared data");
}

void inference_persistence_roundtrip(const studio::ProjectModel& prototype) {
    const auto require = [](bool value, const char* message) { if (!value) throw std::runtime_error(message); };
    QTemporaryDir fixtures;
    require(fixtures.isValid(), "inference fixtures unavailable");
    const auto image = fixtures.filePath("input.png");
    QImage input(6, 4, QImage::Format_RGB888); input.fill(Qt::red);
    require(input.save(image), "cannot create inference input");
    studio::ProjectManager project;
    auto& graph = project.active_workflow().document.graph;
    const int source = graph.add_node(studio::NodeKind::DatasetInput);
    auto& dataset = std::get<studio::DatasetConfig>(graph.node(source).config);
    dataset.items = {{image.toStdString(), {}, true, "stable-sample"}};
    const int first = graph.add_task_node({"sg"}), second = graph.add_task_node({"sg"});
    graph.connect(source, 0, first, 0); graph.connect(source, 0, second, 0);
    project.active_workflow().document.positions = {{source, {0, 0}}, {first, {200, 0}}, {second, {200, 200}}};
    project.save_project(fixtures.filePath("Project"));
    for (int owner : {first, second}) {
        studio::TaskWorkspaceService service(project.active_workflow().document.graph, owner);
        auto model = prototype;
        model.asset_directory = QDir(project.node_models_directory(owner)).filePath("owned").toStdString();
        model.weights.front().path = QDir(QString::fromStdString(model.asset_directory)).filePath("best.pt").toStdString();
        auto alternate = model.weights.front();
        alternate.id = "libtorch_alternate";
        alternate.role = jocelin::model::WeightRole::Inference;
        alternate.parent_weight_id = "libtorch_fp32";
        model.weights.push_back(alternate);
        auto empty = alternate; empty.id = "no_result"; model.weights.push_back(empty);
        auto openvino = alternate; openvino.id = "openvino_fp32";
        openvino.format = jocelin::config::WeightType::OpenVino;
        openvino.supported_device_types = {jocelin::config::DeviceType::Cpu};
        const QDir assets(QString::fromStdString(model.asset_directory));
        require(QDir().mkpath(assets.absolutePath()), "cannot create IR fixture directory");
        openvino.path = assets.filePath("openvino_fp32.xml").toStdString();
        openvino.normalization.emplace();
        studio::ProjectStorage::write_atomic(assets.filePath("openvino_fp32.xml"), "IR fixture");
        studio::ProjectStorage::write_atomic(assets.filePath("openvino_fp32.bin"), "IR weights fixture");
        model.weights.push_back(openvino);
        require(service.add_model(model).id == "model_1", "fixture model identity changed");
    }
    const auto record = [&](int owner, const std::string& model_id, const std::string& branch, double confidence,
                            jocelin::config::WeightType format = jocelin::config::WeightType::LibTorch) {
        const auto directory = QDir(project.root_directory()).filePath("nodes/" + project.active_workflow().id + "/" +
            studio::inference_result_directory(owner, model_id, branch));
        require(QDir().mkpath(directory), "cannot create branch artifacts");
        jocelin::result::sg::SgResult value;
        value.image_path = image.toStdString(); value.result_name = "sample";
        value.save_dir = directory.toStdString(); value.image_width = 6; value.image_height = 4;
        value.mean_confidence = confidence;
        value.mask_path = QDir(directory).filePath("sample_mask.png").toStdString();
        require(input.save(QString::fromStdString(value.mask_path)), "cannot write prediction artifact");
        jocelin::result::sg::SgInferenceResult report; report.samples.push_back(value);
        studio::TaskWorkspaceService(project.active_workflow().document.graph, owner).record_inference_result(
            report, model_id, branch, format, directory.toStdString(), {});
        return directory;
    };
    const auto first_dir = record(first, "model_1", "libtorch_fp32", 0.2);
    const auto alternate_dir = record(first, "model_1", "libtorch_alternate", 0.4);
    const auto peer_dir = record(second, "model_1", "libtorch_fp32", 0.6);
    record(first, "model_1", "openvino_fp32", 0.7, jocelin::config::WeightType::OpenVino);
    project.save_project();
    const auto alternate_json = studio::ProjectStorage::read(QDir(alternate_dir).filePath("result.json"));
    const auto peer_json = studio::ProjectStorage::read(QDir(peer_dir).filePath("result.json"));
    require(!alternate_json.contains("result_id") && alternate_json.contains("stable-sample"), "duplicate result identity persisted");
    record(first, "model_1", "libtorch_fp32", 0.9);
    project.save_project();
    require(studio::ProjectStorage::read(QDir(alternate_dir).filePath("result.json")) == alternate_json &&
        studio::ProjectStorage::read(QDir(peer_dir).filePath("result.json")) == peer_json,
        "rerunning one branch overwrote another branch or Node");
    const auto old_json = studio::ProjectStorage::read(QDir(first_dir).filePath("result.json"));
    const auto old_mask = studio::ProjectStorage::read(QDir(first_dir).filePath("sample_mask.png"));
    const auto make_pending = [&] {
        const auto staging = project.prepare_inference_directory(first, "model_1", "libtorch_fp32");
        auto value = std::get<jocelin::result::sg::SgResult>(
            studio::TaskWorkspaceService(project.active_workflow().document.graph, first)
                .inference_result("model_1", "libtorch_fp32")->samples.front().result);
        value.mean_confidence = 0.95;
        value.save_dir = staging.toStdString();
        value.mask_path = QDir(staging).filePath("sample_mask.png").toStdString();
        value.metadata_path = QDir(staging).filePath("sample_meta.json").toStdString();
        QImage next = input; next.fill(Qt::blue);
        require(next.save(QString::fromStdString(value.mask_path)) && next.save(QDir(staging).filePath("sample_heatmap.png")),
            "cannot write pending artifacts");
        studio::ProjectStorage::write_atomic(QString::fromStdString(value.metadata_path), QJsonDocument(QJsonObject{
            {"heatmap_path", QDir(staging).filePath("sample_heatmap.png")}}).toJson());
        jocelin::result::sg::SgInferenceResult batch; batch.samples.push_back(value);
        return std::make_pair(staging, jocelin::inference::InferenceReport{batch});
    };
    auto pending = make_pending();
    bool wrong_scope = false;
    try { project.discard_inference_directory(second, "model_1", "libtorch_fp32", pending.first); }
    catch (const std::runtime_error&) { wrong_scope = true; }
    require(wrong_scope && QDir(pending.first).exists(), "cleanup accepted another Node's pending branch");
    // A failed worker discards only pending output, without publishing records or files.
    project.discard_inference_directory(first, "model_1", "libtorch_fp32", pending.first);
    require(!QDir(pending.first).exists() && studio::ProjectStorage::read(QDir(first_dir).filePath("result.json")) == old_json &&
        studio::ProjectStorage::read(QDir(first_dir).filePath("sample_mask.png")) == old_mask,
        "failed inference changed the previous committed branch");
    pending = make_pending();
    const auto workflow_file = QDir(project.root_directory()).filePath("workflows/" + project.active_workflow().id + ".json");
    const auto saved_workflow = workflow_file + ".test-backup";
    require(QFile::rename(workflow_file, saved_workflow) && QDir().mkpath(workflow_file), "cannot inject reference commit failure");
    bool commit_failed = false;
    try { project.commit_inference_result(first, "model_1", "libtorch_fp32", pending.second, pending.first); }
    catch (const std::runtime_error&) { commit_failed = true; }
    require(QDir().rmdir(workflow_file) && QFile::rename(saved_workflow, workflow_file), "cannot restore Workflow test fixture");
    project.discard_inference_directory(first, "model_1", "libtorch_fp32", pending.first);
    require(commit_failed && studio::ProjectStorage::read(QDir(first_dir).filePath("result.json")) == old_json &&
        studio::ProjectStorage::read(QDir(first_dir).filePath("sample_mask.png")) == old_mask &&
        std::get<jocelin::result::sg::SgResult>(studio::TaskWorkspaceService(project.active_workflow().document.graph, first)
            .inference_result("model_1", "libtorch_fp32")->samples.front().result).mean_confidence == 0.9,
        "failed branch commit did not retain the previous files and record");
    studio::ProjectManager previous; previous.open_project(project.project_file());
    pending = make_pending();
    project.commit_inference_result(first, "model_1", "libtorch_fp32", pending.second, pending.first);
    require(!QDir(pending.first).exists() && studio::ProjectStorage::read(QDir(first_dir).filePath("sample_mask.png")) != old_mask &&
        studio::ProjectStorage::read(QDir(alternate_dir).filePath("result.json")) == alternate_json &&
        studio::ProjectStorage::read(QDir(peer_dir).filePath("result.json")) == peer_json,
        "safe replacement failed or touched another branch/Node");
    require(!studio::ProjectStorage::read(QDir(first_dir).filePath("sample_meta.json")).contains(".pending-"),
        "published Core artifact metadata retained pending paths");
    studio::ProjectManager reopened; reopened.open_project(project.project_file());
    studio::TaskWorkspaceService restored(reopened.active_workflow().document.graph, first,
        QFileInfo(reopened.node_models_directory(first)).absolutePath());
    const auto score = [](const studio::InferenceResultSet* result) {
        if (!result || result->samples.size() != 1) throw std::runtime_error("missing restored branch");
        return std::get<jocelin::result::sg::SgResult>(result->samples.front().result).mean_confidence;
    };
    require(restored.task().runtime.results.size() == 3 && score(restored.inference_result("model_1", "libtorch_fp32")) == 0.95 &&
        score(restored.inference_result("model_1", "libtorch_alternate")) == 0.4 &&
        score(restored.inference_result("model_1", "openvino_fp32")) == 0.7 &&
        score(studio::TaskWorkspaceService(reopened.active_workflow().document.graph, second)
            .inference_result("model_1", "libtorch_fp32")) == 0.6, "branch results did not reopen independently");
    restored.select_inference_result("model_1", "libtorch_fp32");
    require(score(restored.selected_inference_result()) == 0.95, "selection did not load its exact branch");
    restored.select_inference_result("model_1", "no_result");
    require(!restored.selected_inference_result(), "empty branch fell back to another branch");
    const auto valid = studio::ProjectStorage::read(QDir(first_dir).filePath("result.json"));
    auto invalid = QJsonDocument::fromJson(valid).object(); invalid["weight_id"] = "foreign";
    studio::ProjectStorage::write_atomic(QDir(first_dir).filePath("result.json"), QJsonDocument(invalid).toJson());
    bool rejected = false;
    try { reopened.open_project(project.project_file()); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "reopen accepted a foreign branch association");
    studio::ProjectStorage::write_atomic(QDir(first_dir).filePath("result.json"), valid);
    const auto model_file = QDir(reopened.node_models_directory(first)).filePath("model_list.json");
    const auto saved_models = studio::ProjectStorage::read(model_file);
    auto foreign_models = QJsonDocument::fromJson(saved_models).object();
    auto entries = foreign_models.value("models").toArray();
    auto foreign_model = entries[0].toObject(); foreign_model["id"] = "model_99";
    entries[0] = foreign_model; foreign_models["models"] = entries;
    studio::ProjectStorage::write_atomic(model_file, QJsonDocument(foreign_models).toJson());
    rejected = false;
    try { reopened.open_project(project.project_file()); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "reopen resolved a model from a different Node");
    studio::ProjectStorage::write_atomic(model_file, saved_models);
    require(QFile::remove(model_file), "cannot remove inference owner's ModelList fixture");
    studio::ProjectManager missing_list_project;
    const auto missing_list = missing_list_project.open_project(project.project_file());
    require(std::any_of(missing_list.begin(), missing_list.end(), [first](const auto& issue) {
        return issue.node_id == first && issue.model_id.isEmpty();
    }) && studio::TaskWorkspaceService(missing_list_project.active_workflow().document.graph, first)
        .task().runtime.results.size() == 3 &&
        studio::ProjectStorage::read(QDir(alternate_dir).filePath("result.json")) == alternate_json,
        "missing catalog deleted or rejected orphaned inference");
    missing_list_project.save_project();
    require(!QFileInfo::exists(model_file), "save silently recreated a missing catalog with unresolved inference");
    missing_list_project.open_project(project.project_file());
    studio::ProjectStorage::write_atomic(model_file, saved_models);
    const auto additional = restored.add_model(*restored.find_model("model_1"));
    auto remaining = *restored.inference_result("model_1", "libtorch_fp32");
    remaining.model_id = additional.id;
    remaining.result_directory = QDir(reopened.root_directory()).filePath("nodes/" + reopened.active_workflow().id + "/" +
        studio::inference_result_directory(first, additional.id, remaining.weight_id)).toStdString();
    auto& remaining_value = std::get<jocelin::result::sg::SgResult>(remaining.samples.front().result);
    remaining_value.save_dir = remaining.result_directory;
    remaining_value.mask_path.clear();
    remaining_value.metadata_path.clear();
    restored.task().runtime.results.push_back(remaining);
    project.save_project_as("Copy", fixtures.path());
    studio::ProjectManager copy; copy.open_project(project.project_file());
    const auto* copied = studio::TaskWorkspaceService(copy.active_workflow().document.graph, first)
        .inference_result("model_1", "libtorch_alternate");
    require(copied && score(copied) == 0.4 &&
        QDir::fromNativeSeparators(QString::fromStdString(copied->result_directory)).startsWith(
            QDir::fromNativeSeparators(copy.root_directory()) + "/") &&
        studio::ProjectStorage::read(QString::fromStdString(std::get<jocelin::result::sg::SgResult>(copied->samples.front().result).mask_path)) ==
            studio::ProjectStorage::read(QDir(alternate_dir).filePath("sample_mask.png")),
        "Save As failed to relocate Node inference assets");
    reopened.remove_models(first, {"model_1"}, "libtorch_alternate");
    require(!restored.inference_result("model_1", "libtorch_alternate") && !QDir(alternate_dir).exists() &&
        restored.inference_result("model_1", "libtorch_fp32") && QDir(first_dir).exists() &&
        studio::ProjectStorage::read(QDir(peer_dir).filePath("result.json")) == peer_json,
        "weight deletion crossed inference branch or Node boundaries");
    const auto* ir_result = restored.inference_result("model_1", "openvino_fp32");
    require(ir_result, "OpenVINO branch result is missing before deletion");
    const QString ir_results_directory = QString::fromStdString(ir_result->result_directory);
    const QDir ir_assets(QString::fromStdString(restored.find_model("model_1")->asset_directory));
    reopened.remove_models(first, {"model_1"}, "openvino_fp32");
    require(!restored.inference_result("model_1", "openvino_fp32") && !QDir(ir_results_directory).exists() &&
        QFileInfo::exists(ir_assets.filePath("openvino_fp32.xml")) &&
        QFileInfo::exists(ir_assets.filePath("openvino_fp32.bin")), "deleting an OpenVINO branch damaged a shared asset or kept a result");
    reopened.remove_models(first, {QString::fromStdString(additional.id)}, "openvino_fp32");
    require(
        !QFileInfo::exists(ir_assets.filePath("openvino_fp32.xml")) &&
        !QFileInfo::exists(ir_assets.filePath("openvino_fp32.bin")), "deleting the last OpenVINO reference left an asset");
    const QString copied_model_list = QDir(copy.node_models_directory(first)).filePath("model_list.json");
    require(QFile::remove(copied_model_list), "cannot remove copied ModelList");
    copy.open_project(copy.project_file());
    copy.remove_models(first);
    copy.open_project(copy.project_file());
    require(studio::TaskWorkspaceService(copy.active_workflow().document.graph, first).task().runtime.results.empty() &&
        !QDir(QFileInfo(copy.node_models_directory(first)).absolutePath() + "/inference").exists(),
        "missing ModelList cleanup did not persist removal of orphaned inference");
    require(restored.remove_model("model_1") && restored.task().runtime.results.size() == 1 &&
        restored.task().runtime.results.front().model_id == additional.id && !QDir(first_dir).exists() && !QDir(alternate_dir).exists(),
        "model delete did not remove exactly its inference directory and records");
    reopened.save_project();
    require(QDir(peer_dir).exists(), "model delete removed another Node's inference directory");
    require(score(studio::TaskWorkspaceService(reopened.active_workflow().document.graph, second)
        .inference_result("model_1", "libtorch_fp32")) == 0.6, "model delete crossed Node scope");
    restored.clear_models();
    require(!QDir(QFileInfo(reopened.node_models_directory(first)).absolutePath() + "/inference").exists() && QDir(peer_dir).exists(),
        "clear did not remove only the current Node inference directory");
    reopened.save_project();
    studio::ProjectManager cleared; cleared.open_project(reopened.project_file());
    require(studio::TaskWorkspaceService(cleared.active_workflow().document.graph, first).task().runtime.results.empty() &&
        score(studio::TaskWorkspaceService(cleared.active_workflow().document.graph, second)
            .inference_result("model_1", "libtorch_fp32")) == 0.6, "clear did not persist Node isolation");
}

void model_list_ownership_roundtrip(const studio::ProjectModel& prototype) {
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    QTemporaryDir fixtures;
    require(fixtures.isValid(), "model ownership fixtures unavailable");
    studio::ProjectManager project;
    auto& first_graph = project.active_workflow().document.graph;
    const int first = first_graph.add_task_node({"sg"});
    const int second = first_graph.add_task_node({"sg"});
    project.active_workflow().document.positions = {{first, {0, 0}}, {second, {200, 0}}};
    project.add_workflow("Independent models");
    const int other = project.active_workflow().document.graph.add_task_node({"sg"});
    project.active_workflow().document.positions = {{other, {0, 0}}};
    require(first == other, "fixture must reuse a Node id across Workflows");
    project.save_project(fixtures.filePath("Project"));
    studio::ProjectManager empty;
    empty.open_project(project.project_file());
    for (const auto& workflow : empty.workflows())
        for (const auto& [node_id, node] : workflow.document.graph.nodes()) {
            const auto& list = std::get<studio::TaskNodeConfig>(node.config).model_list;
            require(list.empty() && list.next_number() == 1, "empty Node ModelList did not reopen");
        }

    // Two Nodes with the same task type, plus the same Node id in another Workflow.
    for (size_t workflow_index = 0; workflow_index < 2; ++workflow_index) {
        project.set_active_index(workflow_index);
        for (const int node_id : workflow_index == 0 ? std::vector<int>{first, second} : std::vector<int>{other}) {
            studio::TaskWorkspaceService service(project.active_workflow().document.graph, node_id);
            require(service.models().empty(), "new TaskNode must own an empty ModelList");
            auto model = prototype;
            model.image_width = 128 + static_cast<int>(workflow_index) * 128 + node_id;
            model.asset_directory = QDir(project.node_models_directory(node_id)).filePath("owned").toStdString();
            require(QDir().mkpath(QString::fromStdString(model.asset_directory)), "cannot create Node model assets");
            model.weights.front().path = QDir(QString::fromStdString(model.asset_directory)).filePath("best.pt").toStdString();
            studio::ProjectStorage::write_atomic(QString::fromStdString(model.weights.front().path), "owned weight");
            const auto added = service.add_model(model);
            require(added.id == "model_1" && added.number == 1, "ModelList numbering must be Node-local");
            auto converted = added.weights.front();
            converted.id = "onnx_fp16";
            converted.format = jocelin::config::WeightType::Onnx;
            converted.precision = jocelin::model::WeightPrecision::Float16;
            converted.role = jocelin::model::WeightRole::Inference;
            converted.parent_weight_id = added.weights.front().id;
            converted.path = QDir(QString::fromStdString(model.asset_directory)).filePath("model.onnx").toStdString();
            converted.runtime_name = "onnx";
            converted.normalization.emplace();
            studio::ProjectStorage::write_atomic(QString::fromStdString(converted.path), "derived weight");
            service.task().model_list.add_weight(added.id, converted);
            service.find_model("model_1")->metric = 0.25 + 0.1 * node_id;
            service.record_training_started(added.id);
            auto wrong_task = model;
            wrong_task.task = jocelin::config::ToolType::Cls;
            bool rejected = false;
            try { service.add_model(wrong_task); }
            catch (const std::runtime_error&) { rejected = true; }
            require(rejected && service.models().size() == 1, "TaskNode accepted a different task's model");
        }
    }
    project.set_active_index(0);
    studio::TaskWorkspaceService service(project.active_workflow().document.graph, first);
    auto extra = *service.find_model("model_1");
    require(service.add_model(extra).number == 2, "model creation did not advance Node sequence");
    require(service.remove_model("model_1") && !service.find_model("model_1") &&
        service.task().runtime.training_model_id.empty(), "model removal did not clear its training association");
    require(!service.remove_model("missing"), "removing a missing model must report false");
    service.clear_models();
    require(service.models().empty(), "model clear did not empty its Node");
    require(service.add_model(extra).number == 3, "model clear reset the allocation sequence");
    require(studio::TaskWorkspaceService(project.active_workflow().document.graph, second).models().size() == 1,
        "CRUD in one TaskNode changed another Node's ModelList");
    project.mark_active_dirty();
    project.save_project();
    require(!QFileInfo::exists(QDir(project.root_directory()).filePath("models/model_list.json")),
        "Project still writes a global ModelList");

    studio::ProjectManager reopened;
    reopened.open_project(project.project_file());
    for (size_t workflow_index = 0; workflow_index < 2; ++workflow_index) {
        reopened.set_active_index(workflow_index);
        for (const int node_id : workflow_index == 0 ? std::vector<int>{first, second} : std::vector<int>{other}) {
            studio::TaskWorkspaceService restored(reopened.active_workflow().document.graph, node_id);
            const QString id = workflow_index == 0 && node_id == first ? "model_3" : "model_1";
            const auto* model = restored.find_model(id);
            require(restored.models().size() == 1 && model && model->weights.size() == 2 &&
                model->metric == 0.25 + 0.1 * node_id &&
                model->image_width == 128 + static_cast<int>(workflow_index) * 128 + node_id &&
                model->weights.back().id == "onnx_fp16" &&
                model->weights.back().parent_weight_id == "libtorch_fp32" &&
                model->weights.back().precision == jocelin::model::WeightPrecision::Float16,
                "reopen lost or mixed Node model data and branches");
            const auto* training = jocelin::model::find_training_weight(*model);
            require(training && studio::ProjectStorage::read(QString::fromStdString(training->path)) == "owned weight" &&
                QDir::cleanPath(QString::fromStdString(model->asset_directory)) ==
                    QDir(reopened.node_models_directory(node_id)).filePath("owned"),
                "reopen did not resolve model assets in the owning Node");
            require(restored.task().dataset_binding.source_node_id == 0, "fixture unexpectedly connected a Dataset");
        }
    }
    reopened.set_active_index(0);
    require(std::get<studio::TaskNodeConfig>(reopened.active_workflow().document.graph.node(first).config)
        .model_list.next_number() == 4, "reopen lost the Node model allocation sequence");
    const QString path = QDir(reopened.node_models_directory(second)).filePath("model_list.json");
    const auto valid = studio::ProjectStorage::read(path);
    auto invalid = QJsonDocument::fromJson(valid).object();
    auto models = invalid.value("models").toArray();
    auto model = models[0].toObject();
    model["task"] = "cls";
    models[0] = model;
    invalid["models"] = models;
    studio::ProjectStorage::write_atomic(path, QJsonDocument(invalid).toJson());
    const auto previous_id = reopened.metadata().project_id;
    bool rejected = false;
    try { reopened.open_project(project.project_file()); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected && reopened.metadata().project_id == previous_id &&
        studio::TaskWorkspaceService(reopened.active_workflow().document.graph, first).find_model("model_3"),
        "invalid Node ModelList was accepted or replaced the open Project");
    studio::ProjectStorage::write_atomic(path, valid);
    require(reopened.open_project(project.project_file()).empty(), "valid model assets reported missing");
    const auto* checked_model = studio::TaskWorkspaceService(
        reopened.active_workflow().document.graph, second).find_model("model_1");
    const QString training_path = QString::fromStdString(checked_model->weights.front().path);
    const QString converted_path = QString::fromStdString(checked_model->weights.back().path);
    require(QFile::remove(converted_path), "cannot remove converted asset fixture");
    auto missing = reopened.open_project(project.project_file());
    require(missing.size() == 1 && missing.front().node_id == second &&
        missing.front().model_id == "model_1" && missing.front().weight_ids == QStringList{"onnx_fp16"} &&
        !missing.front().all_weights_missing && studio::ProjectStorage::read(path) == valid && !reopened.is_dirty(),
        "missing weight did not open readably without changing the catalog");
    require(QFile::remove(training_path), "cannot remove training asset fixture");
    missing = reopened.open_project(project.project_file());
    require(missing.size() == 1 && missing.front().all_weights_missing,
        "entirely missing model was not diagnosed");
    require(QFile::remove(path), "cannot remove ModelList fixture");
    missing = reopened.open_project(project.project_file());
    require(missing.size() == 1 && missing.front().model_id.isEmpty() &&
        studio::TaskWorkspaceService(reopened.active_workflow().document.graph, second).models().empty() &&
        !QFileInfo::exists(path), "missing ModelList did not open empty without being recreated");
    studio::ProjectStorage::write_atomic(path, "{broken");
    rejected = false;
    try { reopened.open_project(project.project_file()); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "corrupt ModelList was treated as missing");
    model = QJsonDocument::fromJson(valid).object().value("models").toArray()[0].toObject();
    model["creation_mode"] = "import";
    models = {model};
    invalid = QJsonDocument::fromJson(valid).object();
    invalid["models"] = models;
    studio::ProjectStorage::write_atomic(path, QJsonDocument(invalid).toJson());
    rejected = false;
    try { reopened.open_project(project.project_file()); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "illegal lifecycle metadata was treated as missing assets");
    studio::ProjectStorage::write_atomic(path, valid);
    studio::ProjectStorage::write_atomic(training_path, "owned weight");
    studio::ProjectStorage::write_atomic(converted_path, "derived weight");
    reopened.open_project(project.project_file());
    const auto status = studio::TaskWorkspaceService(reopened.active_workflow().document.graph, second)
        .find_model("model_1")->training_status;
    reopened.remove_models(second, {"model_1"}, "libtorch_fp32");
    require(reopened.open_project(project.project_file()).empty(), "training branch deletion did not reopen");
    const auto* retained = studio::TaskWorkspaceService(reopened.active_workflow().document.graph, second).find_model("model_1");
    require(retained && retained->weights.size() == 1 && retained->weights.front().id == "onnx_fp16" &&
        retained->weights.front().parent_weight_id.empty() && retained->training_status == status &&
        !QFileInfo::exists(training_path) && QFileInfo::exists(converted_path),
        "training branch deletion changed status or removed its independent child");
    reopened.remove_models(second, {"model_1"}, "onnx_fp16");
    reopened.open_project(project.project_file());
    require(studio::TaskWorkspaceService(reopened.active_workflow().document.graph, second)
        .find_model("model_1")->weights.empty(), "empty model row did not survive reopen");
    reopened.remove_models(second, {"model_1"});
    require(!QDir(QFileInfo(converted_path).absolutePath()).exists() &&
        studio::TaskWorkspaceService(reopened.active_workflow().document.graph, first).find_model("model_3"),
        "model removal did not clean assets in only its owning Node");
}

void annotation_persistence_roundtrip() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    QTemporaryDir fixtures;
    require(fixtures.isValid(), "annotation fixtures unavailable");
    QImage image(16, 12, QImage::Format_RGB888);
    image.fill(Qt::red);
    const QString first = fixtures.filePath("first.png"), second = fixtures.filePath("second.png");
    const QString third = fixtures.filePath("third.png");
    require(image.save(first) && image.save(second) && image.save(third), "cannot create sample inputs");
    studio::ProjectManager project;
    const auto temporary_workflow = project.add_workflow("Temporary");
    const QString removed_workflow_id = project.active_workflow().id;
    project.remove_workflow(temporary_workflow);
    const auto replacement_workflow = project.add_workflow("Replacement");
    require(project.active_workflow().id != removed_workflow_id, "Workflow namespace was recycled");
    project.remove_workflow(replacement_workflow);
    auto& document = project.active_workflow().document;
    auto& graph = document.graph;
    const int source_node_id = graph.add_node(studio::NodeKind::DatasetInput);
    const int removed_node_id = graph.add_node(studio::NodeKind::DatasetInput);
    graph.remove_node(removed_node_id);
    auto& source_config = std::get<studio::DatasetConfig>(graph.node(source_node_id).config);
    source_config.items = {{first.toStdString(), {}, true, "shared"}, {second.toStdString(), {}, true, "pending"}, {third.toStdString(), {}, true, "SHARED"}};
    const int node_id = graph.add_task_node({"cls"});
    const int branch_node_id = graph.add_task_node({"sg"});
    const int peer_node_id = graph.add_task_node({"sg"});
    for (int target_node_id : {node_id, branch_node_id, peer_node_id}) graph.connect(source_node_id, 0, target_node_id, 0);
    const int deleted_node_id = graph.add_task_node({"sg"});
    graph.remove_node(deleted_node_id);
    for (const auto& [node_id, node] : graph.nodes()) document.positions.emplace(node_id, QPointF(node_id * 100, 0));
    studio::TaskWorkspaceService classification(graph, node_id), foreground(graph, branch_node_id), empty(graph, peer_node_id);
    const auto cat = classification.add_class("cat");
    classification.add_class("dog");
    classification.set_class_for_sample("shared", cat);
    foreground.add_class("foreground");
    empty.add_class("foreground");
    QImage mask(16, 12, QImage::Format_Grayscale8);
    mask.fill(0);
    mask.scanLine(2)[3] = 1;
    const QString external_mask = fixtures.filePath("external-mask.png");
    require(mask.save(external_mask), "cannot create import target");
    studio::import_segmentation_annotation(foreground.binding().classes,
        foreground.binding().samples.front(), external_mask);
    const auto expected_runs = foreground.binding().samples.front().regions.front().vRun;
    QImage distinct_target = mask;
    distinct_target.fill(1);
    foreground.set_segmentation_mask("SHARED", distinct_target);
    require(QFile::remove(external_mask), "cannot remove original annotation source");
    empty.set_segmentation_sample_empty("shared");
    for (auto* service : {&classification, &foreground, &empty})
        service->binding().samples.front().quality_issues = {"stale diagnostic"};
    const auto task_id = foreground.task().runtime.task_id;
    const QString root = fixtures.filePath("ProjAnnotation");
    project.save_project(root);
    const QString workflow_path = QDir(root).filePath("workflows/workflow_1.json");
    const auto bytes = studio::ProjectStorage::read(workflow_path);
    require(!bytes.contains("quality_issues"), "workflow persisted derived diagnostics");
    require(!bytes.contains("\"task_id\"") && !bytes.contains("class_label") && !bytes.contains("\"mask_path\""),
        "workflow contains runtime identity or adapter target");
    const auto nodes = QJsonDocument::fromJson(bytes).object().value("nodes").toArray();
    for (const auto& value : nodes) {
        const auto node = value.toObject();
        if (node.value("id").toInt() != branch_node_id) continue;
        const auto binding = node.value("config").toObject().value("dataset_binding").toObject();
        const auto sample = binding.value("samples").toArray().at(0).toObject();
        require(sample.contains("image_paths") && !sample.contains("class_id"), "SG bucket snapshot or target fields are incorrect");
        require(QFileInfo(QDir(root).filePath("nodes/workflow_1/" + sample.value("region_file").toString())).isFile(),
            "Node region asset missing");
    }
    require(foreground.task().runtime.task_id == task_id, "ordinary save replaced runtime Task");
    studio::ProjectManager reopened;
    reopened.open_project(root);
    auto& restored_graph = reopened.active_workflow().document.graph;
    studio::TaskWorkspaceService restored_cls(restored_graph, node_id), restored_fg(restored_graph, branch_node_id),
        restored_empty(restored_graph, peer_node_id);
    for (const auto* service : {&restored_cls, &restored_fg, &restored_empty})
        require(service->binding().samples.front().quality_issues.empty(), "stale diagnostics survived recovery");
    require(restored_graph.connections().size() == 3 && restored_fg.source_node_id() == source_node_id,
        "shared DataNode relationship changed");
    require(restored_cls.binding().samples.front().sample_id == "shared" &&
        restored_cls.binding().samples.front().class_id == cat &&
        restored_cls.binding().samples.front().annotation_state == "annotated", "Classification target changed");
    require(restored_fg.binding().samples.front().regions.front().vRun == expected_runs &&
        restored_fg.binding().samples.front().annotation_state == "annotated", "SG foreground target changed");
    require(restored_fg.binding().samples[2].sample_id == "SHARED" &&
        restored_fg.binding().samples[2].regions.front().vRun != expected_runs,
        "sample identities collided through filesystem case folding");
    require(restored_empty.binding().samples.front().regions.empty() &&
        restored_empty.binding().samples.front().annotation_state == "annotated" &&
        restored_empty.binding().samples[1].regions.empty() &&
        restored_empty.binding().samples[1].annotation_state == "unannotated", "empty annotations lost their state distinction");
    require(restored_fg.task().runtime.task_id != task_id, "runtime Task identity was persisted");
    require(restored_graph.add_task_node({"cls"}) > deleted_node_id, "persistent Node identity was recycled");
    restored_graph.remove_node(restored_graph.nodes().rbegin()->first);
    QImage changed = mask;
    changed.fill(1);
    restored_empty.set_segmentation_mask("shared", changed);
    require(restored_fg.binding().samples.front().regions.front().vRun == expected_runs,
        "editing one TaskNode overwrote another annotation");
    reopened.save_project();
    studio::ProjectManager verified;
    verified.open_project(root);
    const auto& fg = std::get<studio::TaskNodeConfig>(verified.active_workflow().document.graph.node(branch_node_id).config);
    const auto& independent = std::get<studio::TaskNodeConfig>(verified.active_workflow().document.graph.node(peer_node_id).config);
    require(fg.dataset_binding.samples.front().regions.front().vRun == expected_runs &&
        independent.dataset_binding.samples.front().regions.front().vRun != expected_runs,
        "Node-owned spatial assets collided on save");
    auto invalid_workflow = verified;
    studio::TaskWorkspaceService(invalid_workflow.active_workflow().document.graph, branch_node_id)
        .set_segmentation_mask("shared", changed);
    invalid_workflow.add_workflow("Missing layout");
    invalid_workflow.active_workflow().document.graph.add_node(studio::NodeKind::DatasetInput);
    const auto saved_workflow = studio::ProjectStorage::read(workflow_path);
    const QString saved_region_path = QDir(root).filePath("nodes/workflow_1/" +
        QString::number(branch_node_id) + "/annotations/736861726564.regions.json");
    const auto saved_region = studio::ProjectStorage::read(saved_region_path);
    bool preparation_failed = false;
    try { invalid_workflow.save_project(); }
    catch (const std::runtime_error&) { preparation_failed = true; }
    require(preparation_failed && invalid_workflow.is_dirty() &&
        studio::ProjectStorage::read(workflow_path) == saved_workflow &&
        studio::ProjectStorage::read(saved_region_path) == saved_region,
        "later Workflow serialization failure modified saved Annotation");
    const QString unsaved_root = fixtures.filePath("PrepareOnly");
    preparation_failed = false;
    try { invalid_workflow.save_project(unsaved_root); }
    catch (const std::runtime_error&) { preparation_failed = true; }
    require(preparation_failed && !QFileInfo::exists(unsaved_root),
        "save preparation created Project files or directories");
    require(QFile::remove(third), "cannot remove input for diagnostic recovery check");
    studio::ProjectManager missing_input;
    missing_input.open_project(root);
    const auto& diagnosed = std::get<studio::TaskNodeConfig>(
        missing_input.active_workflow().document.graph.node(branch_node_id).config).dataset_binding.samples;
    require(diagnosed.front().quality_issues.empty() &&
        diagnosed[2].quality_issues == std::vector<std::string>{"source image is missing or unreadable"},
        "Project recovery did not recalculate current input diagnostics");
}
} // namespace

int main(int argc, char** argv) try {
    QCoreApplication application(argc, argv);
    annotation_persistence_roundtrip();
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    QDir root(QDir(temporary.path()).filePath("Proj1"));
    root.mkpath("datasets/images");
    root.mkpath("annotations");
    root.mkpath("models");
    root.mkpath("results");
    root.mkpath("nodes/workflow_1/2/inference/model_1/libtorch_fp32");
    for (const auto& relative : {
             "datasets/images/input.png",
             "annotations/input.png",
             "models/checkpoint.pt",
             "nodes/workflow_1/2/inference/model_1/libtorch_fp32/input_mask.png",
             "nodes/workflow_1/2/inference/model_1/libtorch_fp32/input_color.png",
             "nodes/workflow_1/2/inference/model_1/libtorch_fp32/input_meta.json"}) {
        QFile file(root.filePath(relative));
        if (!file.open(QIODevice::WriteOnly) ||
            file.write("test") < 0)
            return 2;
    }

    studio::ProjectManager project;
    project.set_project_name("Inspection Project");
    project.set_project_description(
        "Project persistence test");
    auto& workflow = project.active_workflow();
    workflow.name = "Single Task";
    const int source_node_id =
        workflow.document.graph.add_node(
            studio::NodeKind::DatasetInput);
    const int node_id = workflow.document.graph.add_task_node({"sg"});
    auto& dataset_config =
        std::get<studio::DatasetConfig>(
            workflow.document.graph.node(source_node_id).config);
    dataset_config.directory =
        root.filePath("datasets/images").toStdString();
    dataset_config.items = {{
        root.filePath("datasets/images/input.png").toStdString(),
        {}, true, "sample-input"}};
    workflow.document.graph.connect(
        source_node_id, 0, node_id, 0);
    auto& task =
        std::get<studio::TaskNodeConfig>(
            workflow.document.graph.node(node_id).config);
    task.model_index = 0;
    task.model_scale = "s";
    task.training.output_directory =
        root.filePath("models").toStdString();
    task.inference_output_directory =
        root.filePath("results").toStdString();
    task.dataset_binding.classes = {{
        "foreground", 1, "foreground", "#1F8A9E",
        true, false}};
    task.dataset_binding.samples.front().regions = {jocelin::geo::Region({{1, 1, 3, 1}})};
    task.dataset_binding.samples.front().split = "train";
    task.dataset_binding.samples.front().annotation_state =
        "annotated";
    workflow.document.positions = {
        {source_node_id, {-260, -40}},
        {node_id, {80, -40}}};

    project.mark_active_dirty();
    project.add_workflow("Second Workflow");
    project.set_active_index(0);
    studio::ProjectModel model;
    model.training_run_id = "train_contract";
    model.created_at = "2026-07-27T00:00:00Z";
    model.task = jocelin::config::ToolType::Sg;
    model.model_index = 0;
    model.model_scale = jocelin::config::ModelScale::S;
    model.classes = {{"foreground", "foreground", 1}};
    model.image_width = 256;
    model.image_height = 256;
    model.asset_directory = root.filePath("nodes/workflow_1/2/models/train_contract").toStdString();
    jocelin::model::ModelWeight training_weight;
    training_weight.id = "libtorch_fp32";
    training_weight.format = jocelin::config::WeightType::LibTorch;
    training_weight.precision = jocelin::model::WeightPrecision::Float32;
    training_weight.role = jocelin::model::WeightRole::Training;
    training_weight.path = root.filePath("nodes/workflow_1/2/models/train_contract/best.pt").toStdString();
    training_weight.created_at = model.created_at;
    training_weight.supported_device_types = {
        jocelin::config::DeviceType::Cpu,
        jocelin::config::DeviceType::Gpu};
    training_weight.runtime_name = "libtorch";
    training_weight.runtime_version = "2.8.0";
    training_weight.artifact_version = "checkpoint-v1";
    training_weight.input_signature = "NCHW float32";
    training_weight.output_signature = "segmentation logits";
    model.weights.push_back(std::move(training_weight));
    model.completed_epochs = 12;
    model.best_epoch = 7;
    model.metric_name = "mIoU";
    model.metric = 0.75;
    model.creation_mode = jocelin::model::ModelCreationMode::NewTraining;
    model_list_ownership_roundtrip(model);
    inference_persistence_roundtrip(model);
    dataset_lifecycle_roundtrip(model);
    const auto added_model = studio::TaskWorkspaceService(project.active_workflow().document.graph, node_id).add_model(model);
    if (added_model.id != "model_1" ||
        added_model.name != "Model 1" ||
        added_model.number != 1)
        return 15;
    if (project.training_runs().allocate() != 1 ||
        project.training_runs().allocate() != 2)
        return 19;
    jocelin::result::sg::SgResult inference;
    inference.image_path = root.filePath("datasets/images/input.png").toStdString();
    inference.result_name = "input";
    inference.save_dir = root.filePath("nodes/workflow_1/2/inference/model_1/libtorch_fp32").toStdString();
    inference.mask_path = root.filePath("nodes/workflow_1/2/inference/model_1/libtorch_fp32/input_mask.png").toStdString();
    inference.visualization_path = root.filePath("nodes/workflow_1/2/inference/model_1/libtorch_fp32/input_color.png").toStdString();
    inference.metadata_path = root.filePath("nodes/workflow_1/2/inference/model_1/libtorch_fp32/input_meta.json").toStdString();
    inference.image_width = 5;
    inference.image_height = 4;
    inference.min_component_area = 2;
    inference.min_component_confidence = 0.8;
    inference.predicted_area = 2;
    inference.mean_confidence = 0.9;
    inference.present_labels = {1};
    jocelin::result::sg::SgResult::Component component;
    component.label = 1;
    component.area = 2;
    component.region = jocelin::geo::Region({{1,1,3,1}});
    component.bbox = {1,1,2,3};
    component.aspect_ratio = 2.0;
    component.confidence = 0.9;
    component.area_pass = component.aspect_ratio_pass = component.confidence_pass = component.interest = true;
    inference.components.push_back(component);
    studio::ResultReference reference;
    reference.source_sample_id = "sample-input";
    reference.result = inference;
    studio::InferenceResultSet result_set;
    result_set.model_id = added_model.id;
    result_set.weight_id = "libtorch_fp32";
    result_set.result_directory = inference.save_dir;
    result_set.samples.push_back(std::move(reference));
    std::get<studio::TaskNodeConfig>(project.active_workflow().document.graph.node(node_id).config)
        .runtime.results.push_back(std::move(result_set));
    project.set_active_index(1);
    project.save_project(root.path());
    if (project.is_dirty() ||
        !QFile::exists(root.filePath("project.json")) ||
        !QFile::exists(
            root.filePath("workflows/workflow_1.json")))
        return 3;

    QFile workflow_file(
        root.filePath("workflows/workflow_1.json"));
    if (!workflow_file.open(QIODevice::ReadOnly))
        return 4;
    const QByteArray serialized =
        workflow_file.readAll();
    workflow_file.close();
    auto invalid_models = project;
    auto& changed_document = invalid_models.workflows().front().document;
    std::get<studio::TaskNodeConfig>(changed_document.graph.node(node_id).config)
        .dataset_binding.samples.front().regions = {jocelin::geo::Region({{1, 1, 4, 1}})};
    const QString region_path = root.filePath("nodes/workflow_1/2/annotations/73616d706c652d696e707574.regions.json");
    std::map<QString, QByteArray> saved_files;
    for (const auto& path : {project.project_file(), root.filePath("workflows/workflow_1.json"),
             root.filePath("nodes/workflow_1/2/models/model_list.json"), root.filePath("runs/training_runs.json"), region_path})
        saved_files.emplace(path, studio::ProjectStorage::read(path));
    const auto prepared = studio::serialize_workflow(changed_document.graph, changed_document.positions);
    if (prepared.region_assets.size() != 1 ||
        prepared.region_assets.front().second != "[[[1,1,4,1]]]") return 60;
    for (const auto& [path, data] : saved_files)
        if (studio::ProjectStorage::read(path) != data) return 61;
    studio::TaskWorkspaceService(changed_document.graph, node_id).find_model(QString::fromStdString(added_model.id))->asset_directory = "project:invalid-runtime-path";
    invalid_models.training_runs().allocate();
    bool model_preparation_failed = false;
    try { invalid_models.save_project(); }
    catch (const std::runtime_error&) { model_preparation_failed = true; }
    if (!model_preparation_failed || !invalid_models.training_runs().is_dirty()) return 62;
    for (const auto& [path, data] : saved_files)
        if (studio::ProjectStorage::read(path) != data) return 63;
    if (!serialized.contains("project:datasets/images") ||
        !serialized.contains(
            "project:datasets/images/input.png") ||
        !serialized.contains(
            "2/annotations/73616d706c652d696e707574.regions.json"))
        return 5;

    studio::ProjectManager loaded;
    loaded.open_project(root.filePath("project.json"));
    if (loaded.metadata().name != "Inspection Project" ||
        loaded.workflows().size() != 2 ||
        studio::TaskWorkspaceService(loaded.active_workflow().document.graph, node_id).models().size() != 1 ||
        loaded.active_index() != 0 ||
        loaded.is_dirty())
        return 6;
    const auto* loaded_model = studio::TaskWorkspaceService(loaded.active_workflow().document.graph, node_id).find_model("model_1");
    const auto* loaded_training_weight = loaded_model
        ? jocelin::model::find_training_weight(*loaded_model)
        : nullptr;
    if (!loaded_model || loaded_model->name != "Model 1" ||
        loaded_model->classes.size() != 1 ||
        loaded_model->completed_epochs != 12 || loaded_model->best_epoch != 7 ||
        !loaded_training_weight ||
        loaded_training_weight->supported_device_types.size() != 2 ||
        loaded_training_weight->runtime_name != "libtorch" ||
        loaded_training_weight->runtime_version != "2.8.0" ||
        loaded_training_weight->artifact_version != "checkpoint-v1" ||
        loaded_training_weight->input_signature != "NCHW float32" ||
        loaded_training_weight->output_signature != "segmentation logits" ||
        QDir::cleanPath(QString::fromStdString(loaded_training_weight->path)) !=
            QDir::cleanPath(root.filePath("nodes/workflow_1/2/models/train_contract/best.pt")))
        return 16;
    loaded.set_active_index(1);
    if (loaded.is_dirty()) return 37;
    loaded.set_active_index(0);
    if (loaded.training_runs().allocate() != 3 || !loaded.is_dirty()) return 20;
    loaded.save_project();
    if (loaded.is_dirty()) return 21;
    const auto& loaded_graph =
        loaded.active_workflow().document.graph;
    const auto& loaded_dataset =
        std::get<studio::DatasetConfig>(
            loaded_graph.node(1).config);
    const auto& loaded_task =
        std::get<studio::TaskNodeConfig>(
            loaded_graph.node(2).config);
    if (loaded_task.runtime.results.size() != 1 ||
        loaded_task.runtime.results.front().samples.size() != 1) return 33;
    const auto& restored_result = std::get<jocelin::result::sg::SgResult>(
        loaded_task.runtime.results.front().samples.front().result);
    if (QDir::cleanPath(QString::fromStdString(restored_result.image_path)) != root.filePath("datasets/images/input.png") ||
        QDir::cleanPath(QString::fromStdString(restored_result.save_dir)) != root.filePath("nodes/workflow_1/2/inference/model_1/libtorch_fp32") ||
        QDir::cleanPath(QString::fromStdString(restored_result.mask_path)) != root.filePath("nodes/workflow_1/2/inference/model_1/libtorch_fp32/input_mask.png") ||
        QDir::cleanPath(QString::fromStdString(restored_result.visualization_path)) != root.filePath("nodes/workflow_1/2/inference/model_1/libtorch_fp32/input_color.png") ||
        QDir::cleanPath(QString::fromStdString(restored_result.metadata_path)) != root.filePath("nodes/workflow_1/2/inference/model_1/libtorch_fp32/input_meta.json") ||
        restored_result.min_component_area != 2 || restored_result.min_component_confidence != 0.8 ||
        restored_result.components.size() != 1 || !restored_result.components.front().interest ||
        cv::norm(restored_result.render_interest_mask(), inference.render_interest_mask(), cv::NORM_INF) != 0)
        return 33;
    if (loaded_graph.connections().size() != 1 ||
        loaded_dataset.items.size() != 1 ||
        loaded_dataset.items.front().sample_id !=
            "sample-input" ||
        QDir::cleanPath(QString::fromStdString(
            loaded_dataset.directory)) !=
            QDir::cleanPath(
                root.filePath("datasets/images")) ||
        loaded_task.dataset_binding.samples.front().regions.front().vRun !=
            std::vector<jocelin::geo::Run>{{1, 1, 3, 1}} ||
        loaded_task.dataset_binding.classes.front().id !=
            "foreground")
        return 7;

    QFile saved_project_file(
        root.filePath("project.json"));
    if (!saved_project_file.open(QIODevice::ReadOnly))
        return 8;
    auto invalid_project =
        QJsonDocument::fromJson(
            saved_project_file.readAll()).object();
    if (invalid_project.contains("derived_from") ||
        invalid_project.contains("derived_from_project_id") ||
        invalid_project.contains("derived_from_project_path") ||
        invalid_project.size() != 7 ||
        invalid_project.contains("active_workflow") ||
        invalid_project.contains("models") ||
        invalid_project.contains("next_model_number") ||
        invalid_project.contains("next_training_run_number") ||
        invalid_project.contains("root_directory"))
        return 34;
    auto incomplete_project = invalid_project;
    incomplete_project.remove("project_id");
    incomplete_project.remove("modified_at");
    QFile incomplete_project_file(root.filePath("incomplete-project.json"));
    if (!incomplete_project_file.open(QIODevice::WriteOnly) ||
        incomplete_project_file.write(QJsonDocument(incomplete_project).toJson()) < 0)
        return 17;
    incomplete_project_file.close();
    try {
        studio::ProjectManager incomplete;
        incomplete.open_project(root.filePath("incomplete-project.json"));
        return 18;
    } catch (const std::runtime_error&) {
    }
    invalid_project["unexpected"] = true;
    QFile invalid_project_file(
        root.filePath("invalid-project.json"));
    if (!invalid_project_file.open(QIODevice::WriteOnly) ||
        invalid_project_file.write(
            QJsonDocument(invalid_project).toJson()) < 0)
        return 9;
    invalid_project_file.close();
    try {
        studio::ProjectManager rejected;
        rejected.open_project(
            root.filePath("invalid-project.json"));
        return 10;
    } catch (const std::runtime_error&) {
    }

    loaded.set_active_index(loaded.active_index());
    loaded.set_project_name(loaded.metadata().name);
    loaded.set_project_description(
        loaded.metadata().description);
    loaded.rename_active_workflow(
        loaded.active_workflow().name);
    if (loaded.is_dirty()) return 11;
    loaded.mark_active_dirty();
    if (!loaded.is_dirty()) return 12;
    loaded.remove_workflow(1);
    if (loaded.workflows().size() != 1)
        return 13;
    std::get<studio::DatasetConfig>(
        loaded.active_workflow().document.graph
            .node(1).config).directory = "../outside";
    bool rejected_outside = false;
    try {
        loaded.save_project();
    } catch (const std::runtime_error&) {
        rejected_outside = true;
    }
    if (!rejected_outside) return 14;

    QTemporaryDir copy_root;
    if (!copy_root.isValid()) return 22;
    const QString source_root = QDir(copy_root.path()).filePath("Proj1");
    studio::ProjectManager source_project;
    source_project.new_project("Source");
    const int source_task_id = source_project.active_workflow().document.graph.add_task_node({"sg"});
    source_project.active_workflow().document.positions.emplace(source_task_id, QPointF(0, 0));
    auto source_model = model;
    source_model.asset_directory = QDir(source_root).filePath("nodes/workflow_1/1/models/train_contract").toStdString();
    source_model.weights.front().path =
        QDir(source_root).filePath("nodes/workflow_1/1/models/train_contract/best.pt").toStdString();
    studio::TaskWorkspaceService(source_project.active_workflow().document.graph, source_task_id).add_model(source_model);
    const auto removed_model = studio::TaskWorkspaceService(source_project.active_workflow().document.graph, source_task_id).add_model(source_model);
    studio::TaskWorkspaceService(source_project.active_workflow().document.graph, source_task_id).remove_model(QString::fromStdString(removed_model.id));
    if (source_project.training_runs().allocate() != 1)
        return 38;
    if (!QDir(source_root).mkpath("nodes/workflow_1/1/models/train_contract")) return 45;
    studio::ProjectStorage::write_atomic(
        QString::fromStdString(source_model.weights.front().path), "model weights");
    source_project.save_project(source_root);
    const QString source_id = source_project.metadata().project_id;
    QFile source_file(source_project.project_file());
    if (!source_file.open(QIODevice::ReadOnly)) return 23;
    const QByteArray source_manifest = source_file.readAll();
    source_file.close();
    source_project.set_project_description("copy description");
    source_project.save_project_as("Proj2", copy_root.path());
    if (source_project.metadata().name != "Proj2" || source_project.is_dirty() ||
        source_project.metadata().project_id == source_id ||
        !QFile::exists(source_project.project_file()))
        return 24;
    QFile original(QDir(source_root).filePath("project.json"));
    if (!original.open(QIODevice::ReadOnly) || original.readAll() != source_manifest)
        return 25;
    studio::ProjectManager reopened;
    reopened.open_project(source_project.project_file());
    if (reopened.metadata().project_id != source_project.metadata().project_id)
        return 26;
    const auto* copied_model = studio::TaskWorkspaceService(reopened.active_workflow().document.graph, source_task_id).find_model("model_1");
    if (!copied_model ||
        QDir::cleanPath(QString::fromStdString(copied_model->asset_directory)) !=
            QDir(reopened.root_directory()).filePath("nodes/workflow_1/1/models/train_contract") ||
        QDir::cleanPath(QString::fromStdString(copied_model->weights.front().path)) !=
            QDir(reopened.root_directory()).filePath("nodes/workflow_1/1/models/train_contract/best.pt") ||
        studio::ProjectStorage::read(QString::fromStdString(copied_model->weights.front().path)) !=
            "model weights" ||
        studio::ModelListStorage::load(QDir(reopened.node_models_directory(source_task_id)).absoluteFilePath(".."), reopened.root_directory()).next_number() != 3 ||
        studio::TaskWorkspaceService(reopened.active_workflow().document.graph, source_task_id).add_model(source_model).number != 3)
        return 39;
    if (reopened.training_runs().allocate() != 2 ||
        studio::TrainingRunStore::load(source_root).allocate() != 2)
        return 40;
    QFile copy_file(source_project.project_file());
    if (!copy_file.open(QIODevice::ReadOnly)) return 35;
    const auto copy_manifest = QJsonDocument::fromJson(copy_file.readAll()).object();
    if (copy_manifest.contains("derived_from") ||
        copy_manifest.contains("derived_from_project_id") ||
        copy_manifest.contains("derived_from_project_path") || copy_manifest.size() != 7 ||
        copy_manifest.contains("active_workflow") || copy_manifest.contains("models") ||
        copy_manifest.contains("next_model_number") ||
        copy_manifest.contains("next_training_run_number") ||
        copy_manifest.contains("root_directory"))
        return 36;
    if (studio::ProjectPathValidator::validate_save_as("Proj2", copy_root.path()) ||
        studio::ProjectPathValidator::validate_save_as("child", source_root, source_root) ||
        studio::ProjectPathValidator::validate_name("CON").isEmpty())
        return 27;

    studio::ProjectManager unsaved;
    if (unsaved.training_runs().allocate() != 1 || !unsaved.is_dirty()) return 46;
    unsaved.save_project_as("Proj3", copy_root.path());
    studio::ProjectManager reopened_unsaved;
    reopened_unsaved.open_project(unsaved.project_file());
    if (reopened_unsaved.training_runs().allocate() != 2) return 47;

    QTemporaryDir settings_root;
    QTemporaryDir recent_root;
    if (!settings_root.isValid() || !recent_root.isValid()) return 28;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(
        QSettings::IniFormat, QSettings::UserScope, settings_root.path());
    studio::RecentProjectStore recent(2);
    QStringList paths;
    for (int index = 0; index < 3; ++index) {
        const QString path = QDir(recent_root.path()).filePath(
            QString("p%1.json").arg(index));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return 29;
        file.write("{}");
        paths.append(path);
        recent.add(path);
    }
    if (recent.projects().size() != 2 || recent.projects().front() != paths[2])
        return 30;
    recent.add(paths[1]);
    if (recent.projects().front() != paths[1] ||
        recent.projects().count(paths[1]) != 1)
        return 31;
    QFile::remove(paths[2]);
    if (recent.prune().contains(paths[2]) ||
        recent.remove(paths[1]).contains(paths[1]))
        return 32;

    const QString model_list_path = root.filePath("nodes/workflow_1/2/models/model_list.json");
    const auto saved_model_list = studio::ProjectStorage::read(model_list_path);
    auto invalid_model_list = QJsonDocument::fromJson(saved_model_list).object();
    invalid_model_list.remove("next_number");
    studio::ProjectStorage::write_atomic(
        model_list_path, QJsonDocument(invalid_model_list).toJson());
    const auto previous_id = source_project.metadata().project_id;
    const auto previous_root = source_project.root_directory();
    try {
        source_project.open_project(root.path());
        return 41;
    } catch (const std::runtime_error&) {
    }
    if (source_project.metadata().project_id != previous_id ||
        source_project.root_directory() != previous_root)
        return 42;
    studio::ProjectStorage::write_atomic(model_list_path, saved_model_list);

    const QString invalid_training_output = root.filePath("invalid-training");
    if (!root.mkpath("invalid-training/runs")) return 43;
    studio::ProjectStorage::write_atomic(
        QDir(invalid_training_output).filePath("runs/training_runs.json"),
        "{\"next_run_number\":0}");
    try {
        studio::TrainingRunStore::load(invalid_training_output);
        return 44;
    } catch (const std::runtime_error&) {
    }
    // Classification model order survives storage without segmentation pixel values.
    jocelin::model::ModelList classification_models;
    auto classification_model = model;
    classification_model.task = jocelin::config::ToolType::Cls;
    classification_model.model_index = 1;
    classification_model.classes = {{"class-dog", "dog", 0}, {"class-cat", "cat", 0}};
    classification_models.add(classification_model);
    studio::ModelListStorage::save(
        studio::ModelListStorage::serialize(classification_models, root.path()), root.filePath("classification-node"));
    const auto restored_classification = studio::ModelListStorage::load(root.filePath("classification-node"), root.path());
    if (restored_classification.entries().front().classes[0].id != "class-dog" ||
        restored_classification.entries().front().classes[1].id != "class-cat" ||
        restored_classification.entries().front().classes[0].mask_value != 0) return 48;
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 99;
}
