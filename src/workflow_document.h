#pragma once

#include "workflow.h"

#include <QByteArray>
#include <QPointF>
#include <QString>

#include <map>
#include <utility>
#include <vector>

namespace studio {

struct WorkflowDocument {
    WorkflowGraph graph;
    std::map<int, QPointF> positions;
};

struct SerializedWorkflow {
    QByteArray json;
    // Region paths are relative to nodes/<workflow_id>/.
    std::vector<std::pair<QString, QByteArray>> region_assets;
    std::vector<std::pair<QString, QByteArray>> inference_assets;
};

SerializedWorkflow serialize_workflow(
    const WorkflowGraph& graph,
    const std::map<int, QPointF>& positions);

WorkflowDocument parse_workflow(const QByteArray& data, const QString& node_asset_root = {});

// Relative to nodes/<workflow_id>/; ids are the existing Core identities.
QString inference_result_directory(int node_id, const std::string& model_id, const std::string& weight_id);

} // namespace studio
