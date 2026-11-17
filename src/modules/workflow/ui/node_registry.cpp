#include "modules/workflow/ui/node_registry.h"

#include <stdexcept>

namespace studio {

const std::vector<NodeDescriptor>& node_descriptors() {
    static const std::vector<NodeDescriptor> descriptors{
        {NodeKind::DatasetInput, "Dataset", "D", QColor("#1F8A9E"), QPointF(-440, -190), 210},
        {NodeKind::TaskConfig, "Task", "T", QColor("#D18432"), QPointF(20, -190), 210}};
    return descriptors;
}

const NodeDescriptor& node_descriptor(NodeKind kind) {
    for (const auto& descriptor : node_descriptors()) {
        if (descriptor.kind == kind) return descriptor;
    }
    throw std::runtime_error("node kind is not registered");
}

} // namespace studio
