#pragma once

#include "workflow.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsObject>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QPointF>
#include <QColor>

#include <cstdint>
#include <functional>
#include <map>
#include <set>

namespace studio {

class WorkflowScene;

class PortItem final : public QGraphicsEllipseItem {
public:
    PortItem(int node_id, size_t port_index, const PortSpec& spec, QGraphicsItem* parent);

    int node_id() const { return node_id_; }
    size_t port_index() const { return port_index_; }
    PortDirection direction() const { return spec_.direction; }
    DataType data_type() const { return spec_.data_type; }
    const PortSpec& spec() const { return spec_; }
    void set_rejected(bool rejected);
    void set_candidate(bool active, bool compatible = true);

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    int node_id_;
    size_t port_index_;
    PortSpec spec_;
    bool rejected_ = false;
    bool hovered_ = false;
    bool candidate_ = false;
    bool candidate_compatible_ = true;
};

class NodeItem final : public QGraphicsObject {
public:
    static constexpr qreal Width = 280.0;
    NodeItem(WorkflowGraph* graph, int node_id);

    QRectF boundingRect() const override;
    qreal height() const;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    PortItem* port(PortDirection direction, size_t index) const;
    void set_validation(const std::vector<ValidationIssue>& issues);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    QString summary() const;
    QString status_text() const;

    WorkflowGraph* graph_;
    int node_id_;
    std::vector<PortItem*> input_ports_;
    std::vector<PortItem*> output_ports_;
    std::vector<ValidationIssue> issues_;
    QPointF drag_start_position_;
    QPointF drag_start_scene_position_;
    bool drag_moved_ = false;
};

class ConnectionItem final : public QGraphicsPathItem {
public:
    ConnectionItem(Connection connection, PortItem* source, PortItem* target);

    const Connection& connection() const { return connection_; }
    void update_path();
    QPainterPath shape() const override;

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    Connection connection_;
    PortItem* source_;
    PortItem* target_;
    QPointF arrow_a_;
    QPointF arrow_b_;
};

class WorkflowScene final : public QGraphicsScene {
public:
    explicit WorkflowScene(QObject* parent = nullptr);

    void set_model(WorkflowGraph* graph, std::map<int, QPointF>* positions);
    WorkflowGraph* model() const { return graph_; }
    void rebuild();
    void refresh_validation(const std::vector<ValidationIssue>& issues);
    void refresh_node(int node_id);
    int selected_node_id() const;
    bool delete_selected_items();
    void set_grid_visible(bool visible) { grid_visible_ = visible; update(); }
    bool grid_visible() const { return grid_visible_; }

    std::function<void()> model_changed;
    // Called before mutating topology, while all affected edges still exist.
    std::function<bool(const std::vector<int>&)> before_nodes_removed;
    std::function<void()> nodes_removed;
    std::function<void()> position_changed;
    std::function<void()> validation_refresh_requested;
    std::function<void(int node_id)> node_activated;
    std::function<void(int node_id, const QString&)> node_action_requested;
    std::function<void(const QString&)> feedback;

    void begin_connection_drag(PortItem* port, const QPointF& scene_position);
    void update_connection_drag(const QPointF& scene_position);
    void finish_connection_drag(PortItem* port, const QPointF& scene_position);
    void node_moved(int node_id, const QPointF& position);
    void node_move_completed();
    void activate_node(int node_id);
    void request_node_action(int node_id, const QString& action);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    void sync_connections();
    void update_connections_for_node(int node_id);
    void reject_drag(const QString& message, PortItem* target = nullptr);
    PortItem* port_at(const QPointF& scene_position) const;
    PortItem* nearby_input_port(const QPointF& scene_position) const;
    void clear_candidate();

    WorkflowGraph* graph_ = nullptr;
    std::map<int, QPointF>* positions_ = nullptr;
    std::map<int, NodeItem*> node_items_;
    std::vector<ConnectionItem*> connection_items_;
    PortItem* drag_start_ = nullptr;
    PortItem* candidate_target_ = nullptr;
    QGraphicsPathItem* drag_path_ = nullptr;
    std::uint64_t rebuild_generation_ = 0;
    bool grid_visible_ = true;
};

QColor data_type_color(DataType type);

} // namespace studio
