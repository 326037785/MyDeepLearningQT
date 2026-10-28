#include "workflow_scene.h"
#include "model_presentation.h"
#include "task_module_catalog.h"

#include <QApplication>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QLineF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPathStroker>
#include <QMenu>
#include <QStyleOptionGraphicsItem>
#include <QTimer>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace studio {

namespace {

QPainterPath connection_path(const QPointF& source, const QPointF& target) {
    QPainterPath path(source);
    if (target.x() >= source.x() + 60.0) {
        const qreal offset = std::max<qreal>(80.0, (target.x() - source.x()) * 0.45);
        path.cubicTo(source + QPointF(offset, 0), target - QPointF(offset, 0), target);
    } else {
        const qreal corridor_y = std::min(source.y(), target.y()) - 90.0;
        const QPointF apex((source.x() + target.x()) / 2.0, corridor_y);
        path.cubicTo(source + QPointF(90, 0), apex + QPointF(90, 0), apex);
        path.cubicTo(apex - QPointF(90, 0), target - QPointF(90, 0), target);
    }
    return path;
}

QString elide(const QString& text, int width) {
    return QFontMetrics(QFont("Segoe UI", 8)).elidedText(text, Qt::ElideMiddle, width);
}

} // namespace

QColor data_type_color(DataType type) {
    switch (type) {
    case DataType::Dataset: return QColor("#27879B");
    }
    return QColor("#66757A");
}

PortItem::PortItem(int node_id, size_t port_index, const PortSpec& spec, QGraphicsItem* parent)
    : QGraphicsEllipseItem(QRectF(-7, -7, 14, 14), parent),
      node_id_(node_id), port_index_(port_index), spec_(spec) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setCursor(Qt::CrossCursor);
    setToolTip(QString::fromStdString(spec.name) + " : " +
               QString::fromStdString(WorkflowGraph::data_type_name(spec.data_type)));
    setZValue(3);
}

void PortItem::set_rejected(bool rejected) { rejected_ = rejected; update(); }
void PortItem::set_candidate(bool active, bool compatible) {
    candidate_ = active;
    candidate_compatible_ = compatible;
    update();
}

void PortItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    if (hovered_ || candidate_) {
        const QColor halo = candidate_ && !candidate_compatible_ ? QColor("#D14B45") : QColor("#2FAE76");
        painter->setPen(QPen(QColor(halo.red(), halo.green(), halo.blue(), 80), 5));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(QRectF(-10, -10, 20, 20));
    }
    painter->setPen(QPen(rejected_ ? QColor("#C63737") : QColor("#FFFFFF"), rejected_ ? 3 : 2));
    painter->setBrush(data_type_color(spec_.data_type));
    painter->drawEllipse(rect());
}

void PortItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    hovered_ = true;
    update();
    QGraphicsEllipseItem::hoverEnterEvent(event);
}

void PortItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    hovered_ = false;
    update();
    QGraphicsEllipseItem::hoverLeaveEvent(event);
}

void PortItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        static_cast<WorkflowScene*>(scene())->begin_connection_drag(this, event->scenePos());
        event->accept();
        return;
    }
    QGraphicsEllipseItem::mousePressEvent(event);
}

void PortItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    static_cast<WorkflowScene*>(scene())->update_connection_drag(event->scenePos());
    event->accept();
}

void PortItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    static_cast<WorkflowScene*>(scene())->finish_connection_drag(this, event->scenePos());
    event->accept();
}

NodeItem::NodeItem(WorkflowGraph* graph, int node_id) : graph_(graph), node_id_(node_id) {
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setData(0, node_id);
    setZValue(2);
    const auto& node_value = graph_->node(node_id_);
    const auto& inputs = WorkflowGraph::input_ports(node_value.kind);
    const auto& outputs = WorkflowGraph::output_ports(node_value.kind);
    for (size_t i = 0; i < inputs.size(); ++i) {
        auto* item = new PortItem(node_id_, i, inputs[i], this);
        item->setPos(0, 47 + static_cast<qreal>(i) * 22);
        input_ports_.push_back(item);
    }
    for (size_t i = 0; i < outputs.size(); ++i) {
        auto* item = new PortItem(node_id_, i, outputs[i], this);
        item->setPos(Width, 47 + static_cast<qreal>(i) * 22);
        output_ports_.push_back(item);
    }
}

