#include "project_manager.h"
#include "project_path_validator.h"
#include "recent_project_store.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include <stdexcept>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    QDir root(temporary.path());
    root.mkpath("datasets/images");
    root.mkpath("annotations");
    root.mkpath("models");
    root.mkpath("results");
    for (const auto& relative : {
             "datasets/images/input.png",
             "annotations/input.png",
             "models/checkpoint.pt",
             "results/input_mask.png",
             "results/input_color.png",
             "results/input_meta.json"}) {
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
    const int dataset =
        workflow.document.graph.add_node(
            studio::NodeKind::DatasetInput);
    const int task_id = workflow.document.graph.add_task_node({"sg"});
    auto& dataset_config =
        std::get<studio::DatasetConfig>(
            workflow.document.graph.node(dataset).config);
    dataset_config.directory =
        root.filePath("datasets/images").toStdString();
    dataset_config.items = {{
        root.filePath("datasets/images/input.png").toStdString(),
        {}, true, "sample-input"}};
    workflow.document.graph.connect(
        dataset, 0, task_id, 0);
    auto& task =
        std::get<studio::TaskNodeConfig>(
            workflow.document.graph.node(task_id).config);
    task.model_index = 0;
    task.model_scale = "s";
    task.training.output_directory =
        root.filePath("models").toStdString();
    task.inference_output_directory =
        root.filePath("results").toStdString();
    task.dataset_binding.classes = {{
        "foreground", 1, "foreground", "#1F8A9E",
        true, false, true}};
    task.dataset_binding.samples.front().mask_path =
        root.filePath("annotations/input.png").toStdString();
    task.dataset_binding.samples.front().split = "train";
    task.dataset_binding.samples.front().annotation_state =
        "annotated";
    workflow.document.positions = {
        {dataset, {-260, -40}},
        {task_id, {80, -40}}};

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
    model.asset_directory = root.filePath("models/train_contract").toStdString();
    jocelin::model::ModelWeight training_weight;
    training_weight.id = "libtorch_fp32";
    training_weight.format = jocelin::config::WeightType::LibTorch;
    training_weight.precision = jocelin::model::WeightPrecision::Float32;
    training_weight.role = jocelin::model::WeightRole::Training;
    training_weight.path = root.filePath("models/train_contract/best.pt").toStdString();
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
    const auto added_model = project.add_model(model);
    if (added_model.id != "model_1" ||
        added_model.name != "Model 1" ||
        added_model.number != 1)
        return 15;
    if (project.allocate_training_run_number() != 1 ||
        project.allocate_training_run_number() != 2)
        return 19;
    jocelin::result::sg::SgResult inference;
    inference.image_path = root.filePath("datasets/images/input.png").toStdString();
    inference.result_name = "input";
    inference.save_dir = root.filePath("results").toStdString();
    inference.mask_path = root.filePath("results/input_mask.png").toStdString();
    inference.visualization_path = root.filePath("results/input_color.png").toStdString();
    inference.metadata_path = root.filePath("results/input_meta.json").toStdString();
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
    reference.result_id = "result-input";
    reference.source_sample_id = "sample-input";
    reference.result = inference;
    studio::InferenceResultSet result_set;
    result_set.model_id = added_model.id;
    result_set.weight_id = "libtorch_fp32";
    result_set.result_directory = inference.save_dir;
    result_set.samples.push_back(std::move(reference));
    std::get<studio::TaskNodeConfig>(project.active_workflow().document.graph.node(task_id).config)
        .runtime.results.push_back(std::move(result_set));
    project.save_project(temporary.path());
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
    if (!serialized.contains("project:datasets/images") ||
        !serialized.contains(
            "project:datasets/images/input.png") ||
        !serialized.contains(
            "project:annotations/input.png"))
        return 5;

    studio::ProjectManager loaded;
    loaded.open_project(root.filePath("project.json"));
    if (loaded.metadata().name != "Inspection Project" ||
        loaded.workflows().size() != 2 ||
        loaded.models().size() != 1 ||
        loaded.active_index() != 0 ||
        loaded.is_dirty())
        return 6;
    const auto* loaded_model = loaded.find_model("model_1");
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
            QDir::cleanPath(root.filePath("models/train_contract/best.pt")))
        return 16;
    if (loaded.allocate_training_run_number() != 3) return 20;
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
        QDir::cleanPath(QString::fromStdString(restored_result.save_dir)) != root.filePath("results") ||
        QDir::cleanPath(QString::fromStdString(restored_result.mask_path)) != root.filePath("results/input_mask.png") ||
        QDir::cleanPath(QString::fromStdString(restored_result.visualization_path)) != root.filePath("results/input_color.png") ||
        QDir::cleanPath(QString::fromStdString(restored_result.metadata_path)) != root.filePath("results/input_meta.json") ||
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
        QDir::cleanPath(QString::fromStdString(
            loaded_task.dataset_binding.samples.front()
                .mask_path)) !=
            QDir::cleanPath(
                root.filePath("annotations/input.png")) ||
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
    auto incomplete_project = invalid_project;
    incomplete_project.remove("project_id");
    incomplete_project.remove("modified_at");
    incomplete_project.remove("derived_from");
    incomplete_project.remove("models");
    incomplete_project.remove("next_model_number");
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
    const QString source_root = QDir(copy_root.path()).filePath("source");
    studio::ProjectManager source_project;
    source_project.new_project("Source");
    source_project.save_project(source_root);
    const QString source_id = source_project.metadata().project_id;
    QFile source_file(source_project.project_file());
    if (!source_file.open(QIODevice::ReadOnly)) return 23;
    const QByteArray source_manifest = source_file.readAll();
    source_file.close();
    source_project.set_project_description("copy description");
    source_project.save_project_as("Copy", copy_root.path());
    if (source_project.metadata().name != "Copy" || source_project.is_dirty() ||
        source_project.metadata().project_id == source_id ||
        source_project.metadata().derived_from_project_id != source_id ||
        !source_project.metadata().derived_from_project_path.endsWith("source/project.json") ||
        !QFile::exists(source_project.project_file()))
        return 24;
    QFile original(QDir(source_root).filePath("project.json"));
    if (!original.open(QIODevice::ReadOnly) || original.readAll() != source_manifest)
        return 25;
    studio::ProjectManager reopened;
    reopened.open_project(source_project.project_file());
    if (reopened.metadata().project_id != source_project.metadata().project_id)
        return 26;
    if (studio::ProjectPathValidator::validate_save_as("Copy", copy_root.path()) ||
        studio::ProjectPathValidator::validate_save_as("child", source_root, source_root) ||
        studio::ProjectPathValidator::validate_name("CON").isEmpty())
        return 27;

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
    return 0;
}
