#include "modules/task/application/task_dataset_binding.h"
#include "modules/workflow/model/workflow.h"

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
    // Rebind compares bucket identity only, independently of file availability.
    studio::TaskDatasetBinding saved;
    saved.bucket_capacity = 2;
    saved.samples = {{"a", {"missing/a.png", "missing/b.png"}},
                     {"b", {"missing/c.png", "missing/d.png"}}};
    studio::DatasetConfig candidate;
    candidate.bucket_capacity = 2;
    candidate.items = {{"missing/c.png", {}, true, "b"}, {"missing/d.png", {}, true, "b"},
                       {"missing/./a.png", {}, true, "a"}, {"missing/b.png", {}, true, "a"}};
    studio::validate_task_dataset_rebind(saved, candidate);
    std::swap(candidate.items[0], candidate.items[1]);
    if (!rejects([&] { studio::validate_task_dataset_rebind(saved, candidate); })) return 15;
    studio::TaskDatasetBinding empty_verified;
    empty_verified.bucket_capacity = 1;
    studio::DatasetConfig empty_candidate;
    empty_candidate.bucket_capacity = 2;
    if (!rejects([&] { studio::validate_task_dataset_rebind(empty_verified, empty_candidate); })) return 16;
    studio::validate_task_dataset_rebind(studio::TaskDatasetBinding{}, empty_candidate);
    studio::WorkflowGraph graph;
    if (!graph.nodes().empty() || !graph.connections().empty())
        return 1;
    if (!rejects([&] { graph.add_node(studio::NodeKind::TaskConfig); }) ||
        !rejects([&] { graph.add_task_node({}); }))
        return 13;
    studio::WorkflowGraph defaults;
    const int first_node_id = defaults.add_task_node({"sg"});
    const int second_node_id = defaults.add_task_node({"cls"});
    const auto& sg_config = std::get<studio::TaskNodeConfig>(defaults.node(first_node_id).config);
    const auto& cls_config = std::get<studio::TaskNodeConfig>(defaults.node(second_node_id).config);
    if (sg_config.task != "sg" || sg_config.model_index != 0 || sg_config.model_scale != "s" ||
        cls_config.task != "cls" || cls_config.model_index != 1 || cls_config.model_scale != "s")
        return 14;

    const int source_node_id =
        graph.add_node(studio::NodeKind::DatasetInput);
    const int node_id = graph.add_task_node({"sg"});
    graph.connect(source_node_id, 0, node_id, 0);
    if (graph.connections().size() != 1)
        return 2;
    if (std::get<studio::TaskNodeConfig>(
            graph.node(node_id).config)
            .dataset_binding.source_node_id != source_node_id)
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
        graph.node(source_node_id).config);
    source.directory =
        (temp_root / "dataset").string();
    source.items.push_back({
        image.string(), {}, true, "sample-a"});
    studio::synchronize_task_bindings_for_dataset(
        graph, source_node_id);
    const auto& binding =
        std::get<studio::TaskNodeConfig>(
            graph.node(node_id).config).dataset_binding;
    if (binding.samples.size() != 1 ||
        binding.samples.front().sample_id != "sample-a")
        return 4;

    auto& config = std::get<studio::TaskNodeConfig>(
        graph.node(node_id).config);
    config.model_index = 0;
    config.model_scale = "s";
    if (!graph.validate_for_execution().empty())
        return 5;

    // Each Task owns its binding while sharing the same raw Dataset.
    const int branch_node_id = graph.add_task_node({"cls"});
    graph.connect(source_node_id, 0, branch_node_id, 0);
    if (std::get<studio::TaskNodeConfig>(graph.node(branch_node_id).config).dataset_binding.samples.front().sample_id != "sample-a")
        return 6;
    graph.remove_node(branch_node_id);
    if (!rejects([&] {
            graph.connect(source_node_id, 0, node_id, 0);
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
            studio::TaskConfigValue>(values.at(node_id)))
        return 9;
    if (graph.node(source_node_id).status !=
            studio::NodeStatus::Success ||
        graph.node(node_id).status !=
            studio::NodeStatus::Success)
        return 10;

    graph.remove_node(source_node_id);
    if (std::get<studio::TaskNodeConfig>(
            graph.node(node_id).config)
            .dataset_binding.source_node_id != 0)
        return 11;
    if (graph.validate_for_execution().empty())
        return 12;
    std::filesystem::remove_all(temp_root);
    return 0;
}
