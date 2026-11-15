#include "task_workspace_service.h"

#include "task_dataset_binding.h"
#include "workflow_document.h"
#include "segmentation_annotation_state.h"
#include "project_storage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <set>

int main(int argc, char** argv) try {
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    QDir root(temporary.path());
    root.mkpath("images");
    root.mkpath("masks");
    root.mkpath("models");
    root.mkpath("results");

    studio::WorkflowDocument document;
    const int source_node_id =
        document.graph.add_node(studio::NodeKind::DatasetInput);
    const int node_id = document.graph.add_task_node({"sg"});
    document.positions = {
        {source_node_id, {-220, 0}},
        {node_id, {120, 0}}};
    auto& dataset =
        std::get<studio::DatasetConfig>(document.graph.node(source_node_id).config);
    dataset.directory = temporary.path().toStdString();
    dataset.bucket_capacity = 2;

    std::vector<std::string> mask_paths;
    for (int sample = 0; sample < 10; ++sample) {
        const QString sample_id = QString("sample-%1").arg(sample);
        const QString mask_path =
            root.filePath(QString("masks/%1.png").arg(sample_id));
        QImage mask(16, 12, QImage::Format_Grayscale8);
        mask.fill(sample % 2 == 0 ? 1 : 2);
        if (!mask.save(mask_path)) return 2;
        mask_paths.push_back(mask_path.toStdString());
        for (int image_index = 0; image_index < 2; ++image_index) {
            const QString image_path = root.filePath(
                QString("images/%1-%2.png").arg(sample_id).arg(image_index));
            QImage image(16, 12, QImage::Format_RGB888);
            image.fill(image_index == 0 ? Qt::red : Qt::blue);
            if (!image.save(image_path)) return 3;
            studio::DatasetConfig::Item item;
            item.path = image_path.toStdString();
            item.sample_id = sample_id.toStdString();
            dataset.items.push_back(std::move(item));
        }
    }
    document.graph.connect(source_node_id, 0, node_id, 0);
    auto& task =
        std::get<studio::TaskNodeConfig>(document.graph.node(node_id).config);
    task.model_index = 0;
    task.model_scale = "s";
    task.training.output_directory = root.filePath("models").toStdString();
    task.training.augment.probability = 0.8f;
    task.training.augment.enable.rotate = true;
    task.training.augment.rotation_ranges = {{-15.0, 20.0}};
    task.inference_output_directory = root.filePath("results").toStdString();

    studio::TaskWorkspaceService service(document.graph, node_id);
    service.synchronize();
    if (service.binding().samples.size() != 10) return 4;
    for (const auto& sample : service.binding().samples)
        if (sample.image_paths.size() != 2) return 5;
    const std::string road_id = service.add_class("road");
    const std::string vehicle_id = service.add_class("vehicle");
    for (int index = 0; index < 10; ++index) {
        auto& sample =
            service.binding().samples[static_cast<size_t>(index)];
        studio::import_segmentation_annotation(service.binding().classes, sample,
            QString::fromStdString(mask_paths[static_cast<size_t>(index)]));
        sample.annotation_state = "annotated";
        sample.split =
            index < 7 ? "train" :
            index < 9 ? "validation" : "test";
    }
    if (service.binding().classes.size() != 2 ||
        service.binding().classes[0].id.empty() ||
        service.binding().classes[0].mask_value != 1 ||
        service.binding().classes[1].mask_value != 2) return 6;

    const int road_mask = service.binding().classes[0].mask_value;
    service.rename_class(road_id, "drivable");
    service.move_class(road_id, 1);
    const auto renamed = std::find_if(
        service.binding().classes.begin(), service.binding().classes.end(),
        [&road_id](const studio::AnnotationClass& value) {
            return value.id == road_id;
        });
    if (renamed == service.binding().classes.end() ||
        renamed->name != "drivable" || renamed->mask_value != road_mask) return 7;
    const std::string person_id = service.add_class("person");
    const auto person = std::find_if(
        service.binding().classes.begin(), service.binding().classes.end(),
        [&person_id](const studio::AnnotationClass& value) {
            return value.id == person_id;
        });
    if (person == service.binding().classes.end() ||
        person->mask_value == road_mask) return 8;

    service.assign_reproducible_split(60, 20, 20, 314159);
    std::vector<std::string> first_assignment;
    for (const auto& sample : service.binding().samples)
        first_assignment.push_back(sample.split);
    const auto first_stats = service.split_stats();
    if (first_stats.train != 6 || first_stats.validation != 2 ||
        first_stats.test != 2 || first_stats.unassigned != 0) return 9;
    std::map<std::string, std::array<int, 2>> strata_by_split;
    for (size_t index = 0; index < service.binding().samples.size(); ++index) {
        const int stratum = static_cast<int>(index % 2);
        ++strata_by_split[
            service.binding().samples[index].split][stratum];
    }
    if (strata_by_split["validation"][0] != 1 ||
        strata_by_split["validation"][1] != 1 ||
        strata_by_split["test"][0] != 1 ||
        strata_by_split["test"][1] != 1)
        return 23;
    service.assign_reproducible_split(60, 20, 20, 314159);
    for (size_t index = 0; index < service.binding().samples.size(); ++index)
        if (service.binding().samples[index].split != first_assignment[index])
            return 10;
    service.set_sample_split(service.binding().samples.front().sample_id, "validation");
    if (service.binding().samples.front().split != "validation" ||
        !service.binding().samples.front().split_manual) return 11;
    service.restore_automatic_split(
        service.binding().samples.front().sample_id);
    if (service.binding().samples.front().split_manual) return 15;
    service.set_sample_split(
        service.binding().samples.front().sample_id, "validation");
    service.assign_reproducible_split(70, 30, 0, 271828);
    if (service.binding().samples.front().split != "validation" ||
        !service.binding().samples.front().split_manual ||
        service.binding().split_settings.seed != 271828)
        return 22;

    // A stored Region can contain runs belonging to several classes.
    service.binding().samples[1].regions.front().vRun.emplace_back(0, 0, 1, road_mask);
    service.remove_class(road_id);
    if (service.binding().samples.front().annotation_state !=
            "unannotated" ||
        !service.binding().samples.front().regions.empty() ||
        !service.binding().samples.front().quality_issues.empty() ||
        service.binding().samples[1].annotation_state != "annotated" ||
        service.binding().samples[1].regions.front().vRun.size() != 12)
        return 12;
    for (size_t index = 1; index < service.binding().samples.size(); ++index)
        if (!service.binding().samples[index].quality_issues.empty())
            return 12;
    service.binding().preprocessing.normalize = false;
    const auto request = service.build_training_request(jocelin::config::cpu_device());
    if (request.samples.samples.size() != 5 ||
        request.config.bucket_capacity != 2 || request.config.normalize ||
        request.config.batch_size != task.training.batch_size ||
        std::abs(request.config.augment.probability - 0.8f) > 1e-6f ||
        !request.config.augment.enable.rotate ||
        request.config.augment.rotation_ranges.front().high != 20.0 ||
        request.config.num_classes != 4 ||
        request.class_names != std::vector<std::string>{"background", "[reserved] 1", "vehicle", "person"})
        return 13;
    const auto transfer_request = service.build_training_request(
        jocelin::config::cpu_device(),
        {jocelin::config::WeightMode::Transfer, "parent-model.pt"});
    if (transfer_request.weight_source.mode != jocelin::config::WeightMode::Transfer ||
        transfer_request.weight_source.path != "parent-model.pt")
        return 31;
    jocelin::model::ModelEntry inference_model;
    inference_model.task = jocelin::config::ToolType::Sg;
    inference_model.model_index = 1;
    inference_model.model_scale = jocelin::config::ModelScale::M;
    inference_model.image_width = 640;
    inference_model.image_height = 480;
    inference_model.classes = {{vehicle_id, "vehicle", 2}, {person_id, "person", 3}};
    jocelin::model::ModelWeight inference_weight;
    inference_weight.id = "libtorch_fp32";
    inference_weight.path = "same-task-medium.pt";
    inference_model.weights.push_back(inference_weight);
    task.inference_config.confidence_threshold = 0.625f;
    task.inference_config.min_component_confidence = 0.875;
    task.inference_config.min_region_area = 37;
    task.inference_config.max_region_area = 9007199254740993LL;
    task.inference_config.aspect_ratio_range = {0.125, 3.25};
    task.inference_config.morphological_refine = false;
    task.inference_config.morph_iterations = 7;
    const auto inference_request =
        service.build_inference_request(
            jocelin::config::cpu_device(), inference_model, inference_weight);
    if (inference_request.config.model_index != 1 ||
        inference_request.config.model_scale != jocelin::config::ModelScale::M ||
        inference_request.config.image_width != 640 ||
        inference_request.config.image_height != 480 ||
        inference_request.config.num_classes != 4 ||
        inference_request.config.confidence_threshold != 0.625f ||
        inference_request.config.min_component_confidence != 0.875 ||
        inference_request.config.min_region_area != 37 ||
        inference_request.config.max_region_area != 9007199254740993LL ||
        inference_request.config.aspect_ratio_range.low != 0.125 ||
        inference_request.config.aspect_ratio_range.high != 3.25 ||
        inference_request.config.morphological_refine ||
        inference_request.config.morph_iterations != 7 ||
        inference_request.weight_path != "same-task-medium.pt" ||
        inference_request.image_paths.empty() ||
        std::count_if(inference_request.targets.begin(), inference_request.targets.end(),
            [](const jocelin::inference::InferenceTarget& target) { return target.available; }) != 10) {
        return 33;
    }

    const auto omitted = std::find_if(
        request.samples.samples.begin(),
        request.samples.samples.end(),
        [&](const jocelin::meta::AiMetaData& sample) {
            return sample.sample_id ==
                service.binding().samples.front().sample_id;
        });
    if (omitted != request.samples.samples.end())
        return 16;

    const auto encoded = studio::serialize_workflow(document.graph, document.positions);
    for (const auto& [path, data] : encoded.region_assets) {
        const QString destination = QDir(temporary.path()).filePath(path);
        if (!QDir().mkpath(QFileInfo(destination).absolutePath())) return 72;
        studio::ProjectStorage::write_atomic(destination, data);
    }
    auto decoded = studio::parse_workflow(encoded.json, temporary.path());
    const auto& decoded_task = std::get<studio::TaskNodeConfig>(
        decoded.graph.node(node_id).config);
    if (decoded_task.dataset_binding.samples.size() != 10 ||
        decoded_task.dataset_binding.classes.size() != 2 ||
        decoded_task.dataset_binding.classes[0].id != vehicle_id ||
        decoded_task.dataset_binding.classes[0].mask_value != 2 ||
        decoded_task.dataset_binding.classes[1].id != person_id ||
        decoded_task.dataset_binding.classes[1].mask_value != 3 ||
        !decoded_task.dataset_binding.samples.front().regions.empty() ||
        decoded_task.dataset_binding.samples.front().annotation_state != "unannotated" ||
        decoded_task.dataset_binding.split_settings.seed != 271828 ||
        !decoded_task.dataset_binding.samples.front().split_manual ||
        decoded_task.inference_config.confidence_threshold != 0.625f ||
        decoded_task.inference_config.min_component_confidence != 0.875 ||
        decoded_task.inference_config.min_region_area != 37 ||
        decoded_task.inference_config.max_region_area != 9007199254740993LL ||
        decoded_task.inference_config.aspect_ratio_range.low != 0.125 ||
        decoded_task.inference_config.aspect_ratio_range.high != 3.25 ||
        decoded_task.inference_config.morphological_refine ||
        decoded_task.inference_config.morph_iterations != 7) {
        return 14;
    }
    studio::TaskWorkspaceService reopened_sg(decoded.graph, node_id);
    const auto reopened_training = reopened_sg.build_training_request(jocelin::config::cpu_device());
    if (reopened_training.class_names != request.class_names ||
        reopened_training.samples.samples.size() != request.samples.samples.size()) {
        return 73;
    }
    for (const auto& sample : reopened_training.samples.samples) {
        for (const auto& region : sample.regions) {
            for (const auto& run : region.vRun) {
                if (run.label != 2) {
                    return 74;
                }
            }
        }
    }
    const std::string replacement = reopened_sg.add_class("drivable");
    if (replacement == road_id || reopened_sg.binding().classes.back().mask_value != road_mask) {
        return 75;
    }
    QImage new_target(16, 12, QImage::Format_Grayscale8);
    new_target.fill(road_mask);
    reopened_sg.set_segmentation_mask("sample-0", new_target);
    auto prior_model = inference_model;
    prior_model.classes.push_back({road_id, "drivable", road_mask});
    const auto prior_inference = reopened_sg.build_inference_request(
        jocelin::config::cpu_device(), prior_model, inference_weight);
    if (prior_inference.targets.front().available) {
        return 76;
    }

    // A one-image dataset shares its sole train sample with an automatically
    // derived validation split rather than refusing to train.
    studio::WorkflowDocument single_document;
    const int single_source_node_id =
        single_document.graph.add_node(studio::NodeKind::DatasetInput);
    const int single_node_id = single_document.graph.add_task_node({"sg"});
    single_document.graph.connect(
        single_source_node_id, 0, single_node_id, 0);
    auto& single_dataset = std::get<studio::DatasetConfig>(
        single_document.graph.node(single_source_node_id).config);
    const QString single_image_path =
        root.filePath("images/single-background.png");
    const QString unrelated_source_mask =
        root.filePath("masks/single-background.png");
    QImage single_image(16, 12, QImage::Format_RGB888);
    single_image.fill(Qt::black);
    QImage source_mask(16, 12, QImage::Format_Grayscale8);
    source_mask.fill(1);
    if (!single_image.save(single_image_path) ||
        !source_mask.save(unrelated_source_mask)) return 19;
    single_dataset.directory = temporary.path().toStdString();
    single_dataset.items.push_back({
        single_image_path.toStdString(), {}, true, "single"});
    auto& single_task = std::get<studio::TaskNodeConfig>(
        single_document.graph.node(single_node_id).config);
    single_task.model_index = 0;
    single_task.model_scale = "s";
    single_task.training.output_directory =
        root.filePath("models").toStdString();
    studio::TaskWorkspaceService single_service(
        single_document.graph, single_node_id);
    single_service.synchronize();
    // A raw Dataset connection must never claim a sibling mask implicitly.
    // Annotation paths are established only by explicit annotation import or
    // by saving inside the Task annotation workspace.
    if (!single_service.binding().samples.front().regions.empty() ||
        single_service.binding().samples.front().annotation_state !=
            "unannotated")
        return 32;
    single_service.add_class("foreground");
    single_service.set_sample_split("single", "train");
    single_service.refresh_quality();
    // Explicit background reaches Core as an empty region target without a spatial file.
    single_service.set_segmentation_sample_empty("single");
    const auto single_stats = single_service.split_stats();
    if (single_stats.train != 1 ||
        single_stats.validation != 0 ||
        single_stats.derived_validation != 1 ||
        single_stats.shared_validation != 1)
        return 20;
    const auto single_request =
        single_service.build_training_request(jocelin::config::cpu_device());
    if (single_request.samples.samples.size() != 2 ||
        single_request.samples.samples[0].split != jocelin::meta::AiMetaData::Split::Train ||
        single_request.samples.samples[1].split != jocelin::meta::AiMetaData::Split::Validation ||
        single_request.samples.samples[0].annotation_state != jocelin::meta::AiMetaData::AnnotationState::Annotated ||
        single_request.samples.samples[0].image_paths.size() != 1 ||
        !single_request.samples.samples[0].regions.empty() ||
        single_request.samples.samples[0].sample_id !=
            single_request.samples.samples[1].sample_id)
        return 21;
    // Ordinary empty editor saves do not confirm pure background.
    const QString empty_mask = root.filePath("masks/empty.png");
    QImage empty_target(16, 12, QImage::Format_Grayscale8);
    empty_target.fill(0);
    if (!empty_target.save(empty_mask)) return 34;
    single_service.set_segmentation_mask("single", QImage(empty_mask));
    single_service.refresh_quality();
    if (single_service.binding().samples.front().annotation_state != "unannotated") return 35;
    single_service.set_segmentation_sample_empty("single");
    single_service.synchronize();
    if (single_service.binding().samples.front().annotation_state != "annotated" ||
        !single_service.binding().samples.front().regions.empty()) return 37;
    single_document.positions = {
        {single_source_node_id, {-220, 0}}, {single_node_id, {120, 0}}};
    const auto restored_empty = studio::parse_workflow(
        studio::serialize_workflow(single_document.graph, single_document.positions).json);
    const auto& restored_empty_task = std::get<studio::TaskNodeConfig>(
        restored_empty.graph.node(single_node_id).config);
    if (restored_empty_task.dataset_binding.samples.front().annotation_state != "annotated" ||
        !restored_empty_task.dataset_binding.samples.front().regions.empty()) return 38;
    const auto empty_request = single_service.build_training_request(jocelin::config::cpu_device());
    if (!empty_request.samples.samples.front().regions.empty() ||
        QImage(empty_mask) != empty_target) return 39;

    // Read-only storage still supplies a valid foreground target.
    const QString validity_mask = root.filePath("masks/validity.png");
    if (!source_mask.save(validity_mask)) return 50;
    const auto original_permissions = QFile::permissions(validity_mask);
    if (!QFile::setPermissions(validity_mask, original_permissions &
            ~(QFileDevice::WriteOwner | QFileDevice::WriteUser |
              QFileDevice::WriteGroup | QFileDevice::WriteOther))) return 51;
    const bool read_only = !QFileInfo(validity_mask).isWritable();
    single_service.set_segmentation_mask("single", QImage(validity_mask));
    single_service.refresh_quality();
    const auto read_only_sample = single_service.binding().samples.front();
    if (!QFile::setPermissions(validity_mask, original_permissions)) return 52;
    if (!read_only || read_only_sample.annotation_state != "annotated" ||
        !read_only_sample.quality_issues.empty()) return 53;

    // Unrelated diagnostics do not decide annotation validity.
    auto diagnosed_sample = read_only_sample;
    diagnosed_sample.quality_issues = {"source sample is missing from the Dataset"};
    studio::commit_segmentation_mask(single_service.binding().classes, diagnosed_sample, QImage(validity_mask));
    if (diagnosed_sample.annotation_state != "annotated" || diagnosed_sample.quality_issues.empty()) return 54;
    studio::import_segmentation_annotation(single_service.binding().classes, diagnosed_sample, validity_mask);
    if (diagnosed_sample.annotation_state != "annotated") return 55;

    // The imported target is independent of its source mask file.
    QFile::remove(validity_mask);
    single_service.refresh_quality();
    auto& canonical_target = single_service.binding().samples.front();
    if (canonical_target.annotation_state != "annotated") return 56;
    const auto valid_regions = canonical_target.regions;
    canonical_target.regions.front().vRun.front().end = 99;
    single_service.refresh_quality();
    if (canonical_target.annotation_state != "unannotated") return 57;
    canonical_target.regions = valid_regions;
    single_service.refresh_quality();
    if (canonical_target.annotation_state != "annotated") return 58;
    canonical_target.regions.front().vRun.front().label = 99;
    single_service.refresh_quality();
    if (canonical_target.annotation_state != "unannotated") return 59;
    canonical_target.regions = valid_regions;
    single_service.refresh_quality();
    if (canonical_target.annotation_state != "annotated") return 60;

    studio::WorkflowDocument cls_document;
    const int second_source_node_id = cls_document.graph.add_node(studio::NodeKind::DatasetInput);
    const int second_node_id = cls_document.graph.add_task_node({"cls"});
    auto& cls_dataset = std::get<studio::DatasetConfig>(cls_document.graph.node(second_source_node_id).config);
    cls_dataset.items.push_back({single_image_path.toStdString(), {}, true, "cls-sample"});
    cls_document.graph.connect(second_source_node_id, 0, second_node_id, 0);
    auto& cls_task = std::get<studio::TaskNodeConfig>(cls_document.graph.node(second_node_id).config);
    cls_task.model_index = 1;
    cls_task.model_scale = "s";
    cls_task.training.output_directory = root.filePath("models").toStdString();
    cls_task.inference_output_directory = root.filePath("results").toStdString();
    studio::TaskWorkspaceService cls_service(cls_document.graph, second_node_id);
    cls_service.synchronize();
    // Semantic identities are deliberately ordered differently from both UI order and mask values.
    cls_service.add_class("temporary");
    if (cls_service.binding().classes.front().mask_value != 0) return 40;
    const std::string cat = "class-cat", dog = "class-dog";
    cls_service.binding().classes = {
        {dog, 93, "dog", "#112233", true, false},
        {cat, 0, "cat", "#445566", true, false}};
    cls_service.set_sample_split("cls-sample", "train");
    if (cls_service.binding().samples.front().annotation_state != "unannotated") return 41;
    auto cls_model = inference_model;
    cls_model.task = jocelin::config::ToolType::Cls;
    cls_model.model_index = 1;
    cls_model.model_scale = jocelin::config::ModelScale::S;
    // Existing model output order is authoritative even when it differs from the current Task.
    cls_model.classes = {{dog, "dog", 0}, {cat, "cat", 0}};
    for (const auto& id : {cat, dog}) {
        cls_service.set_class_for_sample("cls-sample", id);
        const auto training = cls_service.build_training_request(jocelin::config::cpu_device());
        const int label = id == cat ? 1 : 2;
        if (training.config.num_classes != 2 ||
            training.class_names != std::vector<std::string>{"cat", "dog"} ||
            training.samples.samples.front().class_label != label ||
            training.samples.samples.front().annotation_state != jocelin::meta::AiMetaData::AnnotationState::Annotated)
            return 42;
        const auto inference = cls_service.build_inference_request(
            jocelin::config::cpu_device(), cls_model, inference_weight);
        if (!inference.targets.front().available ||
            inference.targets.front().class_label != (id == dog ? 1 : 2) ||
            inference.config.num_classes != 2) return 43;
        const auto resumed = cls_service.build_training_request(jocelin::config::cpu_device(),
            {jocelin::config::WeightMode::Resume, inference_weight.path}, &cls_model);
        if (resumed.class_names != std::vector<std::string>{"dog", "cat"} ||
            resumed.samples.samples.front().class_label != (id == dog ? 1 : 2)) return 48;
    }
    cls_service.move_class(cat, -1);
    cls_service.rename_class(dog, "canine");
    cls_service.binding().classes.back().mask_value = 244;
    const auto reordered = cls_service.build_training_request(jocelin::config::cpu_device());
    if (reordered.samples.samples.front().class_label != 2 ||
        reordered.class_names != std::vector<std::string>{"cat", "canine"}) return 44;
    cls_document.positions = {{second_source_node_id, {0, 0}}, {second_node_id, {100, 0}}};
    auto reopened_cls = studio::parse_workflow(studio::serialize_workflow(cls_document.graph, cls_document.positions).json);
    studio::TaskWorkspaceService reopened_service(reopened_cls.graph, second_node_id);
    if (reopened_service.build_training_request(jocelin::config::cpu_device()).samples.samples.front().class_label != 2)
        return 45;
    cls_service.binding().samples.front().class_id = "missing-class";
    cls_service.refresh_quality();
    if (cls_service.binding().samples.front().annotation_state != "unannotated") return 46;
    cls_service.set_class_for_sample("cls-sample", dog);
    cls_service.remove_class(dog);
    if (cls_service.binding().samples.front().annotation_state != "unannotated" ||
        !cls_service.binding().samples.front().class_id.empty()) return 47;
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 90;
}
