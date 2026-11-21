#include "modules/workflow/ui/workflow_module.h"
#include "app/main_window.h"
#include "modules/diagnostics/activity_log.h"
#include "common/ui/icon_provider.h"
#include "modules/workflow/ui/node_registry.h"
#include "modules/workflow/ui/node_workspace.h"
#include "modules/workflow/ui/properties_editor.h"
#include "modules/task/application/catalog/task_module_catalog.h"
#include "modules/task/application/task_workspace_service.h"
#include "modules/workflow/ui/workflow_scene.h"
#include <QAction>
#include <QDockWidget>
#include <QFrame>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>

namespace studio {
namespace {
void clear_workflow_nodes(WorkflowDocument& document) {
    while (!document.graph.nodes().empty()) document.graph.remove_node(document.graph.nodes().begin()->first);
    document.positions.clear();
}
class WorkflowView final : public QGraphicsView {
public:
    explicit WorkflowView(QGraphicsScene* scene, QWidget* parent = nullptr)
        : QGraphicsView(scene, parent) {
        setObjectName("workflowView");
        setDragMode(QGraphicsView::NoDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setRenderHint(QPainter::Antialiasing);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }

    std::function<void()> viewport_changed;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton) {
            panning_ = true;
            pan_anchor_ = event->pos();
            viewport()->setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QGraphicsView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (panning_) {
            const QPoint delta = event->pos() - pan_anchor_;
            pan_anchor_ = event->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            if (viewport_changed) viewport_changed();
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton && panning_) {
            panning_ = false;
            viewport()->unsetCursor();
            event->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override {
        const qreal current = transform().m11();
        const qreal requested = event->angleDelta().y() > 0 ? current * 1.15 : current / 1.15;
        const qreal bounded = std::clamp(requested, 0.25, 4.0);
        if (!qFuzzyCompare(current, bounded)) scale(bounded / current, bounded / current);
        if (viewport_changed) viewport_changed();
        event->accept();
    }

    void resizeEvent(QResizeEvent* event) override {
        QGraphicsView::resizeEvent(event);
        if (viewport_changed) viewport_changed();
    }

private:
    bool panning_ = false;
    QPoint pan_anchor_;
};

class WorkflowMinimap final : public QGraphicsView {
public:
    explicit WorkflowMinimap(QGraphicsView* source, QWidget* parent = nullptr)
        : QGraphicsView(source ? source->scene() : nullptr, parent), source_(source) {
        setObjectName("workflowMinimapView");
        setInteractive(false);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setRenderHint(QPainter::Antialiasing);
        setMinimumHeight(92);
        setMaximumHeight(128);
    }

    void refresh_overview() {
        QRectF bounds;
        if (scene()) {
            for (QGraphicsItem* item : scene()->items()) {
                if (auto* node = dynamic_cast<NodeItem*>(item))
                    bounds = bounds.isNull() ? node->sceneBoundingRect() : bounds.united(node->sceneBoundingRect());
            }
        }
        if (bounds.isEmpty()) bounds = QRectF(-300, -200, 600, 400);
        fitInView(bounds.adjusted(-50, -50, 50, 50), Qt::KeepAspectRatio);
        viewport()->update();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !source_) return;
        dragging_ = true;
        source_->centerOn(mapToScene(event->pos()));
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!dragging_ || !source_) return;
        source_->centerOn(mapToScene(event->pos()));
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !dragging_) return;
        dragging_ = false;
        viewport()->setCursor(Qt::PointingHandCursor);
        event->accept();
    }

    void resizeEvent(QResizeEvent* event) override {
        QGraphicsView::resizeEvent(event);
        refresh_overview();
    }

