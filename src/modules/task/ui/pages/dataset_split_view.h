#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QSpinBox;
class QTableWidget;

namespace studio {

class SampleBrowser;
class TaskWorkspaceService;

class DatasetSplitView final : public QWidget {
public:
    explicit DatasetSplitView(QWidget* parent = nullptr);
    void set_service(TaskWorkspaceService* service);
    void refresh();
    void apply_random_split();
    std::function<void()> changed;
    std::function<void(const QString&)> status;

private:
    void refresh_stats();
    TaskWorkspaceService* service_ = nullptr;
    SampleBrowser* thumbnails_ = nullptr;
    QTableWidget* table_ = nullptr;
    QSpinBox* train_percent_ = nullptr;
    QSpinBox* validation_percent_ = nullptr;
    QSpinBox* test_percent_ = nullptr;
    QSpinBox* seed_ = nullptr;
    QLabel* train_value_ = nullptr;
    QLabel* validation_value_ = nullptr;
    QLabel* test_value_ = nullptr;
    QLabel* unassigned_value_ = nullptr;
    QLabel* warning_ = nullptr;
    bool rebuilding_ = false;
};

} // namespace studio
