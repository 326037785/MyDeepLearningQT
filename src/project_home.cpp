#include "project_home.h"
#include "recent_project_store.h"
#include "task_module_catalog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace studio {

namespace {

struct RecentProjectSummary {
    QString name;
    QString task;
};

RecentProjectSummary project_summary(const QString& path) {
    RecentProjectSummary summary{QFileInfo(path).dir().dirName(), ProjectHome::tr("Vision workflow")};
    QFile project(path);
    if (!project.open(QIODevice::ReadOnly)) return summary;
    const auto root = QJsonDocument::fromJson(project.readAll()).object();
    if (!root.value("name").toString().trimmed().isEmpty()) summary.name = root.value("name").toString().trimmed();
    const auto workflows = root.value("workflows").toArray();
    for (const auto& value : workflows) {
        const QString relative = value.toObject().value("file").toString();
        QFile workflow(QFileInfo(path).dir().filePath(relative));
        if (!workflow.open(QIODevice::ReadOnly)) continue;
        const auto nodes = QJsonDocument::fromJson(workflow.readAll()).object().value("nodes").toArray();
        for (const auto& node : nodes) {
            const QString task = node.toObject().value("config").toObject().value("task").toString();
            if (const auto* module = find_task_module(task.toStdString()))
                return {summary.name, ProjectHome::tr(module->display_name)};
        }
    }
    return summary;
}

QPushButton* nav_button(const QString& text, QWidget* parent, bool active = false) {
    auto* button = new QPushButton(text, parent);
    button->setProperty("homeNav", true);
    button->setProperty("active", active);
    return button;
}

} // namespace

