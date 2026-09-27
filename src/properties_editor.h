#pragma once

#include "workflow.h"

#include <QWidget>

#include <functional>

class QVBoxLayout;

namespace studio {

class PropertiesEditor final : public QWidget {
public:
    explicit PropertiesEditor(QWidget* parent = nullptr);

    void show_workflow(const QString& project_name, const QString& workflow_name,
                       size_t node_count, size_t connection_count);
    void edit_node(WorkflowGraph* graph, int node_id, std::function<void()> changed);

private:
    void replace_content(QWidget* content);

    QVBoxLayout* layout_ = nullptr;
    QWidget* content_ = nullptr;
};

} // namespace studio
