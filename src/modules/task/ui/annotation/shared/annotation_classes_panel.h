#pragma once

#include <QFrame>
#include <functional>
#include <string>

class QHBoxLayout;
class QListWidget;
class QToolButton;

namespace studio {

struct AnnotationClassPanelNames {
    const char* list;
    const char* add;
    const char* rename;
    const char* remove;
};

// Owns the common list, toolbar and edit dialogs. Task adapters perform the
// edits and refresh their own target/selection state through these callbacks.
class AnnotationClassesPanel final : public QFrame {
public:
    enum { NameRole = Qt::UserRole + 1 };

    explicit AnnotationClassesPanel(const AnnotationClassPanelNames& names, QWidget* parent = nullptr);
    QListWidget* list() const { return list_; }
    QHBoxLayout* tools() const { return tools_; }
    void set_available(bool available);

    std::function<void(const QString&)> add_class;
    std::function<void(const std::string&, const QString&)> rename_class;
    std::function<QString(const std::string&)> removal_message;
    std::function<void(const std::string&)> remove_class;

private:
    std::string selected_id() const;
    void update_actions();
    void add();
    void rename();
    void remove();

    QListWidget* list_ = nullptr;
    QHBoxLayout* tools_ = nullptr;
    QToolButton* add_ = nullptr;
    QToolButton* rename_ = nullptr;
    QToolButton* remove_ = nullptr;
    bool available_ = false;
};

} // namespace studio
