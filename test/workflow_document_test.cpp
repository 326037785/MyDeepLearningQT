#include "modules/workflow/storage/workflow_document.h"
#include "modules/project/storage/project_storage.h"
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <stdexcept>
#include <limits>

int main() {
    studio::WorkflowGraph empty;
    const auto empty_decoded = studio::parse_workflow(
        studio::serialize_workflow(empty, {}).json);
    if (!empty_decoded.graph.nodes().empty() ||
        !empty_decoded.graph.connections().empty())
        return 1;

    studio::WorkflowGraph detection_graph;
    const int od_source = detection_graph.add_node(studio::NodeKind::DatasetInput);
    const int od_node = detection_graph.add_task_node({"od"});
    studio::DatasetConfig& od_dataset = std::get<studio::DatasetConfig>(detection_graph.node(od_source).config);
    od_dataset.items = {{"dataset/input.png", {}, true, "od-input"}};
    detection_graph.connect(od_source, 0, od_node, 0);
    studio::TaskNodeConfig& detection = std::get<studio::TaskNodeConfig>(detection_graph.node(od_node).config);
    detection.dataset_binding.classes = {{"defect", 0, "Defect", "#123456", true, false}};
    studio::TaskSampleBinding& od_sample = detection.dataset_binding.samples.front();
    od_sample.bbox_comps = {{"defect", {0, 15.5, 12.25, 13, 7}, std::nullopt}};
    od_sample.annotation_state = "annotated";
    const studio::SerializedWorkflow od_document = studio::serialize_workflow(
        detection_graph, {{od_source, {0, 0}}, {od_node, {300, 0}}});
    const studio::WorkflowDocument od_restored = studio::parse_workflow(od_document.json);
    const studio::TaskNodeConfig& od_config = std::get<studio::TaskNodeConfig>(od_restored.graph.node(od_node).config);
    if (od_config.task != "od" || od_config.model_index != 0 || od_config.model_scale != "n" ||
        od_config.training.image_width != 640 || od_config.training.image_height != 640 ||
        od_config.dataset_binding.source_node_id != od_source || od_config.dataset_binding.samples.size() != 1) {
        throw std::runtime_error("OD TaskNode did not round-trip with its Dataset binding");
    }
    const studio::TaskSampleBinding& od_restored_sample = od_config.dataset_binding.samples.front();
    if (od_restored_sample.bbox_comps.size() != 1 || od_restored_sample.annotation_state != "annotated") {
        throw std::runtime_error("OD annotation did not round-trip");
    }
    const jocelin::meta::BboxComp& od_box = od_restored_sample.bbox_comps.front();
    if (od_box.class_id != "defect" || od_box.box.theta != 0 || od_box.box.cx() != 15.5 ||
        od_box.box.cy() != 12.25 || od_box.box.w() != 13 || od_box.box.h() != 7 || od_box.score) {
        throw std::runtime_error("OD annotation lost its class or source-image coordinates");
    }

    studio::WorkflowGraph graph;
    const int source_node_id = graph.add_node(studio::NodeKind::DatasetInput);
    const int node_id = graph.add_task_node({"cls"});
    auto& dataset_config =
        std::get<studio::DatasetConfig>(graph.node(source_node_id).config);
    dataset_config.directory = "dataset";
    dataset_config.items = {
        {"dataset/input.png", {}, true, "sample-input"},
        {"dataset/broken.png", "decode failed", false, "sample-broken"}};
    graph.connect(source_node_id, 0, node_id, 0);

    auto& task = std::get<studio::TaskNodeConfig>(graph.node(node_id).config);
    task.model_index = 2;
    task.model_scale = "n";
    task.device = {
        jocelin::config::DeviceType::Gpu, 1, "cuda:1"};
    task.training.batch_size = 12;
    task.training.augment.probability = 0.73f;
    task.training.augment.enable.scale = true;
    task.training.augment.enable.crop = true;
    task.training.augment.enable.translate = true;
    task.training.augment.enable.brightness = true;
    task.training.augment.scale_ranges = {{0.75, 1.25}, {1.5, 2.0}};
    task.training.augment.crop_ratio = 0.22f;
    task.training.augment.translate_ratio = 0.18f;
    task.training.augment.brightness_delta = 0.4f;
    task.training.output_directory = "models/run";
    task.inference_batch_size = 3;
    task.inference_output_directory = "results/run";
    task.inference_use_all_images = false;
    task.inference_images = {"dataset/input.png"};
    task.dataset_binding.classes = {
        {"class-road", 1, "road", "#112233", true, false},
        {"class-car", 2, "car", "#445566", false, true}};
    auto& sample = task.dataset_binding.samples.front();
    sample.training_mask_path = "masks/exclusion.png";
    sample.class_id = "class-road";
    sample.split = "validation";
    sample.split_manual = true;
    sample.annotation_state = "annotated";
    sample.quality_issues = {"stale diagnostic"};
    task.dataset_binding.preprocessing.normalize = false;
    task.dataset_binding.split_settings = {65, 25, 10, 7, "stratified"};
    task.runtime.training_model_id = "model_1";

    studio::InferenceResultSet result_set;
    result_set.model_id = "model_1";
    result_set.weight_id = "libtorch_fp32";
    result_set.weight_format = "libtorch";
    result_set.result_directory = "results/model_1/libtorch";
    result_set.aggregate_metrics = {true, 0.8, 0.75, 0.77};
    result_set.aggregate_metrics.accuracy = 0.85;
    studio::ResultReference result;
    result.source_sample_id = "sample-input";
    jocelin::result::cls::ClsResult classification;
    classification.image_path = "dataset/input.png";
    classification.result_name = "input";
    classification.predicted_class = -1;
    classification.confidence = 0.91;
    classification.probabilities = {0.91, 0.09};
    classification.confidence_threshold = 0.95;
    classification.metadata_path = "results/model_1/libtorch/input.json";
    result.result = classification;
    result_set.samples.push_back(std::move(result));
    task.runtime.results.push_back(std::move(result_set));

    const std::map<int, QPointF> positions{
        {source_node_id, {10.5, 20.25}},
        {node_id, {330, 40}}};
    QTemporaryDir assets;
    const auto serialized = studio::serialize_workflow(graph, positions);
    const auto write_assets = [](const auto& serialized, const QString& root) {
        for (const auto& [path, data] : serialized.inference_assets) {
            const auto file = QDir(root).filePath(path);
            QDir().mkpath(QFileInfo(file).absolutePath());
            studio::ProjectStorage::write_atomic(file, data);
        }
    };
    write_assets(serialized, assets.path());
    const auto encoded = serialized.json;
    if (encoded.contains("quality_issues")) return 55;
    const auto decoded = studio::parse_workflow(encoded, assets.path());
    if (decoded.graph.nodes().size() != 2 ||
        decoded.graph.connections().size() != 1 ||
        decoded.positions.at(source_node_id) != QPointF(10.5, 20.25))
        return 2;

    const auto& decoded_dataset =
        std::get<studio::DatasetConfig>(decoded.graph.node(source_node_id).config);
    const auto& decoded_task =
        std::get<studio::TaskNodeConfig>(decoded.graph.node(node_id).config);
    const auto& decoded_sample = decoded_task.dataset_binding.samples.front();
    if (!decoded_sample.quality_issues.empty()) return 56;
    if (task.runtime.task_id.empty() || decoded_task.runtime.task_id == task.runtime.task_id) return 50;
    // Persistent Node ids survive gaps; runtime Task ids are recreated on load.
    studio::WorkflowGraph identity_graph;
    const int removed_node_id = identity_graph.add_node(studio::NodeKind::DatasetInput);
    const int original_node_id = identity_graph.add_task_node({"cls"});
    identity_graph.remove_node(removed_node_id);
    const auto task_id = std::get<studio::TaskNodeConfig>(identity_graph.node(original_node_id).config).runtime.task_id;
    const int other_node_id = identity_graph.add_task_node({"cls"});
    if (task_id == std::get<studio::TaskNodeConfig>(identity_graph.node(other_node_id).config).runtime.task_id) return 51;
    const int deleted_node_id = identity_graph.add_task_node({"cls"});
    identity_graph.remove_node(deleted_node_id);
    const auto identity_roundtrip = studio::parse_workflow(studio::serialize_workflow(
        identity_graph, {{original_node_id, {0, 0}}, {other_node_id, {100, 0}}}).json);
    if (identity_roundtrip.graph.nodes().begin()->first != original_node_id ||
        std::get<studio::TaskNodeConfig>(identity_roundtrip.graph.nodes().begin()->second.config).runtime.task_id == task_id)
        return 52;
    auto continued_graph = identity_roundtrip.graph;
    if (continued_graph.add_task_node({"cls"}) <= deleted_node_id) return 53;
    // Reordering serialized nodes must preserve persistent ids, ports, positions and bindings.
    auto reordered_document = QJsonDocument::fromJson(encoded).object();
    const auto serialized_nodes = reordered_document.value("nodes").toArray();
    reordered_document["nodes"] = QJsonArray{serialized_nodes[1], serialized_nodes[0]};
    const auto reordered = studio::parse_workflow(QJsonDocument(reordered_document).toJson(), assets.path());
    if (reordered.graph.node(node_id).node_id != node_id || reordered.graph.node(source_node_id).node_id != source_node_id ||
        reordered.graph.connections().front().from_node_id != source_node_id ||
        reordered.graph.connections().front().to_node_id != node_id ||
        reordered.positions.at(node_id) != positions.at(node_id) ||
        std::get<studio::TaskNodeConfig>(reordered.graph.node(node_id).config).dataset_binding.source_node_id != source_node_id)
        return 54;
    if (decoded_dataset.directory != "dataset" ||
        decoded_dataset.items.size() != 2 ||
        decoded_dataset.items[0].sample_id != "sample-input" ||
        decoded_dataset.items[1].valid ||
        decoded_dataset.items[1].error != "decode failed" ||
        decoded_task.task != "cls" || decoded_task.model_index != 2 ||
        decoded_task.model_scale != "n" ||
        decoded_task.device.type != jocelin::config::DeviceType::Gpu ||
        decoded_task.device.index != 1 || decoded_task.device.id != "cuda:1" ||
        decoded_task.training.batch_size != 12 ||
        std::abs(decoded_task.training.augment.probability - 0.73f) > 1e-6f ||
        !decoded_task.training.augment.enable.scale ||
        !decoded_task.training.augment.enable.crop ||
        !decoded_task.training.augment.enable.translate ||
        !decoded_task.training.augment.enable.brightness ||
        decoded_task.training.augment.scale_ranges.size() != 2 ||
        decoded_task.training.augment.scale_ranges[1].low != 1.5 ||
        std::abs(decoded_task.training.augment.crop_ratio - 0.22f) > 1e-6f ||
        std::abs(decoded_task.training.augment.translate_ratio - 0.18f) > 1e-6f ||
        std::abs(decoded_task.training.augment.brightness_delta - 0.4f) > 1e-6f ||
        decoded_task.inference_batch_size != 3 ||
        decoded_task.inference_use_all_images ||
        decoded_task.dataset_binding.classes.size() != 2 ||
        decoded_task.dataset_binding.bucket_capacity != 1 ||
        decoded_sample.image_paths != std::vector<std::string>{"dataset/input.png"} ||
        decoded_sample.split != "validation" || !decoded_sample.split_manual ||
        decoded_sample.training_mask_path != "masks/exclusion.png" ||
        decoded_task.dataset_binding.split_settings.train_percent != 65 ||
        decoded_task.dataset_binding.split_settings.seed != 7 ||
        decoded_task.dataset_binding.preprocessing.normalize ||
        decoded_task.runtime.training_model_id != "model_1" ||
        decoded_task.runtime.results.size() != 1 ||
        decoded_task.runtime.results.front().model_id != "model_1" ||
        decoded_task.runtime.results.front().samples.size() != 1 ||
        decoded_task.runtime.results.front().aggregate_metrics.accuracy != 0.85 ||
        std::get<jocelin::result::cls::ClsResult>(decoded_task.runtime.results.front().samples.front().result).confidence_threshold != 0.95 ||
        std::get<jocelin::result::cls::ClsResult>(decoded_task.runtime.results.front().samples.front().result).predicted_class != -1 ||
        std::get<jocelin::result::cls::ClsResult>(decoded_task.runtime.results.front().samples.front().result).probabilities != classification.probabilities ||
        std::get<jocelin::result::cls::ClsResult>(decoded_task.runtime.results.front().samples.front().result).interest)
        return 3;

    auto& sg_task = std::get<studio::TaskNodeConfig>(graph.node(node_id).config);
    sg_task.task = "sg";
    sg_task.model_index = 0;
    sg_task.model_scale = "s";
    sg_task.runtime.results.front().aggregate_metrics = {};
    jocelin::result::sg::SgResult segmentation;
    segmentation.image_path = "dataset/input.png";
    segmentation.image_width = 5;
    segmentation.image_height = 4;
    segmentation.min_component_area = 2;
    segmentation.aspect_ratio_range = {0.5, std::numeric_limits<double>::max()};
    segmentation.min_component_confidence = 0.8;
    segmentation.metrics = {true, 0.8, 0.75, 0.77, 0.6};
    jocelin::result::sg::SgResult::Component accepted;
    accepted.region = jocelin::geo::Region({{1,1,3,1}});
    accepted.label = 1;
    accepted.area = 2;
    accepted.bbox = {1,1,2,3};
    accepted.aspect_ratio = 2.0;
    accepted.confidence = 0.9;
    accepted.area_pass = accepted.aspect_ratio_pass = accepted.confidence_pass = accepted.interest = true;
    auto rejected = accepted;
    rejected.region = jocelin::geo::Region({{3,0,1,1}});
    rejected.area = 1;
    rejected.bbox = {3,0,4,1};
    rejected.aspect_ratio = 1.0;
    rejected.area_pass = rejected.interest = false;
    segmentation.components = {accepted, rejected};
    segmentation.present_labels = {1};
    segmentation.predicted_area = 2;
    segmentation.mean_confidence = 0.9;
    sg_task.runtime.results.front().samples.front().result = segmentation;
    QTemporaryDir sg_assets;
    const auto sg_serialized = studio::serialize_workflow(graph, positions);
    write_assets(sg_serialized, sg_assets.path());
    const auto sg_decoded = studio::parse_workflow(sg_serialized.json, sg_assets.path());
    const jocelin::config::AiConfig defaults;
    const jocelin::config::AiConfig& restored_filter =
        std::get<studio::TaskNodeConfig>(sg_decoded.graph.node(node_id).config).inference_config;
    if (restored_filter.max_region_area != defaults.max_region_area ||
        restored_filter.aspect_ratio_range.high != defaults.aspect_ratio_range.high) {
        return 57;
    }
    QJsonObject legacy = QJsonDocument::fromJson(sg_serialized.json).object();
    QJsonArray legacy_nodes = legacy.value("nodes").toArray();
    QJsonObject legacy_task = legacy_nodes.at(1).toObject();
    QJsonObject legacy_config = legacy_task.value("config").toObject();
    legacy_config.remove("sg_filter");
    legacy_task["config"] = legacy_config;
    legacy_nodes[1] = legacy_task;
    legacy["nodes"] = legacy_nodes;
    const studio::WorkflowDocument legacy_decoded =
        studio::parse_workflow(QJsonDocument(legacy).toJson(), sg_assets.path());
    const jocelin::config::AiConfig& legacy_filter =
        std::get<studio::TaskNodeConfig>(legacy_decoded.graph.node(node_id).config).inference_config;
    if (legacy_filter.confidence_threshold != defaults.confidence_threshold ||
        legacy_filter.min_component_confidence != defaults.min_component_confidence ||
        legacy_filter.min_region_area != defaults.min_region_area ||
        legacy_filter.max_region_area != defaults.max_region_area ||
        legacy_filter.aspect_ratio_range.low != defaults.aspect_ratio_range.low ||
        legacy_filter.aspect_ratio_range.high != defaults.aspect_ratio_range.high ||
        legacy_filter.morphological_refine != defaults.morphological_refine ||
        legacy_filter.morph_iterations != defaults.morph_iterations) {
        return 58;
    }
    const auto& restored = std::get<jocelin::result::sg::SgResult>(
        std::get<studio::TaskNodeConfig>(sg_decoded.graph.node(node_id).config)
            .runtime.results.front().samples.front().result);
    if (restored.components.size() != 2 || restored.components[1].interest ||
        !restored.components[1].confidence_pass || restored.components[1].area_pass ||
        restored.components[0].region.vRun != accepted.region.vRun ||
        restored.max_component_area != std::numeric_limits<int64_t>::max() ||
        restored.aspect_ratio_range.high != std::numeric_limits<double>::max() || restored.min_component_confidence != 0.8 ||
        restored.metrics.mean_iou != 0.6 ||
        cv::norm(restored.render_interest_mask(), segmentation.render_interest_mask(), cv::NORM_INF) != 0)
        return 8;

    const auto rejects = [&assets](const QJsonObject& value) {
        try {
            (void)studio::parse_workflow(QJsonDocument(value).toJson(), assets.path());
        } catch (const std::runtime_error&) {
            return true;
        }
        return false;
    };

    auto invalid_format = QJsonDocument::fromJson(encoded).object();
    invalid_format["format"] = "unknown";
    if (!rejects(invalid_format)) return 4;

    auto missing_field = QJsonDocument::fromJson(encoded).object();
    auto nodes = missing_field.value("nodes").toArray();
    auto task_node = nodes.at(1).toObject();
    auto task_config = task_node.value("config").toObject();
    task_config.remove("runtime");
    task_node["config"] = task_config;
    nodes[1] = task_node;
    missing_field["nodes"] = nodes;
    if (!rejects(missing_field)) return 5;

    auto unknown_field = QJsonDocument::fromJson(encoded).object();
    unknown_field["unexpected_field"] = true;
    if (!rejects(unknown_field)) return 6;

    auto invalid_augment = QJsonDocument::fromJson(encoded).object();
    auto invalid_nodes = invalid_augment.value("nodes").toArray();
    auto invalid_task_node = invalid_nodes.at(1).toObject();
    auto invalid_task_config = invalid_task_node.value("config").toObject();
    auto invalid_training = invalid_task_config.value("training").toObject();
    auto invalid_augmentation = invalid_training.value("augment").toObject();
    invalid_augmentation["brightness_delta"] = 1000.0;
    invalid_training["augment"] = invalid_augmentation;
    invalid_task_config["training"] = invalid_training;
    invalid_task_node["config"] = invalid_task_config;
    invalid_nodes[1] = invalid_task_node;
    invalid_augment["nodes"] = invalid_nodes;
    if (!rejects(invalid_augment)) return 7;
    return 0;
}
