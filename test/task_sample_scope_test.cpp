#include "modules/task/application/task_workspace_service.h"
#include "modules/task/application/task_dataset_binding.h"
#include "modules/task/ui/pages/inference_workspace.h"

#include <QApplication>
#include <QComboBox>
#include <QImage>
#include <QListWidget>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void check_task(const std::string& kind, const QString& path, const QString& output) {
    studio::WorkflowGraph graph;
    const int source = graph.add_node(studio::NodeKind::DatasetInput);
    const int node = graph.add_task_node({kind});
    studio::DatasetConfig& dataset = std::get<studio::DatasetConfig>(graph.node(source).config);
    // Separate paths avoid the filename aliases accepted by inference scope selection.
    for (const std::string& split : {"train", "validation", "test", "unassigned"}) {
        dataset.items.push_back({(path + QString::fromStdString(split) + ".png").toStdString(), {}, true, split});
    }
    graph.connect(source, 0, node, 0);
    studio::TaskWorkspaceService service(graph, node);
    service.task().training.output_directory = output.toStdString();
    service.task().inference_output_directory = output.toStdString();
    const std::string class_id = service.add_class("object");
    for (const studio::TaskSampleBinding& sample : service.binding().samples) {
        service.set_sample_split(sample.sample_id, sample.sample_id);
        if (kind == "cls") {
            service.set_class_for_sample(sample.sample_id, class_id);
        } else if (kind == "sg") {
            QImage mask(32, 24, QImage::Format_Grayscale8);
            mask.fill(studio::find_annotation_class(service.binding(), class_id)->mask_value);
            service.set_segmentation_mask(sample.sample_id, mask);
        } else {
            service.set_detection_boxes(sample.sample_id, {{class_id, {0, 12, 10, 8, 6}, std::nullopt}});
        }
    }
    service.refresh_quality();
    const jocelin::training::TrainingRequest training = service.build_training_request(jocelin::config::cpu_device());
    require(training.samples.samples.size() == 2 &&
        std::all_of(training.samples.begin(), training.samples.end(), [](const auto& sample) {
            return sample.sample_id == "train" || sample.sample_id == "validation";
        }), "test or unassigned data leaked into a Core training request");

    // Prediction is allowed for every split, including a sample with no supervision.
    service.binding().samples.back().annotation_state = "unannotated";
    service.binding().samples.back().class_id.clear();
    jocelin::model::ModelEntry model;
    model.task = jocelin::config::tool_type_from_string(kind);
    model.model_index = service.task().model_index;
    model.model_scale = jocelin::config::model_scale_from_string(service.task().model_scale);
    model.image_width = 320;
    model.image_height = 320;
    model.classes = service.training_classes();
    jocelin::model::ModelWeight weight;
    weight.id = "test-weight";
    weight.path = "fixture.pt";
    model.weights.push_back(weight);
    const jocelin::inference::InferenceRequest inference =
        service.build_inference_request(jocelin::config::cpu_device(), model, weight);
    require(inference.image_paths.size() == 4,
        "test or default-test data became unavailable for inference");

    studio::InferenceWorkspace workspace;
    workspace.set_service(&service);
    QComboBox* filter = workspace.findChild<QComboBox*>("inferenceSampleSplitFilter");
    QListWidget* images = workspace.findChild<QListWidget*>("inferenceImageList");
    require(filter && images, "inference scope controls are unavailable");
    filter->setCurrentIndex(filter->findData("test"));
    int matching = 0;
    for (int row = 0; row < images->count(); ++row) {
        if (!images->item(row)->isHidden()) {
            ++matching;
        }
    }
    require(matching == 2, "the Test filter omitted default-test samples");

    service.set_sample_split("validation", "unassigned");
    const jocelin::training::TrainingRequest derived = service.build_training_request(jocelin::config::cpu_device());
    require(std::all_of(derived.samples.begin(), derived.samples.end(), [](const auto& sample) {
        return sample.sample_id == "train";
    }), "derived validation used test or unassigned data");
}

} // namespace

int main(int argc, char** argv) try {
    QApplication application(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "cannot create scope fixture");
    QImage image(32, 24, QImage::Format_RGB888);
    image.fill(Qt::white);
    for (const QString& split : {"train", "validation", "test", "unassigned"}) {
        require(image.save(temporary.filePath(split + ".png")), "cannot save scope fixture");
    }
    for (const std::string& task : {"cls", "sg", "od"}) {
        check_task(task, temporary.path() + "/", temporary.path());
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
