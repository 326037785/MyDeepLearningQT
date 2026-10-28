#include "workflow_scene.h"

#include <QApplication>
#include <QGraphicsView>
#include <QTest>

#include <map>

namespace {

studio::NodeItem* find_node(
    studio::WorkflowScene& scene,
    int node_id)
{
    for (auto* item : scene.items()) {
        auto* node =
            dynamic_cast<studio::NodeItem*>(item);
        if (node && node->data(0).toInt() == node_id)
            return node;
    }
    return nullptr;
}

studio::ConnectionItem* find_connection(
    studio::WorkflowScene& scene)
{
    for (auto* item : scene.items())
        if (auto* edge =
                dynamic_cast<studio::ConnectionItem*>(item))
            return edge;
    return nullptr;
}

void drag_port(
    QGraphicsView& view,
    studio::PortItem* source,
    studio::PortItem* target)
{
    const QPoint source_position =
        view.mapFromScene(source->scenePos());
    const QPoint target_position =
        view.mapFromScene(target->scenePos());
    QTest::mousePress(
        view.viewport(), Qt::LeftButton,
        Qt::NoModifier, source_position);
    QTest::mouseMove(
        view.viewport(), target_position, 20);
    QTest::mouseRelease(
        view.viewport(), Qt::LeftButton,
        Qt::NoModifier, target_position);
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    studio::WorkflowGraph graph;
    std::map<int, QPointF> positions;
    studio::WorkflowScene scene;
    scene.set_model(&graph, &positions);
    if (!graph.nodes().empty() ||
        !graph.connections().empty())
        return 1;

    const int source_node_id =
        graph.add_node(studio::NodeKind::DatasetInput);
    const int node_id = graph.add_task_node({"sg"});
    positions = {
        {source_node_id, {-300, -40}},
        {node_id, {80, -40}}};
    scene.rebuild();
    auto* dataset_node = find_node(scene, source_node_id);
    auto* task_node = find_node(scene, node_id);
    if (!dataset_node || !task_node ||
        !graph.connections().empty())
        return 2;
    if (task_node->port(
            studio::PortDirection::Output, 0) != nullptr)
        return 3;

    QGraphicsView view(&scene);
    view.setDragMode(QGraphicsView::NoDrag);
    view.resize(900, 600);
    view.show();
    view.fitInView(
        scene.itemsBoundingRect(),
        Qt::KeepAspectRatio);
    QApplication::processEvents();
    auto* source = dataset_node->port(
        studio::PortDirection::Output, 0);
    auto* target = task_node->port(
        studio::PortDirection::Input, 0);
    if (view.itemAt(
            view.mapFromScene(source->scenePos())) != source)
        return 4;
    drag_port(view, source, target);
    if (graph.connections().size() != 1)
        return 5;

    scene.begin_connection_drag(
        source, source->scenePos());
    scene.finish_connection_drag(
        source, target->scenePos());
    if (graph.connections().size() != 1)
        return 6;

    auto* edge = find_connection(scene);
    if (!edge) return 7;
    const QPainterPath old_path = edge->path();
    dataset_node->setPos(-360, -120);
    if (edge->path() == old_path ||
        positions.at(source_node_id) != QPointF(-360, -120))
        return 8;
    if (edge->path().pointAtPercent(0) !=
        source->scenePos())
        return 9;

    edge->setSelected(true);
    if (!scene.delete_selected_items() ||
        !graph.connections().empty())
        return 10;

    dataset_node = find_node(scene, source_node_id);
    task_node = find_node(scene, node_id);
    scene.begin_connection_drag(
        dataset_node->port(
            studio::PortDirection::Output, 0),
        dataset_node->port(
            studio::PortDirection::Output, 0)
            ->scenePos());
    scene.finish_connection_drag(
        dataset_node->port(
            studio::PortDirection::Output, 0),
        task_node->port(
            studio::PortDirection::Input, 0)
            ->scenePos());
    dataset_node->setSelected(true);
    if (!scene.delete_selected_items() ||
        graph.nodes().count(source_node_id) != 0 ||
        !graph.connections().empty() ||
        positions.count(source_node_id) != 0)
        return 11;
    return 0;
}