qreal NodeItem::height() const {
    const auto& node_value = graph_->node(node_id_);
    const size_t rows = std::max<size_t>(1, std::max(
        WorkflowGraph::input_ports(node_value.kind).size(), WorkflowGraph::output_ports(node_value.kind).size()));
    return 32.0 + static_cast<qreal>(rows) * 22.0 + 46.0;
}

QRectF NodeItem::boundingRect() const { return QRectF(-10, -10, Width + 20, height() + 24); }

QString NodeItem::summary() const {
    const auto& node_value = graph_->node(node_id_);
    if (node_value.kind == NodeKind::DatasetInput) {
        const auto& config = std::get<DatasetConfig>(node_value.config);
        const int valid = static_cast<int>(std::count_if(config.items.begin(), config.items.end(),
            [](const DatasetConfig::Item& item) { return item.valid; }));
        if (!config.items.empty()) return QString("%1 valid image(s)").arg(valid);
        return config.directory.empty() ? "No images imported" : elide(QString::fromStdString(config.directory), 168);
    }
    if (node_value.kind == NodeKind::TaskConfig) {
        const auto& config = std::get<TaskNodeConfig>(node_value.config);
        if (config.task.empty() || config.model_scale.empty()) return "Task and model not configured";
        const auto* module = find_task_module(config.task);
        const QString task_name = module ? QString::fromLatin1(module->display_name)
                                         : QString::fromStdString(config.task);
        return task_name + " | model " + QString::number(config.model_index) + " · " +
               model_scale_name(QString::fromStdString(config.model_scale)) +
               (config.device.type == jocelin::config::DeviceType::Gpu ? " | GPU" : " | CPU");
    }
    return {};
}

