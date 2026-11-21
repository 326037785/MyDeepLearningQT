#pragma once

#include <QWidget>

#include <functional>
#include <string>
#include <vector>

class QLabel;
class QListWidget;
class QTabWidget;
class QToolButton;

namespace studio {

class AnnotationClassesPanel;
class SampleBrowser;
class DetectionCanvas;
class TaskWorkspaceService;

class DetectionAnnotationWidget final : public QWidget {
    Q_OBJECT

public:
    explicit DetectionAnnotationWidget(QTabWidget* inspector, QWidget* parent = nullptr);
    void set_service(TaskWorkspaceService* service);
    void refresh_prediction();
    void refresh_class_display();
    bool confirm_transition();
    std::function<std::string()> choose_class;
    std::function<void(const std::vector<std::string>&, const std::string&)> sample_split_requested;

Q_SIGNALS:
    void changed();

private:
    void load_sample(int index);
    void refresh_sample_status(int index);
    void refresh_classes(const QString& selected = {});
    bool save_current();

    TaskWorkspaceService* service_ = nullptr;
    SampleBrowser* samples_ = nullptr;
    QListWidget* classes_ = nullptr;
    DetectionCanvas* canvas_ = nullptr;
    QLabel* sample_context_ = nullptr;
    QLabel* result_details_ = nullptr;
    AnnotationClassesPanel* class_panel_ = nullptr;
    QToolButton* previous_ = nullptr;
    QToolButton* next_ = nullptr;
    int current_index_ = -1;
    std::string current_sample_id_;
};

} // namespace studio
