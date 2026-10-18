#include "workflow_document.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <stdexcept>
#include <limits>

int main() {
    studio::WorkflowGraph empty;
    const auto empty_decoded = studio::parse_workflow(
        studio::serialize_workflow(empty, {}));
    if (!empty_decoded.graph.nodes().empty() ||
        !empty_decoded.graph.connections().empty())
        return 1;

    studio::WorkflowGraph graph;
    const int dataset = graph.add_node(studio::NodeKind::DatasetInput);
    const int task_id = graph.add_task_node({"cls"});
    auto& dataset_config =
        std::get<studio::DatasetConfig>(graph.node(dataset).config);
    dataset_config.directory = "dataset";
    dataset_config.items = {
        {"dataset/input.png", {}, true, "sample-input"},
        {"dataset/broken.png", "decode failed", false, "sample-broken"}};
    graph.connect(dataset, 0, task_id, 0);

    auto& task = std::get<studio::TaskNodeConfig>(graph.node(task_id).config);
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
        {"class-road", 1, "road", "#112233", true, false, true},
        {"class-car", 2, "car", "#445566", false, true, true}};
    auto& sample = task.dataset_binding.samples.front();
    sample.mask_path = "annotations/input.png";
    sample.training_mask_path = "masks/exclusion.png";
    sample.class_id = "class-road";
    sample.split = "validation";
    sample.split_manual = true;
    sample.annotation_state = "annotated";
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
    result.result_id = "result-input";
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
        {dataset, {10.5, 20.25}},
        {task_id, {330, 40}}};
    const auto encoded = studio::serialize_workflow(graph, positions);
    const auto decoded = studio::parse_workflow(encoded);
    if (decoded.graph.nodes().size() != 2 ||
        decoded.graph.connections().size() != 1 ||
        decoded.positions.at(dataset) != QPointF(10.5, 20.25))
        return 2;

    const auto& decoded_dataset =
        std::get<studio::DatasetConfig>(decoded.graph.node(dataset).config);
    const auto& decoded_task =
        std::get<studio::TaskNodeConfig>(decoded.graph.node(task_id).config);
    const auto& decoded_sample = decoded_task.dataset_binding.samples.front();
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

    auto& sg_task = std::get<studio::TaskNodeConfig>(graph.node(task_id).config);
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
    const auto sg_encoded = studio::serialize_workflow(graph, positions);
    const auto sg_decoded = studio::parse_workflow(sg_encoded);
    const auto& restored = std::get<jocelin::result::sg::SgResult>(
        std::get<studio::TaskNodeConfig>(sg_decoded.graph.node(task_id).config)
            .runtime.results.front().samples.front().result);
    if (restored.components.size() != 2 || restored.components[1].interest ||
        !restored.components[1].confidence_pass || restored.components[1].area_pass ||
        restored.components[0].region.vRun != accepted.region.vRun ||
        restored.max_component_area != std::numeric_limits<int64_t>::max() ||
        restored.aspect_ratio_range.high != std::numeric_limits<double>::max() || restored.min_component_confidence != 0.8 ||
        restored.metrics.mean_iou != 0.6 ||
        cv::norm(restored.render_interest_mask(), segmentation.render_interest_mask(), cv::NORM_INF) != 0)
        return 8;

    const auto rejects = [](const QJsonObject& value) {
        try {
            (void)studio::parse_workflow(QJsonDocument(value).toJson());
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
