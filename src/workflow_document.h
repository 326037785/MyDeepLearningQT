#pragma once

#include "workflow.h"

#include <QByteArray>
#include <QPointF>

#include <map>

namespace studio {

struct WorkflowDocument {
    WorkflowGraph graph;
    std::map<int, QPointF> positions;
};

QByteArray serialize_workflow(
    const WorkflowGraph& graph,
    const std::map<int, QPointF>& positions);

WorkflowDocument parse_workflow(const QByteArray& data);

} // namespace studio