    void paintEvent(QPaintEvent* event) override {
        QGraphicsView::paintEvent(event);
        if (!source_ || !source_->scene()) return;
        const QRectF visible = source_->mapToScene(source_->viewport()->rect()).boundingRect();
        const QPolygon viewport_polygon = mapFromScene(visible);
        QPainter painter(viewport());
        painter.setBrush(QColor(14, 166, 122, 35));
        painter.setPen(QPen(QColor("#0EA67A"), 2));
        painter.drawPolygon(viewport_polygon);
    }

private:
    QGraphicsView* source_ = nullptr;
    bool dragging_ = false;
};

QString issue_text(const ValidationIssue& issue) {
    QString text;
    if (issue.node_id != 0) text += WorkflowModule::tr("Node %1: ").arg(issue.node_id);
    return text + QString::fromStdString(issue.message);
}

QRectF node_bounds(WorkflowScene* scene) {
    QRectF result;
    for (QGraphicsItem* item : scene->items()) {
        if (auto* node = dynamic_cast<NodeItem*>(item))
            result = result.isNull() ? node->sceneBoundingRect() : result.united(node->sceneBoundingRect());
    }
    return result;
}

bool arrange_selected_nodes(WorkflowScene* scene, const QString& command, QString* error) {
    QList<NodeItem*> nodes;
    for (QGraphicsItem* item : scene->selectedItems())
        if (auto* node = dynamic_cast<NodeItem*>(item)) nodes.append(node);
    const int required = command.startsWith("distribute") ? 3 : 2;
    if (nodes.size() < required) {
        if (error) *error = WorkflowModule::tr("Select at least %1 nodes for this command.").arg(required);
        return false;
    }
    if (command == "align-left") {
        const qreal x = (*std::min_element(nodes.begin(), nodes.end(), [](auto* a, auto* b) { return a->x() < b->x(); }))->x();
        for (auto* node : nodes) node->setX(x);
    } else if (command == "align-top") {
        const qreal y = (*std::min_element(nodes.begin(), nodes.end(), [](auto* a, auto* b) { return a->y() < b->y(); }))->y();
        for (auto* node : nodes) node->setY(y);
    } else if (command == "distribute-horizontal") {
        std::sort(nodes.begin(), nodes.end(), [](auto* a, auto* b) { return a->x() < b->x(); });
        const qreal interval = (nodes.back()->x() - nodes.front()->x()) / (nodes.size() - 1);
        for (int index = 1; index + 1 < nodes.size(); ++index) nodes[index]->setX(nodes.front()->x() + interval * index);
    } else if (command == "distribute-vertical") {
        std::sort(nodes.begin(), nodes.end(), [](auto* a, auto* b) { return a->y() < b->y(); });
        const qreal interval = (nodes.back()->y() - nodes.front()->y()) / (nodes.size() - 1);
        for (int index = 1; index + 1 < nodes.size(); ++index) nodes[index]->setY(nodes.front()->y() + interval * index);
    }
    return true;
}

void fit_graph_capped(QGraphicsView* view, const QRectF& bounds, qreal maximum_scale) {
    if (!view || bounds.isEmpty()) return;
    view->fitInView(bounds, Qt::KeepAspectRatio);
    if (view->transform().m11() <= maximum_scale) return;
    view->resetTransform();
    view->scale(maximum_scale, maximum_scale);
    view->centerOn(bounds.center());
}

void fit_graph_down_only(QGraphicsView* view, const QRectF& bounds) {
    if (!view || bounds.isEmpty()) return;
    const QTransform previous = view->transform();
    const qreal previous_scale = previous.m11();
    view->fitInView(bounds, Qt::KeepAspectRatio);
    if (view->transform().m11() <= previous_scale) return;
    view->setTransform(previous);
    view->centerOn(bounds.center());
}

} // namespace

WorkflowModule::WorkflowModule(ProjectManager& project, MainWindow& shell, ActivityLog& log)
    : QObject(&shell), project_(project), shell_(shell), log_(log) {
    build_views();
    scene_->model_changed = [this] { markWorkflowChanged(); };
    scene_->before_nodes_removed = [this](const std::vector<int>& nodes) { return before_nodes_removed(nodes); };
    scene_->nodes_removed = [this] { nodes_removed(); };
    scene_->validation_refresh_requested = [this] { refreshValidation(); };
    scene_->node_activated = [this](int node_id) { openNodeWorkspace(node_id); };
    scene_->node_action_requested = [this](int node_id, const QString& action) {
        if (action == "train")
            request_training(node_id);
        else if (action == "infer") {
            openNodeWorkspace(node_id);
            node_workspace_->show_inference();
        }
    };
    scene_->feedback = [this](const QString& message) {
        log_.append_log(message);
        shell_.show_message(message, 5000);
    };
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this] { showProperties(); });
    node_workspace_->back_requested = [this] { showWorkflowWorkspace(); };
    node_workspace_->changed = [this] { markWorkflowChanged(); };
    node_workspace_->dataset_replace_requested = [this](int source, DatasetConfig dataset, int owner, const TaskDatasetBinding* imported) {
        return replaceDataset(source, std::move(dataset), owner, imported);
    };
    node_workspace_->node_requested = [this](int id) { openNodeWorkspace(id); };
    node_workspace_->status = [this](const QString& text) { shell_.show_message(text, 6000); };
    node_workspace_->run_details_requested = [this](const QString& id) { log_.show_run(project_.root_directory(), id); };
}

