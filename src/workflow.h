#pragma once

#include <common/config/augment.h>
#include <common/config/runtime_route.h>
#include <Result/cls/cls_result.h>
#include <Result/sg/sg_result.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace studio {

enum class DataType { Dataset };
enum class PortDirection { Input, Output };
enum class NodeKind { DatasetInput, TaskConfig };
enum class NodeStatus { Idle, Running, Success, Error };

struct PortSpec {
    std::string name;
    DataType data_type = DataType::Dataset;
    PortDirection direction = PortDirection::Input;
    bool required = false;
    size_t max_connections = 1;
};

struct DatasetConfig {
    struct Item {
        std::string path;
        std::string error;
        bool valid = true;
        // Stable raw-asset sample identity. Items sharing this identity belong
        // to the same bucket; standard current imports create one item per id.
        std::string sample_id;
    };

    std::string directory;
    // Imported resources are the authoritative dataset inventory. The
    // directory is only the common source root when one exists.
    std::vector<Item> items;
    // Number of images packed into one sample (one metadata image_paths
    // entry). Plain sg/cls use 1; multi-image variants pack more.
    int bucket_capacity = 1;
};

struct AnnotationClass {
    // Stable semantic identity. It is never derived from vector position.
    std::string id;
    // Pixel value written to segmentation masks. Reordering or renaming a
    // class never changes this value.
    int mask_value = 0;
    std::string name;
    std::string color;
    bool visible = true;
    bool locked = false;
    // Deletion retires a class instead of recycling its mask value. This
    // preserves the meaning of pixels in existing masks.
    bool active = true;
};

struct TaskSampleBinding {
    std::string sample_id;
    std::vector<std::string> image_paths;
    bool selected = true;
    std::string mask_path;
    // Optional task-agnostic training exclusion mask. Non-zero pixels are
    // hidden from the network input and excluded from dense supervision.
    std::string training_mask_path;
    // Stable AnnotationClass::id for classification. Segmentation masks use
    // AnnotationClass::mask_value.
    std::string class_id;
    std::string split = "unassigned";
    std::string annotation_state = "unannotated";
    std::vector<std::string> quality_issues;
    // Automatic assignments can be recalculated when split settings change.
    // Manual assignments are preserved until the user explicitly restores
    // this sample to automatic mode. Kept last for aggregate compatibility.
    bool split_manual = false;
};

struct DatasetSplitSettings {
    int train_percent = 80;
    int validation_percent = 20;
    int test_percent = 0;
    std::uint32_t seed = 42;
    std::string strategy = "stratified";
};

struct TaskPreprocessingSettings {
    bool normalize = true;
    bool bgr_to_rgb = true;
};

struct TaskDatasetBinding {
    int source_dataset_node_id = 0;
    std::vector<TaskSampleBinding> samples;
    std::vector<AnnotationClass> classes;
    DatasetSplitSettings split_settings;
    TaskPreprocessingSettings preprocessing;
};

struct TrainingSettings {
    // Runtime hyperparameter owned by the Task node, not the Dataset node.
    int batch_size = 8;
    int epochs = 100;
    float learning_rate = 0.001f;
    float weight_decay = 0.0001f;
    int image_width = 512;
    int image_height = 512;
    int workers = 4;
    std::string optimizer = "adam";
    std::string scheduler = "cosine";
    jocelin::config::Augment augment;
    std::string output_directory;
};

struct ResultReference {
    std::string result_id;
    std::string source_sample_id;
    std::variant<jocelin::result::cls::ClsResult, jocelin::result::sg::SgResult> result;
};

struct InferenceResultSet {
    std::string model_id;
    std::string weight_id;
    std::string weight_format = "libtorch";
    std::string result_directory;
    jocelin::result::cls::ClsScopeMetrics aggregate_metrics;
    std::vector<ResultReference> samples;
};

struct TaskRuntimeState {
    // The task only points at the ModelList row it currently controls.
    // Training state, progress, and checkpoint ownership live on that row.
    std::string training_model_id;
    std::string inference_status = "idle";
    std::vector<InferenceResultSet> results;
};

struct TaskNodeConfig {
    std::string task;
    int model_index = 0;
    std::string model_scale;
    jocelin::config::Device device;
    TrainingSettings training;
    int inference_batch_size = 1;
    std::string inference_output_directory;
    bool inference_use_all_images = true;
    std::vector<std::string> inference_images;
    TaskDatasetBinding dataset_binding;
    TaskRuntimeState runtime;
};

using NodeConfig = std::variant<DatasetConfig, TaskNodeConfig>;

struct DatasetValue {
    std::string directory;
    std::vector<std::string> images;
};
struct TaskConfigValue {
    std::string task;
    int model_index = 0;
    std::string model_scale;
    jocelin::config::Device device;
};
using RuntimeValue = std::variant<DatasetValue, TaskConfigValue>;

struct Node {
    int id = 0;
    NodeKind kind = NodeKind::DatasetInput;
    NodeStatus status = NodeStatus::Idle;
    NodeConfig config = DatasetConfig{};
};

struct Connection {
    int from_node = 0;
    size_t from_port = 0;
    int to_node = 0;
    size_t to_port = 0;

    bool operator==(const Connection& other) const {
        return from_node == other.from_node && from_port == other.from_port &&
               to_node == other.to_node && to_port == other.to_port;
    }
};

struct ValidationIssue {
    int node_id = 0;
    int port_index = -1;
    std::string message;
};

struct CreateTaskNodeRequest {
    std::string task;
};

class WorkflowGraph {
public:
    using Runner = std::function<RuntimeValue(const Node&, const std::vector<RuntimeValue>&)>;

    int add_node(NodeKind kind);
    int add_task_node(const CreateTaskNodeRequest& request);
    void remove_node(int id);
    void connect(int from_node, size_t from_port, int to_node, size_t to_port);
    void remove_connection(const Connection& connection);
    Node& node(int id);
    const Node& node(int id) const;
    const std::map<int, Node>& nodes() const { return nodes_; }
    const std::vector<Connection>& connections() const { return connections_; }
    std::vector<int> topological_order() const;
    std::vector<ValidationIssue> validate_for_execution() const;
    std::map<int, RuntimeValue> execute(const Runner& runner);
    void set_status(int id, NodeStatus status);

    static std::string name(NodeKind kind);
    static std::string data_type_name(DataType type);
    static const std::vector<PortSpec>& input_ports(NodeKind kind);
    static const std::vector<PortSpec>& output_ports(NodeKind kind);

private:
    std::vector<ValidationIssue> validate_structure() const;

    int next_id_ = 1;
    std::map<int, Node> nodes_;
    std::vector<Connection> connections_;
};

} // namespace studio
