#include "task_dataset_binding.h"
#include "workflow.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {

template <typename Function>
bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

} // namespace

int main() {
    studio::WorkflowGraph graph;
    if (!graph.nodes().empty() || !graph.connections().empty())
        return 1;
    if (!rejects([&] { graph.add_node(studio::NodeKind::TaskConfig); }) ||
        !rejects([&] { graph.add_task_node({}); }))
        return 13;
    studio::WorkflowGraph defaults;
    const int sg_default = defaults.add_task_node({"sg"});
    const int cls_default = defaults.add_task_node({"cls"});
    const auto& sg_config = std::get<studio::TaskNodeConfig>(defaults.node(sg_default).config);
    const auto& cls_config = std::get<studio::TaskNodeConfig>(defaults.node(cls_default).config);
    if (sg_config.task != "sg" || sg_config.model_index != 0 || sg_config.model_scale != "s" ||
        cls_config.task != "cls" || cls_config.model_index != 1 || cls_config.model_scale != "s")
        return 14;

    const int dataset =
        graph.add_node(studio::NodeKind::DatasetInput);
    const int task = graph.add_task_node({"sg"});
    graph.connect(dataset, 0, task, 0);
    if (graph.connections().size() != 1)
        return 2;
    if (std::get<studio::TaskNodeConfig>(
            graph.node(task).config)
            .dataset_binding.source_dataset_node_id != dataset)
        return 3;

    const auto temp_root =
        std::filesystem::temp_directory_path() /
        "deep_learning_workflow_validation";
    std::filesystem::create_directories(
        temp_root / "dataset");
    const auto image =
        temp_root / "dataset" / "sample.png";
    std::ofstream(image, std::ios::binary) << "test";
    auto& source = std::get<studio::DatasetConfig>(
        graph.node(dataset).config);
    source.directory =
        (temp_root / "dataset").string();
    source.items.push_back({
        image.string(), {}, true, "sample-a"});
    studio::synchronize_task_bindings_for_dataset(
        graph, dataset);
    const auto& binding =
        std::get<studio::TaskNodeConfig>(
            graph.node(task).config).dataset_binding;
    if (binding.samples.size() != 1 ||
        binding.samples.front().sample_id != "sample-a")
        return 4;

    auto& config = std::get<studio::TaskNodeConfig>(
        graph.node(task).config);
    config.model_index = 0;
    config.model_scale = "s";
    if (!graph.validate_for_execution().empty())
        return 5;

    // The concrete product invariant is one Dataset -> one Task.
    const int second_task = graph.add_task_node({"cls"});
    if (!rejects([&] {
            graph.connect(dataset, 0, second_task, 0);
        }))
        return 6;
    graph.remove_node(second_task);
    if (!rejects([&] {
            graph.connect(dataset, 0, task, 0);
        }))
        return 7;
    if (!studio::WorkflowGraph::output_ports(
            studio::NodeKind::TaskConfig).empty())
        return 8;

    const auto values = graph.execute(
        [](const studio::Node& node,
           const std::vector<studio::RuntimeValue>& inputs)
            -> studio::RuntimeValue {
            if (node.kind ==
                studio::NodeKind::DatasetInput)
                return studio::DatasetValue{"dataset"};
            if (inputs.size() != 1 ||
                !std::holds_alternative<
                    studio::DatasetValue>(inputs.front()))
                throw std::runtime_error(
                    "dataset did not reach Task input");
            return studio::TaskConfigValue{
                "sg", 0, "s", jocelin::config::cpu_device()};
        });
    if (!std::holds_alternative<
            studio::TaskConfigValue>(values.at(task)))
        return 9;
    if (graph.node(dataset).status !=
            studio::NodeStatus::Success ||
        graph.node(task).status !=
            studio::NodeStatus::Success)
        return 10;

    graph.remove_node(dataset);
    if (std::get<studio::TaskNodeConfig>(
            graph.node(task).config)
            .dataset_binding.source_dataset_node_id != 0)
        return 11;
    if (graph.validate_for_execution().empty())
        return 12;
    std::filesystem::remove_all(temp_root);
    return 0;
}