WorkflowModule::~WorkflowModule() { unbindWorkflowViews(); }

void WorkflowModule::build_views() {
    scene_ = new WorkflowScene(this);
    build_workspace();
    build_library();
    build_properties();
    node_workspace_ = new NodeWorkspace(&shell_);
    shell_.mount_page(workflow_workspace_);
    shell_.mount_page(node_workspace_);
}

void WorkflowModule::build_workspace() {
    view_ = new WorkflowView(scene_, &shell_);
    workflow_workspace_ = new QWidget(&shell_);
    auto* workspace_layout = new QVBoxLayout(workflow_workspace_);
    workspace_layout->setContentsMargins(0, 0, 0, 0);
    workspace_layout->setSpacing(0);
    auto* workflow_header = new QFrame(workflow_workspace_);
    workflow_header->setObjectName("workflowContextHeader");
    auto* workflow_header_layout = new QVBoxLayout(workflow_header);
    workflow_header_layout->setContentsMargins(12, 5, 12, 0);
    workflow_header_layout->setSpacing(0);
    auto* tabs_row = new QHBoxLayout;
    tabs_row->setContentsMargins(0, 0, 0, 0);
    tabs_row->setSpacing(4);
    workflow_tabs_ = new QTabBar(workflow_header);
    workflow_tabs_->setObjectName("workflowTabs");
    workflow_tabs_->setTabsClosable(false);
    workflow_tabs_->setExpanding(false);
    workflow_tabs_->setElideMode(Qt::ElideRight);
    auto* add_tab = new QPushButton(workflow_header);
    add_tab->setObjectName("addWorkflowTabButton");
    IconProvider::configure(add_tab, "add", tr("Add workflow"));
    add_tab->setFixedSize(34, 32);
    tabs_row->addWidget(workflow_tabs_);
    tabs_row->addWidget(add_tab);
    tabs_row->addStretch();
    workflow_header_layout->addLayout(tabs_row);
    workspace_layout->addWidget(workflow_header);
    auto* canvas_host = new QFrame(workflow_workspace_);
    canvas_host->setObjectName("workflowCanvasHost");
    auto* canvas_layout = new QGridLayout(canvas_host);
    canvas_layout->setContentsMargins(0, 0, 0, 0);
    canvas_layout->addWidget(view_, 0, 0);
    auto* view_tools = new QFrame(canvas_host);
    view_tools->setObjectName("canvasViewTools");
    auto* view_tools_layout = new QHBoxLayout(view_tools);
    view_tools_layout->setContentsMargins(4, 4, 4, 4);
    view_tools_layout->setSpacing(4);
    auto* grid_button = new QToolButton(view_tools);
    grid_button->setObjectName("workflowGridButton");
    IconProvider::configure(grid_button, "grid", tr("Show the workflow grid and snap moved nodes to it."));
    grid_button->setCheckable(true);
    const bool grid_visible = QSettings("Jocelin", "DeepLearningStudio").value("workflow/gridVisible", true).toBool();
    grid_button->setChecked(grid_visible);
    scene_->set_grid_visible(grid_visible);
    auto* align_button = new QToolButton(view_tools);
    align_button->setObjectName("workflowAlignButton");
    IconProvider::configure(align_button, "arrange", tr("Align or distribute the selected workflow nodes."));
    align_button->setPopupMode(QToolButton::InstantPopup);
    auto* arrange_menu = new QMenu(align_button);
    const QList<QPair<QString, QString>> arrange_commands = {
        {tr("Align left"), "align-left"}, {tr("Align top"), "align-top"},
        {tr("Distribute horizontally"), "distribute-horizontal"},
        {tr("Distribute vertically"), "distribute-vertical"}};
    for (const auto& item : arrange_commands) {
        auto* action = arrange_menu->addAction(item.first);
        connect(action, &QAction::triggered, this, [this, command = item.second] {
            QString error;
            if (!arrange_selected_nodes(scene_, command, &error)) shell_.show_message(error, 3500);
        });
    }
    align_button->setMenu(arrange_menu);
    auto* fit_button = new QToolButton(view_tools);
    fit_button->setObjectName("workflowFitButton");
    IconProvider::configure(fit_button, "fit", tr("Fit the complete workflow into view"));
    view_tools_layout->addWidget(grid_button);
    view_tools_layout->addWidget(align_button);
    view_tools_layout->addWidget(fit_button);
    canvas_layout->addWidget(view_tools, 0, 0, Qt::AlignTop | Qt::AlignRight);
    workspace_layout->addWidget(canvas_host, 1);
    connect(add_tab, &QPushButton::clicked, this, [this] { addWorkflow(); });
    connect(grid_button, &QToolButton::toggled, this, [this](bool visible) {
        scene_->set_grid_visible(visible);
        QSettings("Jocelin", "DeepLearningStudio").setValue("workflow/gridVisible", visible);
    });
    connect(fit_button, &QToolButton::clicked, this, &WorkflowModule::fit);
    connect(workflow_tabs_, &QTabBar::currentChanged, this, &WorkflowModule::activateWorkflow);
    connect(workflow_tabs_, &QTabBar::tabCloseRequested, this, &WorkflowModule::closeWorkflow);
    connect(workflow_tabs_, &QTabBar::tabBarDoubleClicked, this, &WorkflowModule::renameWorkflow);
}

