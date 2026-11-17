#include "modules/project/ui/project_home.h"
#include "modules/project/storage/recent_project_store.h"
#include "modules/task/application/catalog/task_module_catalog.h"

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
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSvgRenderer>
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

QPixmap render_svg(const QString& path, const QSize& size) {
    QPixmap image(size);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    QSvgRenderer(path).render(&painter);
    return image;
}

QPushButton* home_action(const QString& title, const QString& hint,
                         const QString& icon, QWidget* parent) {
    auto* button = new QPushButton(parent);
    button->setProperty("homeAction", true);
    button->setAccessibleName(title);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(92);
    auto* layout = new QHBoxLayout(button);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(16);
    auto* symbol = new QLabel(button);
    symbol->setPixmap(render_svg(":/studio/icons/" + icon + ".svg", QSize(36, 36)));
    symbol->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(symbol);
    auto* text = new QVBoxLayout;
    text->setSpacing(4);
    auto* name = new QLabel(title, button);
    name->setProperty("homeActionTitle", true);
    name->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* detail = new QLabel(hint, button);
    detail->setProperty("homeActionHint", true);
    detail->setWordWrap(true);
    detail->setAttribute(Qt::WA_TransparentForMouseEvents);
    text->addWidget(name);
    text->addWidget(detail);
    layout->addLayout(text, 1);
    auto* arrow = new QLabel(QString::fromUtf8("›"), button);
    arrow->setProperty("homeActionArrow", true);
    arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(arrow);
    return button;
}

} // namespace

ProjectHome::ProjectHome(QWidget* parent) : QWidget(parent) {
    setObjectName("projectHome");
    auto* shell = new QHBoxLayout(this);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("homeScroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    content->setObjectName("homeContent");
    auto* content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(40, 20, 40, 28);
    auto* column = new QWidget(content);
    column->setMaximumWidth(1280);
    auto* content_row = new QHBoxLayout;
    content_row->addStretch(0);
    content_row->addWidget(column, 1);
    content_row->addStretch(0);
    content_layout->addLayout(content_row);
    content_layout->addStretch();
    auto* page = new QVBoxLayout(column);
    page->setContentsMargins(0, 0, 0, 0);
    page->setSpacing(20);

    auto* hero = new QFrame(column);
    hero_ = hero;
    hero->setObjectName("homeHero");
    auto* hero_shell = new QHBoxLayout(hero);
    hero_shell->setContentsMargins(32, 12, 0, 12);
    hero_shell->setSpacing(8);
    hero->setMinimumHeight(260);
    auto* title = new QLabel(hero);
    title->setText(tr("Build a %1").arg("<span style='color:#1764d6'>" + tr("Vision Project") + "</span>"));
    title->setProperty("homeTitle", true);
    title->setWordWrap(true);
    hero_shell->addWidget(title, 1);

    auto* visual = new QLabel(hero);
    hero_visual_ = visual;
    visual->setObjectName("homeHeroVisual");
    visual->setPixmap(render_svg(":/studio/home-vision.svg", QSize(500, 250)));
    visual->setAlignment(Qt::AlignCenter);
    hero_shell->addWidget(visual, 0, Qt::AlignRight | Qt::AlignVCenter);
    page->addWidget(hero);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(16);
    auto* create = home_action(tr("New Project"), tr("Start a new workspace"), "new-project", column);
    create->setObjectName("homeNewProjectButton");
    create->setProperty("primaryCard", true);
    auto* open = home_action(tr("Open Project"), tr("Choose a project file"), "open-project", column);
    open->setObjectName("homeOpenProjectButton");
    auto* browse_recent = home_action(tr("Recent Projects"), tr("Browse recent projects"), "recent-projects", column);
    browse_recent->setObjectName("homeRecentProjectsButton");
    actions->addWidget(create, 1);
    actions->addWidget(open, 1);
    actions->addWidget(browse_recent, 1);
    page->addLayout(actions);

    auto* recent_card = new QFrame(column);
    recent_card->setObjectName("recentProjectsCard");
    auto* recent_card_layout = new QVBoxLayout(recent_card);
    recent_card_layout->setContentsMargins(16, 14, 16, 16);
    recent_card_layout->setSpacing(8);
    auto* recent_title = new QLabel(tr("Recent projects"), recent_card);
    recent_title->setProperty("sectionTitle", true);
    recent_card_layout->addWidget(recent_title);
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
    recent_empty_ = new QLabel(tr("No recent projects"), recent_card);
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
    connect(browse_recent, &QPushButton::clicked, scroll, [scroll, recent_card] {
        scroll->ensureWidgetVisible(recent_card, 0, 24);
    });
    connect(recent_table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        auto* item = recent_table_->item(row, 1);
        if (item && open_recent) open_recent(item->data(Qt::UserRole).toString());
    });
    refresh_recent_projects();
}

void ProjectHome::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const bool wide = event->size().width() >= 1000;
    if (hero_visual_) hero_visual_->setVisible(wide);
    if (hero_) hero_->setMinimumHeight(wide ? 260 : 150);
}

void ProjectHome::refresh_recent_projects() {
    const QStringList recent = RecentProjectStore(8).prune();
    recent_table_->clearContents();
    recent_table_->setRowCount(recent.size());
    recent_table_->setVisible(!recent.isEmpty());
    recent_empty_->setVisible(recent.isEmpty());
    for (int row = 0; row < recent.size(); ++row) {
        const QString path = recent[row];
        const QFileInfo manifest(path);
        const RecentProjectSummary summary = project_summary(path);
        auto* project_cell = new QWidget(recent_table_);
        project_cell->setObjectName("recentProjectCell");
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
        path_item->setData(Qt::UserRole, path);
        recent_table_->setItem(row, 1, path_item);
        recent_table_->setItem(row, 2, new QTableWidgetItem(manifest.lastModified().toString("yyyy-MM-dd  HH:mm")));

        auto* actions = new QWidget(recent_table_);
        actions->setObjectName("recentProjectCell");
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