ProjectHome::ProjectHome(QWidget* parent) : QWidget(parent) {
    setObjectName("projectHome");
    auto* shell = new QHBoxLayout(this);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    auto* navigation = new QFrame(this);
    navigation->setObjectName("homeNavigation");
    navigation->setMinimumWidth(176);
    navigation->setMaximumWidth(196);
    auto* navigation_layout = new QVBoxLayout(navigation);
    navigation_layout->setContentsMargins(12, 16, 12, 28);
    navigation_layout->setSpacing(5);
    auto* product = new QLabel(tr("DEEP LEARNING\nSTUDIO"), navigation);
    product->setProperty("navBrand", true);
    navigation_layout->addWidget(product);
    navigation_layout->addSpacing(12);
    auto* home = nav_button(tr("Home"), navigation, true);
    auto* nav_create = nav_button(tr("New Project"), navigation);
    auto* nav_open = nav_button(tr("Open Project"), navigation);
    auto* recent_anchor = nav_button(tr("Recent Projects"), navigation);
    auto* quick_start = nav_button(tr("Quick Start"), navigation);
    auto* templates = nav_button(tr("Templates"), navigation);
    quick_start->setObjectName("homeQuickStartButton");
    templates->setObjectName("homeTemplatesButton");
    quick_start->setToolTip(tr("Create a semantic-segmentation starter project."));
    templates->setToolTip(tr("Create a project from a reusable workflow template."));
    auto* template_menu = new QMenu(templates);
    auto* segmentation_template = template_menu->addAction(tr("Semantic segmentation"));
    auto* classification_template = template_menu->addAction(tr("Image classification"));
    templates->setMenu(template_menu);
    for (QPushButton* item : {home, nav_create, nav_open, recent_anchor, quick_start, templates})
        navigation_layout->addWidget(item);
    navigation_layout->addStretch();
    shell->addWidget(navigation);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("homeScroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    content->setObjectName("homeContent");
    auto* page = new QVBoxLayout(content);
    page->setContentsMargins(28, 24, 28, 24);
    page->setSpacing(16);

    auto* hero = new QFrame(content);
    hero->setObjectName("homeHero");
    auto* hero_shell = new QHBoxLayout(hero);
    hero_shell->setContentsMargins(24, 20, 24, 20);
    hero_shell->setSpacing(24);
    auto* hero_copy = new QVBoxLayout;
    hero_copy->setSpacing(8);
    auto* eyebrow = new QLabel(tr("DEEP LEARNING STUDIO"), hero);
    eyebrow->setProperty("eyebrow", true);
    auto* title = new QLabel(tr("Build a vision project,\nnot just a graph."), hero);
    title->setProperty("homeTitle", true);
    auto* description = new QLabel(tr("Create a project to organize datasets, annotations, training runs, models and inference results."), hero);
    description->setWordWrap(true);
    description->setMaximumWidth(720);
    description->setProperty("homeDescription", true);
    hero_copy->addWidget(eyebrow);
    hero_copy->addWidget(title);
    hero_copy->addWidget(description);
    auto* actions = new QHBoxLayout;
    actions->setSpacing(12);
    auto* create = new QPushButton(tr("+  New Project"), hero);
    create->setObjectName("homeNewProjectButton");
    create->setProperty("primary", true);
    auto* open = new QPushButton(tr("Open Project"), hero);
    open->setObjectName("homeOpenProjectButton");
    open->setProperty("accentSecondary", true);
    create->setMinimumHeight(36);
    open->setMinimumHeight(36);
    actions->addWidget(create);
    actions->addWidget(open);
    actions->addStretch();
    hero_copy->addSpacing(4);
    hero_copy->addLayout(actions);
    hero_shell->addLayout(hero_copy, 3);

    auto* visual = new QFrame(hero);
    hero_visual_ = visual;
    visual->setObjectName("homeHeroVisual");
    visual->setMinimumWidth(250);
    visual->setMaximumWidth(360);
    auto* visual_layout = new QVBoxLayout(visual);
    visual_layout->setContentsMargins(20, 18, 20, 18);
    visual_layout->setSpacing(8);
    auto* visual_title = new QLabel(tr("VISION WORKFLOW"), visual);
    visual_title->setProperty("eyebrow", true);
    visual_layout->addWidget(visual_title);
    for (const QString& stage : {tr("01  DATASET & LABELS"), tr("02  TRAIN & MONITOR"), tr("03  INFERENCE RESULTS")}) {
        auto* chip = new QLabel(stage, visual);
        chip->setProperty("heroStage", true);
        visual_layout->addWidget(chip);
    }
    visual_layout->addStretch();
    hero_shell->addWidget(visual, 1);
    page->addWidget(hero);

    auto* recent_card = new QFrame(content);
    recent_card->setObjectName("recentProjectsCard");
    auto* recent_card_layout = new QVBoxLayout(recent_card);
    recent_card_layout->setContentsMargins(16, 14, 16, 16);
    recent_card_layout->setSpacing(8);
    auto* recent_title = new QLabel(tr("Recent projects"), recent_card);
    recent_title->setProperty("sectionTitle", true);
    auto* recent_hint = new QLabel(tr("Continue where you left off, or open a project from another location."), recent_card);
    recent_hint->setProperty("muted", true);
    recent_hint->setWordWrap(true);
    recent_card_layout->addWidget(recent_title);
    recent_card_layout->addWidget(recent_hint);
    recent_table_ = new QTableWidget(recent_card);
    recent_table_->setObjectName("recentProjectsTable");
    recent_table_->setColumnCount(4);
    recent_table_->setHorizontalHeaderLabels({tr("Project"), tr("Path"), tr("Last modified"), tr("Actions")});
    recent_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    recent_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    recent_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    recent_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    recent_table_->verticalHeader()->hide();
    recent_table_->setShowGrid(false);
    recent_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    recent_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recent_table_->setMinimumHeight(0);
    recent_empty_ = new QLabel(tr("No recent projects yet. Create a project to begin your first vision workflow."), recent_card);
    recent_empty_->setObjectName("recentProjectsEmpty");
    recent_empty_->setProperty("muted", true);
    recent_empty_->setAlignment(Qt::AlignCenter);
    recent_empty_->setWordWrap(true);
    recent_empty_->setMinimumHeight(100);
    recent_card_layout->addWidget(recent_table_);
    recent_card_layout->addWidget(recent_empty_);
    page->addWidget(recent_card);
    page->addStretch();
    scroll->setWidget(content);
    shell->addWidget(scroll, 1);

    connect(create, &QPushButton::clicked, this, [this] { if (create_project) create_project(); });
    connect(open, &QPushButton::clicked, this, [this] { if (open_project) open_project(); });
    connect(nav_create, &QPushButton::clicked, this, [this] { if (create_project) create_project(); });
    connect(nav_open, &QPushButton::clicked, this, [this] { if (open_project) open_project(); });
    connect(quick_start, &QPushButton::clicked, this, [this] { if (this->quick_start) this->quick_start(); });
    connect(segmentation_template, &QAction::triggered, this, [this] { if (create_template) create_template("sg"); });
    connect(classification_template, &QAction::triggered, this, [this] { if (create_template) create_template("cls"); });
    connect(recent_anchor, &QPushButton::clicked, scroll, [scroll, recent_card] { scroll->ensureWidgetVisible(recent_card, 0, 24); });
    refresh_recent_projects();
}

void ProjectHome::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (hero_visual_) hero_visual_->setVisible(event->size().width() >= 980);
}