void WorkflowModule::build_library() {
    library_dock_ = new QDockWidget(tr("Node Library"), &shell_);
    library_dock_->setObjectName("nodeLibraryDock");
    library_dock_->setMinimumWidth(170);
    auto* library = new QWidget(library_dock_);
    auto* library_layout = new QVBoxLayout(library);
    library_layout->setContentsMargins(8, 8, 8, 8);
    library_layout->setSpacing(5);
    auto* data_label = new QLabel(tr("Data"), library);
    data_label->setProperty("libraryGroup", true);
    library_layout->addWidget(data_label);
    auto* dataset = new QPushButton(IconProvider::icon("dataset"), tr("Dataset"), library);
    dataset->setObjectName("addDatasetButton");
    dataset->setProperty("nodeLibraryItem", true);
    dataset->setProperty("group", "data");
    dataset->setMinimumHeight(38);
    connect(dataset, &QPushButton::clicked, this, [this] { addNode(NodeKind::DatasetInput); });
    library_layout->addWidget(dataset);
    auto* task_label = new QLabel(tr("Task"), library);
    task_label->setProperty("libraryGroup", true);
    library_layout->addWidget(task_label);
    for (const auto& module : task_modules) {
        if (!module.available) continue;
        const QString id = QString::fromLatin1(module.id);
        QPushButton* button = new QPushButton(IconProvider::icon("task-" + id), tr(module.display_name), library);
        button->setObjectName("addTask_" + id + "Button");
        button->setProperty("nodeLibraryItem", true);
        button->setProperty("group", "task");
        button->setMinimumHeight(38);
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        connect(button, &QPushButton::clicked, this, [this, id] { addNode(NodeKind::TaskConfig, id); });
        library_layout->addWidget(button);
    }
    library_layout->addStretch();
    auto* example = new QPushButton(tr("Load Example Workflow"), library);
    example->setObjectName("loadExampleButton");
    example->setProperty("accentSecondary", true);
    connect(example, &QPushButton::clicked, this, [this] { loadExampleWorkflow(); });
    library_layout->addWidget(example);
    library_dock_->setWidget(library);
    shell_.mount_dock(Qt::LeftDockWidgetArea, library_dock_);
    library_dock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
}

void WorkflowModule::build_properties() {
    property_dock_ = new QDockWidget(tr("Properties"), &shell_);
    property_dock_->setObjectName("propertiesDock");
    property_dock_->setMinimumWidth(250);
    auto* property_panel = new QWidget(property_dock_);
    auto* property_layout = new QVBoxLayout(property_panel);
    property_layout->setContentsMargins(0, 0, 0, 0);
    property_layout->setSpacing(0);
    property_editor_ = new PropertiesEditor(property_panel);
    property_layout->addWidget(property_editor_, 1);
    auto* minimap = new QFrame(property_panel);
    minimap->setObjectName("minimapPanel");
    auto* minimap_layout = new QVBoxLayout(minimap);
    minimap_layout->setContentsMargins(12, 10, 12, 10);
    auto* minimap_title = new QLabel(tr("Minimap"), minimap);
    minimap_title->setProperty("libraryGroup", true);
    auto* minimap_view = new WorkflowMinimap(view_, minimap);
    minimap_view->setToolTip(tr("Drag to move around the workflow."));
    minimap_layout->addWidget(minimap_title);
    minimap_layout->addWidget(minimap_view);
    property_layout->addWidget(minimap);
    connect(scene_, &QGraphicsScene::changed, minimap_view, [minimap_view] { minimap_view->refresh_overview(); });
    connect(view_->horizontalScrollBar(), &QScrollBar::valueChanged, minimap_view, [minimap_view] { minimap_view->viewport()->update(); });
    connect(view_->verticalScrollBar(), &QScrollBar::valueChanged, minimap_view, [minimap_view] { minimap_view->viewport()->update(); });
    if (auto* workflow_view = dynamic_cast<WorkflowView*>(view_))
        workflow_view->viewport_changed = [minimap_view] { minimap_view->viewport()->update(); };
    minimap_view->refresh_overview();
    property_dock_->setWidget(property_panel);
    shell_.mount_dock(Qt::RightDockWidgetArea, property_dock_);
}

