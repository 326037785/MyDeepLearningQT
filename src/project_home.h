#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QTableWidget;
class QResizeEvent;

namespace studio {

class ProjectHome final : public QWidget {
public:
    explicit ProjectHome(QWidget* parent = nullptr);

    void refresh_recent_projects();
    std::function<void()> create_project;
    std::function<void()> quick_start;
    std::function<void(const QString&)> create_template;
    std::function<void()> open_project;
    std::function<void(const QString&)> open_recent;
    std::function<void(const QString&)> delete_project;

private:
    void resizeEvent(QResizeEvent* event) override;
    QTableWidget* recent_table_ = nullptr;
    QLabel* recent_empty_ = nullptr;
    QWidget* hero_visual_ = nullptr;
};

} // namespace studio