QString NodeItem::status_text() const {
    const auto status = graph_->node(node_id_).status;
    if (status == NodeStatus::Running) return "RUNNING";
    if (status == NodeStatus::Success) return "SUCCESS";
    if (status == NodeStatus::Error) return "ERROR";
    if (!issues_.empty()) return "NOT READY";
    return "READY";
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    const auto& node_value = graph_->node(node_id_);
    QColor border("#739A99");
    QColor body("#FFFFFF");
    if (!issues_.empty()) { border = QColor("#D09232"); body = QColor("#FFF8E8"); }
    if (node_value.status == NodeStatus::Running) { border = QColor("#D09232"); body = QColor("#FFF3CF"); }
    if (node_value.status == NodeStatus::Success) { border = QColor("#3B8A61"); body = QColor("#EAF8EF"); }
    if (node_value.status == NodeStatus::Error) { border = QColor("#C63737"); body = QColor("#FDECEC"); }
    if (isSelected()) border = QColor("#116A73");

    const qreal node_height = height();
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(23, 63, 70, 25));
    painter->drawRoundedRect(QRectF(2, 5, Width, node_height + 3), 7, 7);

    QLinearGradient body_gradient(0, 0, 0, node_height);
    body_gradient.setColorAt(0, body.lighter(103));
    body_gradient.setColorAt(1, body.darker(102));

    painter->setPen(QPen(border, isSelected() ? 3 : 2));
    painter->setBrush(body_gradient);
    painter->drawRoundedRect(QRectF(0, 0, Width, node_height), 7, 7);
    painter->setPen(Qt::NoPen);
    QLinearGradient header_gradient(0, 0, Width, 32);
    header_gradient.setColorAt(0, QColor("#123E46"));
    header_gradient.setColorAt(1, QColor("#24646A"));
    painter->setBrush(header_gradient);
    painter->drawRoundedRect(QRectF(0, 0, Width, 32), 7, 7);
    const QColor badge_color = node_value.kind == NodeKind::DatasetInput
        ? data_type_color(DataType::Dataset)
        : QColor("#C7782B");
    painter->setBrush(badge_color);
    painter->drawEllipse(QRectF(10, 8, 16, 16));
    painter->setPen(Qt::white);
    QFont title_font = painter->font(); title_font.setPointSizeF(8.5); title_font.setWeight(QFont::DemiBold);
    painter->setFont(title_font);
    QString node_title = QString::fromStdString(WorkflowGraph::name(node_value.kind));
    if (node_value.kind == NodeKind::TaskConfig) {
        const auto& task = std::get<TaskNodeConfig>(node_value.config);
        if (const auto* module = find_task_module(task.task))
            node_title = QString::fromLatin1(module->display_name);
    }
    painter->drawText(QRectF(32, 0, Width - 40, 32), Qt::AlignVCenter,
                      node_title + "  #" + QString::number(node_id_));
    QFont badge_font = painter->font(); badge_font.setPointSizeF(6.5); badge_font.setBold(true);
    painter->setFont(badge_font);
    painter->drawText(QRectF(10, 8, 16, 16), Qt::AlignCenter,
                      node_title.left(1));

    QFont detail_font = painter->font(); detail_font.setPointSizeF(7.0); detail_font.setWeight(QFont::Normal);
    painter->setFont(detail_font);
    painter->setPen(QColor("#334B50"));
    const auto& inputs = WorkflowGraph::input_ports(node_value.kind);
    for (size_t i = 0; i < inputs.size(); ++i)
        painter->drawText(QRectF(10, 37 + static_cast<qreal>(i) * 22, 86, 20), Qt::AlignVCenter,
                          QString::fromStdString(inputs[i].name));
    const auto& outputs = WorkflowGraph::output_ports(node_value.kind);
    for (size_t i = 0; i < outputs.size(); ++i)
        painter->drawText(QRectF(Width - 96, 37 + static_cast<qreal>(i) * 22, 86, 20), Qt::AlignVCenter | Qt::AlignRight,
                          QString::fromStdString(outputs[i].name));

    painter->setPen(QColor("#587075"));
    const size_t port_rows = std::max<size_t>(1, std::max(inputs.size(), outputs.size()));
    const qreal summary_y = 37.0 + static_cast<qreal>(port_rows) * 22.0;
    const QString compact_summary = QFontMetrics(painter->font()).elidedText(
        summary(), Qt::ElideRight, static_cast<int>(Width - 20));
    painter->drawText(QRectF(10, summary_y, Width - 20, 18), Qt::AlignVCenter, compact_summary);
    painter->setPen(QPen(QColor("#D9E5E3"), 1));
    painter->drawLine(QPointF(8, node_height - 22), QPointF(Width - 8, node_height - 22));
    painter->setPen(border);
    detail_font.setWeight(QFont::DemiBold); painter->setFont(detail_font);
    painter->drawText(QRectF(10, node_height - 22, Width - 20, 18), Qt::AlignVCenter | Qt::AlignRight, status_text());
}

PortItem* NodeItem::port(PortDirection direction, size_t index) const {
    const auto& list = direction == PortDirection::Input ? input_ports_ : output_ports_;
    return index < list.size() ? list[index] : nullptr;
}

void NodeItem::set_validation(const std::vector<ValidationIssue>& issues) {
    issues_ = issues;
    for (size_t i = 0; i < input_ports_.size(); ++i) {
        const bool rejected = std::any_of(issues_.begin(), issues_.end(), [i](const ValidationIssue& issue) {
            return issue.port_index == static_cast<int>(i);
        });
        input_ports_[i]->set_rejected(rejected);
    }
    update();
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    const auto result = QGraphicsObject::itemChange(change, value);
    if (change == ItemPositionHasChanged && scene())
        static_cast<WorkflowScene*>(scene())->node_moved(node_id_, value.toPointF());
    if (change == ItemSelectedHasChanged) update();
    return result;
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    drag_start_position_ = pos();
    drag_start_scene_position_ = event->scenePos();
    drag_moved_ = false;
    QGraphicsObject::mousePressEvent(event);
}

void NodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    const QPointF pointer_delta = event->scenePos() - drag_start_scene_position_;
    if (pointer_delta.manhattanLength() >= QApplication::startDragDistance()) drag_moved_ = true;
    QGraphicsObject::mouseMoveEvent(event);
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    QGraphicsObject::mouseReleaseEvent(event);
    if (scene() && drag_moved_ && pos() != drag_start_position_) {
        auto* workflow_scene = static_cast<WorkflowScene*>(scene());
        if (workflow_scene->grid_visible()) {
            constexpr qreal grid = 28.0;
            const QPointF snapped(std::round(pos().x() / grid) * grid,
                                  std::round(pos().y() / grid) * grid);
            if (snapped != pos()) setPos(snapped);
        }
        static_cast<WorkflowScene*>(scene())->node_move_completed();
    }
    drag_moved_ = false;
}

void NodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    static_cast<WorkflowScene*>(scene())->activate_node(node_id_);
    event->accept();
}

void NodeItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QMenu menu;
    auto* open = menu.addAction(QObject::tr("Open node workspace"));
    QAction* train = nullptr;
    QAction* infer = nullptr;
    if (graph_->node(node_id_).kind == NodeKind::TaskConfig) {
        menu.addSeparator();
        train = menu.addAction(QObject::tr("Train..."));
        infer = menu.addAction(QObject::tr("Infer..."));
    }
    QAction* selected = menu.exec(event->screenPos());
    auto* workflow_scene = static_cast<WorkflowScene*>(scene());
    if (selected == open) workflow_scene->activate_node(node_id_);
    else if (selected == train) workflow_scene->request_node_action(node_id_, "train");
    else if (selected == infer) workflow_scene->request_node_action(node_id_, "infer");
    event->accept();
}

ConnectionItem::ConnectionItem(Connection connection, PortItem* source, PortItem* target)
    : connection_(connection), source_(source), target_(target) {
    setFlags(ItemIsSelectable);
    setZValue(1);
    setCursor(Qt::PointingHandCursor);
    update_path();
}

void ConnectionItem::update_path() {
    const QPointF source = source_->scenePos();
    const QPointF target = target_->scenePos();
    setPath(connection_path(source, target));
    const qreal angle = -qDegreesToRadians(path().angleAtPercent(1.0));
    arrow_a_ = target - QPointF(std::cos(angle - 0.55) * 11, -std::sin(angle - 0.55) * 11);
    arrow_b_ = target - QPointF(std::cos(angle + 0.55) * 11, -std::sin(angle + 0.55) * 11);
    update();
}

QPainterPath ConnectionItem::shape() const {
    QPainterPathStroker stroker;
    stroker.setWidth(12);
    return stroker.createStroke(path());
}

void ConnectionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    const QColor color = isSelected() ? QColor("#C7782B") : data_type_color(source_->data_type());
    painter->setPen(QPen(color, isSelected() ? 4 : 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path());
    painter->setBrush(color);
    painter->setPen(Qt::NoPen);
    QPolygonF arrow; arrow << path().pointAtPercent(1.0) << arrow_a_ << arrow_b_;
    painter->drawPolygon(arrow);
}

WorkflowScene::WorkflowScene(QObject* parent) : QGraphicsScene(parent) {
    setSceneRect(-1000, -800, 2000, 1600);
}

void WorkflowScene::set_model(WorkflowGraph* graph, std::map<int, QPointF>* positions) {
    graph_ = graph;
    positions_ = positions;
    rebuild();
}

void WorkflowScene::rebuild() {
    ++rebuild_generation_;
    clear();
    node_items_.clear();
    connection_items_.clear();
    drag_start_ = nullptr;
    candidate_target_ = nullptr;
    drag_path_ = nullptr;
    if (!graph_ || !positions_) return;
    if (graph_->nodes().empty()) {
        setSceneRect(-320, -180, 640, 360);
        auto* empty = addText("Empty workflow\nAdd a node or load the example workflow");
        empty->setDefaultTextColor(QColor("#6F8588"));
        QFont font = QApplication::font();
        font.setPointSize(font.pointSize() + 2);
        empty->setFont(font);
        empty->setTextWidth(360);
        empty->setPos(-180, -45);
        return;
    }
    int index = 0;
    for (const auto& [node_id, unused] : graph_->nodes()) {
        if (positions_->count(node_id) == 0) (*positions_)[node_id] = QPointF(-300 + (index % 3) * 360, -180 + (index / 3) * 230);
        auto* item = new NodeItem(graph_, node_id);
        addItem(item);
        item->setPos(positions_->at(node_id));
        node_items_[node_id] = item;
        ++index;
    }
    sync_connections();
    setSceneRect(itemsBoundingRect().adjusted(-500, -400, 500, 400));
}