void ProjectHome::refresh_recent_projects() {
    const QStringList recent = RecentProjectStore(8).prune().mid(0, 6);
    recent_table_->clearContents();
    recent_table_->setRowCount(recent.size());
    recent_table_->setVisible(!recent.isEmpty());
    recent_empty_->setVisible(recent.isEmpty());
    for (int row = 0; row < recent.size(); ++row) {
        const QString path = recent[row];
        const QFileInfo manifest(path);
        const RecentProjectSummary summary = project_summary(path);
        auto* project_cell = new QWidget(recent_table_);
        auto* project_layout = new QVBoxLayout(project_cell);
        project_layout->setContentsMargins(6, 4, 6, 4);
        project_layout->setSpacing(1);
        auto* name = new QLabel(summary.name.isEmpty() ? tr("Untitled project") : summary.name, project_cell);
        name->setProperty("overviewValue", true);
        auto* badge = new QLabel(summary.task, project_cell);
        badge->setProperty("statusBadge", true);
        project_layout->addWidget(name);
        project_layout->addWidget(badge, 0, Qt::AlignLeft);
        recent_table_->setCellWidget(row, 0, project_cell);

        auto* path_item = new QTableWidgetItem(QDir::toNativeSeparators(manifest.absoluteFilePath()));
        path_item->setToolTip(QDir::toNativeSeparators(manifest.absoluteFilePath()));
        recent_table_->setItem(row, 1, path_item);
        recent_table_->setItem(row, 2, new QTableWidgetItem(manifest.lastModified().toString("yyyy-MM-dd  HH:mm")));

        auto* actions = new QWidget(recent_table_);
        auto* action_layout = new QHBoxLayout(actions);
        action_layout->setContentsMargins(4, 2, 4, 2);
        action_layout->setSpacing(6);
        auto* open = new QPushButton(tr("Open"), actions);
        open->setProperty("accentSecondary", true);
        auto* more = new QToolButton(actions);
        more->setText("...");
        more->setToolTip(tr("More actions"));
        more->setPopupMode(QToolButton::InstantPopup);
        auto* menu = new QMenu(more);
        auto* delete_project_action = menu->addAction(tr("Delete project"));
        more->setMenu(menu);
        connect(open, &QPushButton::clicked, this, [this, path] { if (open_recent) open_recent(path); });
        connect(delete_project_action, &QAction::triggered, this, [this, path] {
            if (delete_project) delete_project(path);
        });
        action_layout->addWidget(open);
        action_layout->addWidget(more);
        recent_table_->setCellWidget(row, 3, actions);
        recent_table_->setRowHeight(row, 58);
    }
    if (!recent.isEmpty())
        recent_table_->setFixedHeight(recent_table_->horizontalHeader()->height() + recent.size() * 58 + 2);
}

} // namespace studio
