#include "workflow.h"
#include "task_module_catalog.h"
#include "model_spec_catalog.h"
#include "task_dataset_binding.h"

#include <algorithm>
#include <filesystem>
#include <queue>
#include <stdexcept>

namespace studio {

namespace {

TaskNodeConfig task_config_for(const std::string& task) {
    const auto* module = find_task_module(task);
    if (!module) throw std::invalid_argument("task module is not registered");
    if (!module->available)
        throw std::invalid_argument("task module is unavailable: " +
                                    std::string(module->unavailable_reason));
    const auto* model = first_model_spec(task);
    if (!model)
        throw std::invalid_argument("task module has no supported model configuration");
    TaskNodeConfig config;
    config.task = task;
    config.model_index = model->index;
    config.model_scale = model->scale;
    return config;
}

const std::vector<PortSpec> no_ports;
const std::vector<PortSpec> dataset_outputs{{"dataset", DataType::Dataset, PortDirection::Output, false, 0}};
const std::vector<PortSpec> task_inputs{{"dataset", DataType::Dataset, PortDirection::Input, true, 1}};

bool supported_scale(const std::string& scale) {
    return scale == "n" || scale == "s" || scale == "m" || scale == "l" || scale == "x";
}

} // namespace

int WorkflowGraph::add_node(NodeKind kind) {
    if (kind == NodeKind::TaskConfig)
        throw std::invalid_argument("Task nodes require an explicit task module");
    const int id = next_id_++;
    nodes_.emplace(id, Node{id, kind, NodeStatus::Idle, DatasetConfig{}});
    return id;
}

int WorkflowGraph::add_task_node(const CreateTaskNodeRequest& request) {
    if (request.task.empty())
        throw std::invalid_argument("Task nodes require an explicit task module");
    const int id = next_id_++;
    nodes_.emplace(
        id, Node{id, NodeKind::TaskConfig, NodeStatus::Idle,
                 task_config_for(request.task)});
    return id;
}

void WorkflowGraph::remove_node(int id) {
    if (nodes_.erase(id) == 0) throw std::runtime_error("node does not exist");
    for (auto& [unused_id, remaining] : nodes_) {
        if (remaining.kind != NodeKind::TaskConfig) continue;
        auto& binding = std::get<TaskNodeConfig>(remaining.config).dataset_binding;
        if (binding.source_dataset_node_id == id) {
            binding.source_dataset_node_id = 0;
        }
    }
    connections_.erase(std::remove_if(connections_.begin(), connections_.end(),
        [id](const Connection& edge) { return edge.from_node == id || edge.to_node == id; }), connections_.end());
}

void WorkflowGraph::connect(int from_node, size_t from_port, int to_node, size_t to_port) {
    if (from_node == to_node) throw std::runtime_error("self-connections are not allowed");
    const auto source = nodes_.find(from_node);
    const auto target = nodes_.find(to_node);
    if (source == nodes_.end() || target == nodes_.end()) throw std::runtime_error("connection references a missing node");
    const auto& outputs = output_ports(source->second.kind);
    const auto& inputs = input_ports(target->second.kind);
    if (from_port >= outputs.size()) throw std::runtime_error("connection must start at an output port");
    if (to_port >= inputs.size()) throw std::runtime_error("connection must end at an input port");
    if (outputs[from_port].data_type != inputs[to_port].data_type)
        throw std::runtime_error("port data types are incompatible");
    if (source->second.kind == NodeKind::DatasetInput &&
        target->second.kind == NodeKind::TaskConfig) {
        const bool already_bound_to_task = std::any_of(
            connections_.begin(), connections_.end(),
            [from_node, to_node](const Connection& edge) {
                return edge.from_node == from_node && edge.to_node != to_node;
            });
        if (already_bound_to_task) {
            bool other_task = false;
            for (const auto& edge : connections_) {
                if (edge.from_node != from_node || edge.to_node == to_node) continue;
                const auto found = nodes_.find(edge.to_node);
                if (found != nodes_.end() && found->second.kind == NodeKind::TaskConfig) {
                    other_task = true;
                    break;
                }
            }
            if (other_task)
                throw std::runtime_error(
                    "this release supports one Task binding per Dataset");
        }
    }
    const size_t input_count = static_cast<size_t>(std::count_if(connections_.begin(), connections_.end(),
        [to_node, to_port](const Connection& edge) { return edge.to_node == to_node && edge.to_port == to_port; }));
    if (input_count >= inputs[to_port].max_connections) throw std::runtime_error("target input already has its maximum number of connections");
    const Connection candidate{from_node, from_port, to_node, to_port};
    if (std::find(connections_.begin(), connections_.end(), candidate) != connections_.end())
        throw std::runtime_error("connection already exists");
    connections_.push_back(candidate);
    try {
        (void)topological_order();
        if (source->second.kind == NodeKind::DatasetInput &&
            target->second.kind == NodeKind::TaskConfig)
            (void)synchronize_task_dataset_binding(*this, to_node);
    } catch (...) {
        connections_.pop_back();
        throw;
    }
}

void WorkflowGraph::remove_connection(const Connection& connection) {
    const auto it = std::find(connections_.begin(), connections_.end(), connection);
    if (it == connections_.end()) throw std::runtime_error("connection does not exist");
    connections_.erase(it);
    const auto target = nodes_.find(connection.to_node);
    if (target != nodes_.end() && target->second.kind == NodeKind::TaskConfig &&
        connection.to_port == 0) {
        auto& binding = std::get<TaskNodeConfig>(target->second.config).dataset_binding;
        if (binding.source_dataset_node_id != 0) {
            binding.source_dataset_node_id = 0;
        }
    }
}

Node& WorkflowGraph::node(int id) {
    const auto it = nodes_.find(id);
    if (it == nodes_.end()) throw std::runtime_error("node does not exist");
    return it->second;
}

const Node& WorkflowGraph::node(int id) const {
    const auto it = nodes_.find(id);
    if (it == nodes_.end()) throw std::runtime_error("node does not exist");
    return it->second;
}

std::vector<int> WorkflowGraph::topological_order() const {
    std::map<int, int> indegree;
    for (const auto& [id, unused] : nodes_) indegree[id] = 0;
    for (const auto& edge : connections_) {
        if (indegree.count(edge.from_node) == 0 || indegree.count(edge.to_node) == 0)
            throw std::runtime_error("connection references a missing node");
        ++indegree[edge.to_node];
    }
    std::queue<int> ready;
    for (const auto& [id, degree] : indegree) if (degree == 0) ready.push(id);
    std::vector<int> order;
    while (!ready.empty()) {
        const int id = ready.front(); ready.pop(); order.push_back(id);
        for (const auto& edge : connections_) {
            if (edge.from_node == id && --indegree[edge.to_node] == 0) ready.push(edge.to_node);
        }
    }
    if (order.size() != nodes_.size()) throw std::runtime_error("workflow contains a cycle");
    return order;
}

std::vector<ValidationIssue> WorkflowGraph::validate_structure() const {
    std::vector<ValidationIssue> issues;
    for (const auto& edge : connections_) {
        const auto source = nodes_.find(edge.from_node);
        const auto target = nodes_.find(edge.to_node);
        if (source == nodes_.end() || target == nodes_.end()) {
            issues.push_back({0, -1, "connection references a missing node"});
            continue;
        }
        const auto& outputs = output_ports(source->second.kind);
        const auto& inputs = input_ports(target->second.kind);
        if (edge.from_node == edge.to_node) issues.push_back({edge.from_node, -1, "self-connection is invalid"});
        else if (edge.from_port >= outputs.size()) issues.push_back({edge.from_node, static_cast<int>(edge.from_port), "invalid output port"});
        else if (edge.to_port >= inputs.size()) issues.push_back({edge.to_node, static_cast<int>(edge.to_port), "invalid input port"});
        else if (outputs[edge.from_port].data_type != inputs[edge.to_port].data_type)
            issues.push_back({edge.to_node, static_cast<int>(edge.to_port), "connected port types are incompatible"});
    }
    for (const auto& [id, node_value] : nodes_) {
        const auto& inputs = input_ports(node_value.kind);
        for (size_t port = 0; port < inputs.size(); ++port) {
            const size_t count = static_cast<size_t>(std::count_if(connections_.begin(), connections_.end(),
                [id, port](const Connection& edge) { return edge.to_node == id && edge.to_port == port; }));
            if (inputs[port].required && count == 0)
                issues.push_back({id, static_cast<int>(port), "required input '" + inputs[port].name + "' is not connected"});
            if (count > inputs[port].max_connections)
                issues.push_back({id, static_cast<int>(port), "input '" + inputs[port].name + "' has too many connections"});
        }
    }
    try { (void)topological_order(); }
    catch (const std::exception& error) { issues.push_back({0, -1, error.what()}); }
    return issues;
}

std::vector<ValidationIssue> WorkflowGraph::validate_for_execution() const {
    auto issues = validate_structure();
    if (nodes_.empty()) {
        issues.push_back({0, -1, "workflow is empty"});
        return issues;
    }
    const bool has_task = std::any_of(nodes_.begin(), nodes_.end(), [](const auto& item) {
        return item.second.kind == NodeKind::TaskConfig;
    });
    if (!has_task) issues.push_back({0, -1, "workflow has no Task node"});

    for (const auto& [id, node_value] : nodes_) {
        if (node_value.kind == NodeKind::DatasetInput) {
            const auto& config = std::get<DatasetConfig>(node_value.config);
            const bool has_valid_item = std::any_of(config.items.begin(), config.items.end(),
                [](const DatasetConfig::Item& item) { return item.valid; });
            if (config.directory.empty() && !has_valid_item)
                issues.push_back({id, -1, "dataset has no valid imported images"});
            else if (!config.directory.empty() &&
                     !std::filesystem::is_directory(config.directory))
                issues.push_back({id, -1, "dataset directory does not exist"});
            if (config.bucket_capacity != 1)
                issues.push_back({id, -1, "standard sg/cls datasets require bucket capacity 1"});
        } else if (node_value.kind == NodeKind::TaskConfig) {
            const auto& config = std::get<TaskNodeConfig>(node_value.config);
            if (config.task != "sg" && config.task != "cls") issues.push_back({id, -1, "task type must be sg or cls"});
            if (config.model_index < 0) issues.push_back({id, -1, "model index must be non-negative"});
            if (!supported_scale(config.model_scale)) issues.push_back({id, -1, "model scale must be n, s, m, l, or x"});
            else if ((config.task == "sg" || config.task == "cls") &&
                     !supports_model_spec(config.task, config.model_index, config.model_scale))
                issues.push_back({id, -1, "model spec is not registered for the selected task"});
            if (config.training.batch_size <= 0 || config.training.epochs <= 0 ||
                config.training.learning_rate <= 0.0f || config.training.weight_decay < 0.0f)
                issues.push_back({id, -1, "training batch size, epochs, and learning rate must be positive"});
            if (config.inference_batch_size <= 0)
                issues.push_back({id, -1, "inference batch size must be positive"});
            if (config.training.image_width <= 0 || config.training.image_height <= 0 ||
                config.training.image_width > 1024 || config.training.image_height > 1024)
                issues.push_back({id, -1, "training image size must be in [1, 1024]"});
            if (config.dataset_binding.source_dataset_node_id == 0)
                issues.push_back({id, 0, "connect one Dataset to this Task"});
        }
    }
    return issues;
}

std::map<int, RuntimeValue> WorkflowGraph::execute(const Runner& runner) {
    const auto structural_issues = validate_structure();
    if (!structural_issues.empty()) throw std::runtime_error(structural_issues.front().message);
    const auto order = topological_order();
    std::map<int, RuntimeValue> values;
    for (const int id : order) {
        auto& current = nodes_.at(id);
        current.status = NodeStatus::Running;
        std::vector<RuntimeValue> inputs(input_ports(current.kind).size(), DatasetValue{});
        for (const auto& edge : connections_) {
            if (edge.to_node == id) inputs[edge.to_port] = values.at(edge.from_node);
        }
        try {
            values.emplace(id, runner(current, inputs));
            current.status = NodeStatus::Success;
        } catch (...) {
            current.status = NodeStatus::Error;
            throw;
        }
    }
    return values;
}

void WorkflowGraph::set_status(int id, NodeStatus status) { node(id).status = status; }

std::string WorkflowGraph::name(NodeKind kind) {
    switch (kind) {
    case NodeKind::DatasetInput: return "Dataset";
    case NodeKind::TaskConfig: return "Task";
    }
    throw std::runtime_error("unknown node kind");
}

std::string WorkflowGraph::data_type_name(DataType type) {
    switch (type) {
    case DataType::Dataset: return "Dataset";
    }
    throw std::runtime_error("unknown data type");
}

const std::vector<PortSpec>& WorkflowGraph::input_ports(NodeKind kind) {
    if (kind == NodeKind::TaskConfig) return task_inputs;
    return no_ports;
}

const std::vector<PortSpec>& WorkflowGraph::output_ports(NodeKind kind) {
    switch (kind) {
    case NodeKind::DatasetInput: return dataset_outputs;
    case NodeKind::TaskConfig: return no_ports;
    }
    throw std::runtime_error("unknown node kind");
}

} // namespace studio