void WorkflowScene::sync_connections() {
    for (auto* item : connection_items_) delete item;
    connection_items_.clear();
    if (!graph_) return;
    for (const auto& edge : graph_->connections()) {
        auto* source = node_items_.at(edge.from_node_id)->port(PortDirection::Output, edge.from_port);
        auto* target = node_items_.at(edge.to_node_id)->port(PortDirection::Input, edge.to_port);
        auto* item = new ConnectionItem(edge, source, target);
        addItem(item);
        connection_items_.push_back(item);
    }
}

void WorkflowScene::refresh_validation(const std::vector<ValidationIssue>& issues) {
    for (const auto& [node_id, item] : node_items_) {
        std::vector<ValidationIssue> node_issues;
        std::copy_if(issues.begin(), issues.end(), std::back_inserter(node_issues), [node_id](const ValidationIssue& issue) {
            return issue.node_id == node_id;
        });
        item->set_validation(node_issues);
    }
}

void WorkflowScene::refresh_node(int node_id) {
    const auto item = node_items_.find(node_id);
    if (item != node_items_.end()) item->second->update();
}

int WorkflowScene::selected_node_id() const {
    for (auto* item : selectedItems()) {
        if (auto* node = dynamic_cast<NodeItem*>(item)) return node->data(0).toInt();
    }
    return 0;
}

bool WorkflowScene::delete_selected_items() {
    if (!graph_ || !positions_) return false;
    std::vector<Connection> edges;
    std::vector<int> nodes;
    for (auto* item : selectedItems()) {
        if (auto* connection = dynamic_cast<ConnectionItem*>(item)) edges.push_back(connection->connection());
        if (auto* node = dynamic_cast<NodeItem*>(item)) nodes.push_back(node->data(0).toInt());
    }
    if (edges.empty() && nodes.empty()) return false;
    if (!nodes.empty() && before_nodes_removed && !before_nodes_removed(nodes)) return true;
    for (const auto& edge : edges) graph_->remove_connection(edge);
    for (const int node_id : nodes) { graph_->remove_node(node_id); positions_->erase(node_id); }
    rebuild();
    if (model_changed) model_changed();
    if (!nodes.empty() && nodes_removed) nodes_removed();
    return true;
}

void WorkflowScene::begin_connection_drag(PortItem* port, const QPointF& scene_position) {
    drag_start_ = port;
    if (drag_path_) delete drag_path_;
    drag_path_ = addPath(connection_path(port->scenePos(), scene_position), QPen(data_type_color(port->data_type()), 2, Qt::DashLine));
    drag_path_->setZValue(5);
}

void WorkflowScene::update_connection_drag(const QPointF& scene_position) {
    if (!drag_start_ || !drag_path_) return;
    clear_candidate();
    candidate_target_ = nearby_input_port(scene_position);
    QPointF endpoint = scene_position;
    if (candidate_target_) {
        const bool compatible = drag_start_->direction() == PortDirection::Output &&
            candidate_target_->direction() == PortDirection::Input &&
            drag_start_->data_type() == candidate_target_->data_type() &&
            drag_start_->node_id() != candidate_target_->node_id();
        candidate_target_->set_candidate(true, compatible);
        endpoint = candidate_target_->scenePos();
    }
    drag_path_->setPath(connection_path(drag_start_->scenePos(), endpoint));
}

PortItem* WorkflowScene::port_at(const QPointF& scene_position) const {
    for (auto* item : items(scene_position)) {
        if (auto* port = dynamic_cast<PortItem*>(item)) return port;
    }
    return nullptr;
}

PortItem* WorkflowScene::nearby_input_port(const QPointF& scene_position) const {
    PortItem* nearest = nullptr;
    qreal best_distance = 32.0;
    for (const auto& [node_id, node] : node_items_) {
        const auto& inputs = WorkflowGraph::input_ports(graph_->node(node_id).kind);
        for (size_t index = 0; index < inputs.size(); ++index) {
            auto* port = node->port(PortDirection::Input, index);
            const qreal distance = QLineF(scene_position, port->scenePos()).length();
            if (distance < best_distance) { best_distance = distance; nearest = port; }
        }
    }
    return nearest;
}