void WorkflowModule::mount(QToolBar& toolbar, QMenu& edit_menu, QMenu& view_menu) {
    const auto action = [this](const QString& text, const char* name, auto callback) {
        auto* value = new QAction(text, this);
        value->setObjectName(name);
        connect(value, &QAction::triggered, this, callback);
        editing_actions_.push_back(value);
        return value;
    };
    auto* clear = action(tr("Clear workflow"), "clearWorkflowAction", [this] { clearWorkflow(); });
    auto* remove = action(tr("Delete"), "deleteAction", [this] { deleteSelected(); });
    delete_action_ = remove;
    remove->setIcon(IconProvider::icon("trash"));
    remove->setShortcuts({QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)});
    remove->setEnabled(false);
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this] {
        delete_action_->setEnabled(workflow_workspace_->isEnabled() && !scene_->selectedItems().isEmpty());
    });
    edit_menu.addAction(remove);
    edit_menu.addAction(clear);
    toolbar.addAction(remove);
    toolbar.addSeparator();
    QWidget* spacer = new QWidget(&toolbar);
    spacer->setObjectName("workflowToolbarSpacer");
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar.addWidget(spacer);
    run_action_ = action(tr("Open Task"), "openTaskAction", [this] { runWorkflow(); });
    run_action_->setIcon(IconProvider::icon("play-white"));
    toolbar.addAction(run_action_);
    if (auto* button = qobject_cast<QToolButton*>(toolbar.widgetForAction(run_action_))) {
        button->setProperty("runAction", true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    if (auto* button = qobject_cast<QToolButton*>(toolbar.widgetForAction(remove))) button->setProperty("destructive", true);
    view_menu.addAction(library_dock_->toggleViewAction());
    view_menu.addAction(property_dock_->toggleViewAction());
}

void WorkflowModule::fit() {
    const QRectF bounds = node_bounds(scene_);
    fit_graph_capped(view_, bounds.isEmpty() ? QRectF(-300, -200, 600, 400) : bounds.adjusted(-80, -80, 80, 80), 1.0);
}
bool WorkflowModule::before_nodes_removed(const std::vector<int>& nodes) {
    std::vector<int> sources;
    for (int id : nodes) if (graph().node(id).kind == NodeKind::DatasetInput) sources.push_back(id);
    if (sources.empty()) return true;
    if (!confirmDatasetChange(sources)) return false;
    try { project_.clear_dataset_dependents(sources); return true; }
    catch (const std::exception& error) {
        QMessageBox::warning(&shell_, tr("Cannot delete Dataset"), QString::fromUtf8(error.what()));
        refreshProjectUi(); return false;
    }
}
void WorkflowModule::nodes_removed() {
    try { if (project_.has_storage()) project_.save_project(); }
    catch (const std::exception& error) { QMessageBox::warning(&shell_, tr("Cannot save Workflow"), QString::fromUtf8(error.what())); }
    refreshProjectUi();
}

WorkflowGraph& WorkflowModule::graph() { return project_.active_workflow().document.graph; }

std::map<int, QPointF>& WorkflowModule::positions() { return project_.active_workflow().document.positions; }

void WorkflowModule::bindActiveWorkflow() {
    node_workspace_->clear_node();
    scene_->set_model(&graph(), &positions());
    if (shell_.is_current_page(node_workspace_)) {
        shell_.show_workflow(workflow_workspace_);
        run_action_->setVisible(true);
        delete_action_->setVisible(true);
    }
    refreshProjectUi();
    showProperties();
    refreshValidation();
}

void WorkflowModule::unbindWorkflowViews() {
    if (scene_) scene_->set_model(nullptr, nullptr);
    if (node_workspace_) node_workspace_->clear_node();
    if (property_editor_) property_editor_->show_workflow();
}

QString WorkflowModule::connectedDatasetDirectory(int node_id) {
    for (const auto& edge : graph().connections()) {
        if (edge.to_node_id != node_id || edge.to_port != 0) continue;
        const auto& source = graph().node(edge.from_node_id);
        if (source.kind != NodeKind::DatasetInput) continue;
        return QString::fromStdString(std::get<DatasetConfig>(source.config).directory);
    }
    return {};
}

void WorkflowModule::addWorkflow() {
    if (busy && busy()) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(&shell_, tr("Add workflow"), tr("Workflow name"), QLineEdit::Normal,
                                                tr("Workflow %1").arg(project_.workflows().size() + 1), &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    if (!node_workspace_->confirm_annotation_transition()) {
        return;
    }
    unbindWorkflowViews();
    try {
        project_.add_workflow(name);
        bindActiveWorkflow();
    } catch (const std::exception& error) {
        bindActiveWorkflow();
        QMessageBox::warning(&shell_, tr("Cannot add workflow"), error.what());
    }
}

bool WorkflowModule::activateWorkflow(int index) {
    if (index < 0 || static_cast<size_t>(index) >= project_.workflows().size()) return false;
    if (busy && busy()) {
        const QSignalBlocker block(workflow_tabs_);
        workflow_tabs_->setCurrentIndex(static_cast<int>(project_.active_index()));
        return false;
    }
    if (static_cast<size_t>(index) == project_.active_index()) return true;
    if (!node_workspace_->confirm_annotation_transition()) {
        const QSignalBlocker block(workflow_tabs_);
        workflow_tabs_->setCurrentIndex(static_cast<int>(project_.active_index()));
        return false;
    }
    unbindWorkflowViews();
    try {
        project_.set_active_index(static_cast<size_t>(index));
        bindActiveWorkflow();
        return true;
    } catch (const std::exception& error) {
        bindActiveWorkflow();
        QMessageBox::warning(&shell_, tr("Cannot switch workflow"), error.what());
        return false;
    }
}

void WorkflowModule::closeWorkflow(int index) {
    if (static_cast<size_t>(index) == project_.active_index() &&
        !node_workspace_->confirm_annotation_transition()) {
        return;
    }
    if (!activateWorkflow(index)) return;
    if (QMessageBox::question(&shell_, tr("Remove workflow"),
        tr("Remove workflow '%1' from this project?").arg(project_.active_workflow().name)) != QMessageBox::Yes) return;
    unbindWorkflowViews();
    try {
        project_.remove_workflow(project_.active_index());
        bindActiveWorkflow();
    } catch (const std::exception& error) {
        bindActiveWorkflow();
        QMessageBox::warning(&shell_, tr("Cannot remove workflow"), error.what());
    }
}

void WorkflowModule::renameWorkflow(int index) {
    if (!activateWorkflow(index)) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(&shell_, tr("Rename workflow"), tr("Workflow name"), QLineEdit::Normal,
                                                project_.active_workflow().name, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    try { project_.rename_active_workflow(name); refreshProjectUi(); showProperties(); }
    catch (const std::exception& error) { QMessageBox::warning(&shell_, tr("Cannot rename workflow"), error.what()); }
}

void WorkflowModule::addNode(NodeKind kind, const QString& task_module) {
    const int kind_index = static_cast<int>(std::count_if(graph().nodes().begin(), graph().nodes().end(),
        [kind](const auto& item) { return item.second.kind == kind; }));
    const int node_id = kind == NodeKind::TaskConfig
        ? graph().add_task_node({task_module.toStdString()})
        : graph().add_node(kind);
    const auto& descriptor = node_descriptor(kind);
    positions()[node_id] = descriptor.initial_position + QPointF(0, kind_index * descriptor.vertical_step);
    scene_->rebuild();
    QGraphicsItem* added_item = nullptr;
    for (auto* item : scene_->items()) {
        if (item->data(0).toInt() == node_id) {
            item->setSelected(true);
            added_item = item;
            break;
        }
    }
    if (added_item) view_->ensureVisible(added_item, 48, 48);
    markWorkflowChanged();
}

void WorkflowModule::clearWorkflow() {
    if (!node_workspace_->confirm_annotation_transition()) {
        return;
    }
    if (!graph().nodes().empty() &&
        QMessageBox::question(&shell_, tr("Clear workflow"), tr("Remove all nodes from the active workflow?")) != QMessageBox::Yes) return;
    unbindWorkflowViews();
    clear_workflow_nodes(project_.active_workflow().document);
    project_.mark_active_dirty();
    bindActiveWorkflow();
}

void WorkflowModule::loadExampleWorkflow() {
    if (!node_workspace_->confirm_annotation_transition()) {
        return;
    }
    if (!graph().nodes().empty() &&
        QMessageBox::question(&shell_, tr("Load example"), tr("Replace the active workflow with the example?")) != QMessageBox::Yes) return;
    unbindWorkflowViews();
    clear_workflow_nodes(project_.active_workflow().document);
    auto& active_graph = graph();
    const int source_node_id = active_graph.add_node(NodeKind::DatasetInput);
    const int node_id = active_graph.add_task_node({"cls"});
    active_graph.connect(source_node_id, 0, node_id, 0);
    positions()[source_node_id] = QPointF(-300, -40);
    positions()[node_id] = QPointF(80, -40);
    project_.mark_active_dirty();
    bindActiveWorkflow();
}

void WorkflowModule::deleteSelected() {
    if (!scene_->delete_selected_items()) shell_.show_message(tr("Select a node or connection to delete"), 4000);
}

bool WorkflowModule::confirmDatasetChange(const std::vector<int>& sources, bool importing_annotations) {
    QStringList owners;
    for (const auto& edge : graph().connections())
        if (std::find(sources.begin(), sources.end(), edge.from_node_id) != sources.end() &&
            graph().node(edge.to_node_id).kind == NodeKind::TaskConfig)
            owners.append("#" + QString::number(edge.to_node_id));
    const auto detail = owners.empty() ? tr("No Tasks are currently connected.") :
        tr("Clear Annotation, sample selection/splits, training-mask associations and all Inference results for connected Tasks: %1.")
            .arg(owners.join(", "));
    return QMessageBox::question(&shell_, tr("Delete / replace Dataset"),
        detail + "\n\n" + tr("Task settings, classes, ModelList and weights are retained. Detached Tasks are unaffected.") +
        (importing_annotations ? "\n\n" + tr("The annotation package explicitly supplies the receiving Task's new classes and annotations.") : QString{}) +
        "\n\n" + tr("Continue?"), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
}

bool WorkflowModule::replaceDataset(int source, DatasetConfig dataset, int annotation_owner, const TaskDatasetBinding* imported) {
    const auto& current = std::get<DatasetConfig>(graph().node(source).config);
    const bool same_inventory = current.bucket_capacity == dataset.bucket_capacity &&
        current.items.size() == dataset.items.size() &&
        std::equal(current.items.begin(), current.items.end(), dataset.items.begin(), [](const auto& a, const auto& b) {
            return a.path == b.path && a.sample_id == b.sample_id && a.valid == b.valid;
        });
    if (!imported && same_inventory) {
        std::get<DatasetConfig>(graph().node(source).config) = std::move(dataset);
        return true;
    }
    bool destructive = imported || !current.items.empty();
    for (const auto& edge : graph().connections())
        if (edge.from_node_id == source && graph().node(edge.to_node_id).kind == NodeKind::TaskConfig) {
            const auto& task = std::get<TaskNodeConfig>(graph().node(edge.to_node_id).config);
            destructive = destructive || !task.dataset_binding.samples.empty() || !task.runtime.results.empty();
        }
    if (destructive && !confirmDatasetChange({source}, imported != nullptr)) return false;
    try {
        project_.clear_dataset_dependents({source});
        if (imported) {
            TaskWorkspaceService(graph(), annotation_owner).replace_bound_dataset(std::move(dataset), *imported);
        } else {
            std::get<DatasetConfig>(graph().node(source).config) = std::move(dataset);
            for (const auto& edge : graph().connections())
                if (edge.from_node_id == source && graph().node(edge.to_node_id).kind == NodeKind::TaskConfig)
                    TaskWorkspaceService(graph(), edge.to_node_id).synchronize();
        }
        project_.mark_active_dirty();
        if (project_.has_storage()) project_.save_project();
        refreshProjectUi();
        return true;
    } catch (const std::exception& error) {
        QMessageBox::warning(&shell_, tr("Cannot replace Dataset"), QString::fromUtf8(error.what()));
        // Let the requesting page finish its callback before rebuilding it.
        QTimer::singleShot(0, this, [this, source, annotation_owner] {
            openNodeWorkspace(annotation_owner ? annotation_owner : source);
        });
        return false;
    }
}

void WorkflowModule::showProperties() {
    const int node_id = scene_->selected_node_id();
    if (node_id != 0 && graph().nodes().count(node_id) != 0) {
        property_editor_->edit_node(&graph(), node_id, [this] { markWorkflowChanged(); });
    } else {
        property_editor_->show_workflow();
    }
}

void WorkflowModule::refreshValidation(bool report_to_log) {
    issues_ = graph().validate_for_execution();
    scene_->refresh_validation(issues_);
    run_action_->setEnabled(issues_.empty() && (!busy || !busy()));
    shell_.clear_message();
    shell_.set_context_status(issues_.empty() ? QString{}
        : tr("Needs review: %1").arg(issue_text(issues_.front())));
    if (!issues_.empty()) {
        if (report_to_log) {
            log_.append_log(tr("Workflow validation failed:"));
            for (const auto& issue : issues_) log_.append_log("- " + issue_text(issue));
            log_.show_log();
        }
    }
}

void WorkflowModule::markWorkflowChanged() {
    project_.mark_active_dirty();
    for (const auto& [node_id, unused] : graph().nodes()) {
        graph().set_status(node_id, NodeStatus::Idle);
        scene_->refresh_node(node_id);
    }
    refreshValidation();
    refreshProjectUi();
    if (scene_->selected_node_id() == 0) showProperties();
}

void WorkflowModule::runWorkflow() {
    refreshValidation(true);
    if (!issues_.empty()) return;
    const auto task = std::find_if(
        graph().nodes().begin(), graph().nodes().end(),
        [](const auto& value) {
            return value.second.kind == NodeKind::TaskConfig;
        });
    if (task == graph().nodes().end()) return;
    openNodeWorkspace(task->first);
}

void WorkflowModule::refreshProjectUi() {
    const QSignalBlocker workflow_block(workflow_tabs_);
    while (workflow_tabs_->count() > 0) workflow_tabs_->removeTab(0);
    for (const auto& workflow : project_.workflows()) {
        const int index = workflow_tabs_->addTab(workflow.name + (workflow.dirty ? " *" : ""));
        auto* close = new QToolButton(workflow_tabs_);
        close->setObjectName("workflowTabCloseButton");
        close->setText("x");
        close->setToolTip(tr("Close workflow"));
        close->setAutoRaise(true);
        close->setFixedSize(18, 18);
        workflow_tabs_->setTabButton(index, QTabBar::RightSide, close);
        connect(close, &QToolButton::clicked, this, [this, index] { closeWorkflow(index); });
    }
    workflow_tabs_->setCurrentIndex(static_cast<int>(project_.active_index()));
    if (project_changed) project_changed();
}

void WorkflowModule::showWorkflowWorkspace() {
    shell_.show_workflow(workflow_workspace_);
    run_action_->setVisible(true);
    delete_action_->setVisible(true);
    refreshProjectUi();
    QTimer::singleShot(0, this, [this] {
        if (!shell_.is_current_page(workflow_workspace_)) return;
        const QRectF bounds = node_bounds(scene_);
        if (!bounds.isEmpty())
            fit_graph_down_only(view_, bounds.adjusted(-70, -70, 70, 70));
    });
}

void WorkflowModule::openNodeWorkspace(int node_id) {
    if (graph().nodes().count(node_id) == 0) return;
    if (!node_workspace_->confirm_annotation_transition()) {
        return;
    }
    const QString dataset = graph().node(node_id).kind == NodeKind::TaskConfig
        ? connectedDatasetDirectory(node_id) : QString{};
    node_workspace_->edit_node(&graph(), node_id, dataset);
    shell_.show_task(node_workspace_);
    run_action_->setVisible(false);
    delete_action_->setVisible(false);
}

void WorkflowModule::setEditingEnabled(bool enabled) {
    for (auto* action : editing_actions_) action->setEnabled(enabled);
    delete_action_->setEnabled(enabled && !scene_->selectedItems().isEmpty());
    workflow_tabs_->setEnabled(enabled);
    workflow_workspace_->setEnabled(enabled);
    library_dock_->setEnabled(enabled);
    property_dock_->setEnabled(enabled);
    node_workspace_->setEnabled(enabled);
    view_->setInteractive(enabled);
    run_action_->setEnabled(enabled && issues_.empty());
}

void WorkflowModule::adapt_to_resize(QResizeEvent* event) {
    if (!shell_.is_current_page(workflow_workspace_) || !property_dock_) return;
    if (event->size().width() < 1200) property_dock_->hide();
    else if (event->oldSize().width() < 1200) property_dock_->show();
    if (event->oldSize().isValid() &&
        (event->size().width() < event->oldSize().width() ||
         event->size().height() < event->oldSize().height())) {
        QTimer::singleShot(0, this, [this] {
            if (!shell_.is_current_page(workflow_workspace_)) return;
            const QRectF bounds = node_bounds(scene_);
            if (bounds.isEmpty()) return;
            const QRectF visible =
                view_->mapToScene(view_->viewport()->rect()).boundingRect();
            if (!visible.contains(bounds))
                fit_graph_down_only(
                    view_, bounds.adjusted(-70, -70, 70, 70));
        });
    }
}

} // namespace studio
