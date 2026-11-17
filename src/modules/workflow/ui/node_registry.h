#pragma once

#include "modules/workflow/model/workflow.h"

#include <QColor>
#include <QPointF>
#include <QString>

#include <vector>

namespace studio {

struct NodeDescriptor {
    NodeKind kind;
    QString title;
    QString badge;
    QColor accent;
    QPointF initial_position;
    qreal vertical_step;
};

const std::vector<NodeDescriptor>& node_descriptors();
const NodeDescriptor& node_descriptor(NodeKind kind);

} // namespace studio