void WorkflowScene::clear_candidate() {
    if (candidate_target_) candidate_target_->set_candidate(false);
    candidate_target_ = nullptr;
}

void WorkflowScene::reject_drag(const QString& message, PortItem* target) {
    if (drag_path_) drag_path_->setPen(QPen(QColor("#C63737"), 3, Qt::DashLine));
    if (drag_start_) drag_start_->set_rejected(true);
    if (target) target->set_rejected(true);
    if (feedback) feedback(message);
    const std::uint64_t generation = rebuild_generation_;
    QTimer::singleShot(1200, this, [this, generation] {
        if (generation != rebuild_generation_) return;
        if (drag_path_) { delete drag_path_; drag_path_ = nullptr; }
        if (drag_start_) drag_start_->set_rejected(false);
        drag_start_ = nullptr;
        for (const auto& [node_id, node] : node_items_) {
            const auto& inputs = WorkflowGraph::input_ports(graph_->node(node_id).kind);
            const auto& outputs = WorkflowGraph::output_ports(graph_->node(node_id).kind);
            for (size_t i = 0; i < inputs.size(); ++i) node->port(PortDirection::Input, i)->set_rejected(false);
            for (size_t i = 0; i < outputs.size(); ++i) node->port(PortDirection::Output, i)->set_rejected(false);
        }
        if (validation_refresh_requested) validation_refresh_requested();
    });
}

void WorkflowScene::finish_connection_drag(PortItem*, const QPointF& scene_position) {
    if (!drag_start_ || !graph_) return;
    PortItem* target = port_at(scene_position);
    if (!target) target = nearby_input_port(scene_position);
    clear_candidate();
    if (!target) { reject_drag("Connection rejected: drop on an input port"); return; }
    if (drag_start_->direction() != PortDirection::Output || target->direction() != PortDirection::Input) {
        reject_drag("Connection rejected: connections must be Output to Input", target);
        return;
    }
    try {
        graph_->connect(drag_start_->node_id(), drag_start_->port_index(), target->node_id(), target->port_index());
        delete drag_path_; drag_path_ = nullptr; drag_start_ = nullptr;
        sync_connections();
        if (feedback) feedback("Connection created");
        if (model_changed) model_changed();
    } catch (const std::exception& error) {
        reject_drag("Connection rejected: " + QString::fromUtf8(error.what()), target);
    }
}

void WorkflowScene::node_moved(int node_id, const QPointF& position) {
    if (positions_) (*positions_)[node_id] = position;
    update_connections_for_node(node_id);
    const auto item = node_items_.find(node_id);
    if (item != node_items_.end()) setSceneRect(sceneRect().united(item->second->sceneBoundingRect().adjusted(-400, -300, 400, 300)));
}

void WorkflowScene::node_move_completed() {
    if (position_changed) position_changed();
}

void WorkflowScene::activate_node(int node_id) { if (node_activated) node_activated(node_id); }
void WorkflowScene::request_node_action(int node_id, const QString& action) {
    if (node_action_requested) node_action_requested(node_id, action);
}

void WorkflowScene::update_connections_for_node(int node_id) {
    for (auto* item : connection_items_) {
        const auto& edge = item->connection();
        if (edge.from_node_id == node_id || edge.to_node_id == node_id) item->update_path();
    }
}

void WorkflowScene::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, QColor("#E9EFED"));
    if (!grid_visible_) return;
    constexpr int minor = 28;
    constexpr int major = 140;
    const int left = static_cast<int>(std::floor(rect.left() / minor)) * minor;
    const int top = static_cast<int>(std::floor(rect.top() / minor)) * minor;
    QVector<QLineF> minor_lines;
    QVector<QLineF> major_lines;
    for (int x = left; x < rect.right(); x += minor)
        (x % major == 0 ? major_lines : minor_lines).append(QLineF(x, rect.top(), x, rect.bottom()));
    for (int y = top; y < rect.bottom(); y += minor)
        (y % major == 0 ? major_lines : minor_lines).append(QLineF(rect.left(), y, rect.right(), y));
    painter->setPen(QPen(QColor(74, 105, 105, 11), 0)); painter->drawLines(minor_lines);
    painter->setPen(QPen(QColor(74, 105, 105, 24), 0)); painter->drawLines(major_lines);
}

} // namespace studio
