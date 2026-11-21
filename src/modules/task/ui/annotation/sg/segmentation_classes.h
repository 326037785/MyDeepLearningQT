#pragma once

#include <QObject>
#include <functional>
#include <string>

class QListWidget;
class QTabWidget;

namespace studio {

class AnnotationClassesPanel;
class SegmentationAnnotationWidget;
class TaskWorkspaceService;
struct AnnotationClass;

class SegmentationClasses final : public QObject {
public:
    SegmentationClasses(QTabWidget* inspector, SegmentationAnnotationWidget* editor);
    void set_service(TaskWorkspaceService* service);
    void refresh(const std::string& selected);
    QListWidget* list() const;
    const AnnotationClass* selected_class() const;

    std::function<void()> changed;
    std::function<std::string()> choose_class;

private:
    TaskWorkspaceService* service_ = nullptr;
    AnnotationClassesPanel* panel_ = nullptr;
    SegmentationAnnotationWidget* editor_ = nullptr;
};

} // namespace studio
