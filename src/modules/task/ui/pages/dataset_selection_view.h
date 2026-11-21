#pragma once

#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace studio {

class TaskWorkspaceService;

struct DatasetConfig;
struct TaskDatasetBinding;

class DatasetSelectionView final : public QWidget {
public:
    explicit DatasetSelectionView(QWidget* parent = nullptr);
    void set_service(TaskWorkspaceService* service);
    void refresh();
    std::function<void()> changed;
    std::function<void(int node_id)> open_dataset_requested;
    std::function<bool(int, DatasetConfig, int, const TaskDatasetBinding*)> dataset_replace_requested;

private:
    void apply_filter();
    TaskWorkspaceService* service_ = nullptr;
    QWidget* annotation_exchange_ = nullptr;
    QLineEdit* search_ = nullptr;
    QComboBox* filter_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* sample_value_ = nullptr;
    QLabel* image_value_ = nullptr;
    QLabel* selected_value_ = nullptr;
    QLabel* quality_value_ = nullptr;
    QLabel* issue_banner_ = nullptr;
    bool rebuilding_ = false;
};

} // namespace studio
