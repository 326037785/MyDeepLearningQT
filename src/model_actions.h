#pragma once
#include "project_manager.h"
#include <QCoreApplication>
#include <QObject>
#include <QStringList>
#include <functional>
class QWidget;
namespace studio {
class NodeWorkspace;
class ModelActions final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(studio::ModelActions)
public:
    ModelActions(ProjectManager& project, QWidget* parent);
    void mount(NodeWorkspace& workspace);
    void import_model(int node_id);
    void export_model(int node_id, const QString& model_id);
    void delete_models(int node_id, const QStringList& model_ids, const QString& weight_id = {},
                       const ModelWeightSelection& weights = {});
    void clear_models(int node_id);
    std::function<void(const QString&)> log;
    std::function<void(const QString&, int)> status;
    std::function<void()> models_changed;
private:
    ProjectManager& project_;
    QWidget* dialog_parent_;
};
} // namespace studio
