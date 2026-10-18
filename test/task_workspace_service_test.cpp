#include "task_workspace_service.h"

#include "task_dataset_binding.h"
#include "workflow_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    QDir root(temporary.path());
    root.mkpath("images");
    root.mkpath("masks");
    root.mkpath("models");
    root.mkpath("results");

    studio::WorkflowDocument document;
    const int dataset_id =
        document.graph.add_node(studio::NodeKind::DatasetInput);
    const int task_id = document.graph.add_task_node({"sg"});
    document.positions = {
        {dataset_id, {-220, 0}},
        {task_id, {120, 0}}};
    auto& dataset =
        std::get<studio::DatasetConfig>(document.graph.node(dataset_id).config);
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
    document.graph.connect(dataset_id, 0, task_id, 0);
    auto& task =
        std::get<studio::TaskNodeConfig>(document.graph.node(task_id).config);
    task.model_index = 0;
    task.model_scale = "s";
    task.training.output_directory = root.filePath("models").toStdString();
    task.training.augment.probability = 0.8f;
    task.training.augment.enable.rotate = true;
    task.training.augment.rotation_ranges = {{-15.0, 20.0}};
    task.inference_output_directory = root.filePath("results").toStdString();

    studio::TaskWorkspaceService service(document.graph, task_id);
    service.synchronize();
    if (service.binding().samples.size() != 10) return 4;
    for (const auto& sample : service.binding().samples)
        if (sample.image_paths.size() != 2) return 5;
    const std::string road_id = service.add_class("road");
    const std::string vehicle_id = service.add_class("vehicle");
    for (int index = 0; index < 10; ++index) {
        auto& sample =
            service.binding().samples[static_cast<size_t>(index)];
        sample.mask_path =
            mask_paths[static_cast<size_t>(index)];
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
    service.retire_class(road_id);
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

    // Removing a target makes the sample unannotated. Raw images must not be
    // promoted to background supervision implicitly.
    service.binding().samples.front().mask_path.clear();
    service.refresh_quality();
    if (service.binding().samples.front().annotation_state !=
            "unannotated" ||
        !service.binding().samples.front().quality_issues.empty())
        return 12;
    for (size_t index = 1; index < service.binding().samples.size(); ++index)
        if (!service.binding().samples[index].quality_issues.empty())
            return 12;
    const auto request = service.build_training_request(jocelin::config::cpu_device());
    if (request.samples.samples.size() != 9 ||
        request.config.bucket_capacity != 2 ||
        request.config.batch_size != task.training.batch_size ||
        std::abs(request.config.augment.probability - 0.8f) > 1e-6f ||
        !request.config.augment.enable.rotate ||
        request.config.augment.rotation_ranges.front().high != 20.0 ||
        request.config.num_classes != 4 ||
        request.class_names.front() != "background")
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
    inference_model.classes = {{"a", "a", 1}, {"b", "b", 2}, {"c", "c", 3}};
    jocelin::model::ModelWeight inference_weight;
    inference_weight.id = "libtorch_fp32";
    inference_weight.path = "same-task-medium.pt";
    inference_model.weights.push_back(inference_weight);
    const auto inference_request =
        service.build_inference_request(
            jocelin::config::cpu_device(), inference_model, inference_weight);
    if (inference_request.config.model_index != 1 ||
        inference_request.config.model_scale != jocelin::config::ModelScale::M ||
        inference_request.config.image_width != 640 ||
        inference_request.config.image_height != 480 ||
        inference_request.config.num_classes != 4 ||
        inference_request.weight_path != "same-task-medium.pt" ||
        inference_request.image_paths.empty())
        return 33;

    const auto omitted = std::find_if(
        request.samples.samples.begin(),
        request.samples.samples.end(),
        [&](const jocelin::meta::AiMetaData& sample) {
            return sample.sample_id ==
                service.binding().samples.front().sample_id;
        });
    if (omitted != request.samples.samples.end())
        return 16;

    const QByteArray encoded =
        studio::serialize_workflow(document.graph, document.positions);
    const auto decoded = studio::parse_workflow(encoded);
    const auto& decoded_task = std::get<studio::TaskNodeConfig>(
        decoded.graph.node(task_id).config);
    if (decoded_task.dataset_binding.samples.size() != 10 ||
        decoded_task.dataset_binding.classes.size() != 3 ||
        decoded_task.dataset_binding.classes[1].id != road_id ||
        decoded_task.dataset_binding.classes[1].mask_value != road_mask ||
        decoded_task.dataset_binding.split_settings.seed != 271828 ||
        !decoded_task.dataset_binding.samples.front().split_manual)
        return 14;

    // A one-image dataset shares its sole train sample with an automatically
    // derived validation split rather than refusing to train.
    studio::WorkflowDocument single_document;
    const int single_dataset_id =
        single_document.graph.add_node(studio::NodeKind::DatasetInput);
    const int single_task_id = single_document.graph.add_task_node({"sg"});
    single_document.graph.connect(
        single_dataset_id, 0, single_task_id, 0);
    auto& single_dataset = std::get<studio::DatasetConfig>(
        single_document.graph.node(single_dataset_id).config);
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
        single_document.graph.node(single_task_id).config);
    single_task.model_index = 0;
    single_task.model_scale = "s";
    single_task.training.output_directory =
        root.filePath("models").toStdString();
    studio::TaskWorkspaceService single_service(
        single_document.graph, single_task_id);
    single_service.synchronize();
    // A raw Dataset connection must never claim a sibling mask implicitly.
    // Annotation paths are established only by explicit annotation import or
    // by saving inside the Task annotation workspace.
    if (!single_service.binding().samples.front().mask_path.empty() ||
        single_service.binding().samples.front().annotation_state !=
            "unannotated")
        return 32;
    single_service.add_class("foreground");
    single_service.set_sample_split("single", "train");
    single_service.refresh_quality();
    // Explicit background supervision is still trainable and receives a
    // generated all-zero target.
    single_service.binding().samples.front().annotation_state =
        "background-only";
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
        single_request.samples.samples[0].annotation_state != jocelin::meta::AiMetaData::AnnotationState::BackgroundOnly ||
        single_request.samples.samples[0].image_paths.size() != 1 ||
        !single_request.samples.samples[0].regions.empty() ||
        single_request.samples.samples[0].sample_id !=
            single_request.samples.samples[1].sample_id)
        return 21;
    return 0;
}
