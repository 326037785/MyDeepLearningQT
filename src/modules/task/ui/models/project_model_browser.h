#pragma once

#include "modules/task/application/model_weight_selection.h"

#include <common/model/model_list.h>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <set>
#include <utility>
#include <vector>

class QAction;
class QCheckBox;
class QFrame;
class QGridLayout;
class QLabel;
class QMenu;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace studio {

class ProjectModelBrowser final : public QWidget {
public:
    explicit ProjectModelBrowser(QWidget* parent = nullptr);

    void set_models(const std::vector<jocelin::model::ModelEntry>& models);

    const std::vector<jocelin::model::ModelEntry>& models() const { return models_; }

    QString current_model_id() const;

    const jocelin::model::ModelEntry* current_model() const;

    void set_current_model_id(const QString& id);

    void clear_selection();

    QStringList selected_model_ids() const;

    QString checked_model_id() const;

    const jocelin::model::ModelEntry* checked_model() const;

    void set_checked_model_id(const QString& id);

    void set_model_check_selection(bool enabled);

    void set_branch_check_selection();

    ModelWeightSelection checked_weights() const;

    void set_management_multi_selection(bool enabled);

    void set_model_check_eligible(const QString& id, bool eligible);

    void set_management_actions_enabled(bool enabled);
    void set_import_available(bool available);

    void set_detail_visible(bool visible);

    void set_weight_selection_mode(bool enabled);

    void set_current_weight(
        const QString& model_id,
        jocelin::config::WeightType format,
        const QString& weight_id = {});

    std::function<void(const QString&)> selection_changed;
    std::function<void(const QString&, const QString&)> weight_branch_selection_changed;
    std::function<void(const QString&, const QString&, QWidget*)> convert_weight_requested;
    std::function<void(const QString&)> checked_model_changed;
    std::function<void()> weight_checks_changed;
    std::function<void()> import_requested;
    std::function<void(const QString&)> export_requested;
    std::function<void(const QStringList&)> delete_models_requested;
    std::function<void(const ModelWeightSelection&)> delete_model_weights_requested;
    std::function<void(const QString&, const QString&)> delete_model_weight_requested;
    std::function<void()> clear_models_requested;

private:
    void set_row_weight_checks(const jocelin::model::ModelEntry& model, bool checked);

    void weight_checks_updated();

    static int column_for_weight_format(jocelin::config::WeightType format);

    static QString precision_label(jocelin::model::WeightPrecision precision);

    void set_weight_cell(
        QTreeWidgetItem* item,
        int column,
        const jocelin::model::ModelEntry& model,
        jocelin::config::WeightType format);

    void add_field(QGridLayout* layout, int row, const QString& name, QLabel*& value);

    void update_detail();

    QTreeWidget* table_ = nullptr;
    QFrame* detail_ = nullptr;
    QLabel* model_name_ = nullptr;
    QLabel* task_ = nullptr;
    QLabel* network_ = nullptr;
    QLabel* capacity_ = nullptr;
    QLabel* input_ = nullptr;
    QLabel* classes_ = nullptr;
    QLabel* metric_ = nullptr;
    QLabel* created_ = nullptr;
    QLabel* weights_ = nullptr;
    QToolButton* details_ = nullptr;
    QToolButton* delete_button_ = nullptr;
    QCheckBox* select_all_ = nullptr;
    QMenu* management_menu_ = nullptr;
    QAction* import_ = nullptr;
    QAction* export_ = nullptr;
    QAction* delete_ = nullptr;
    QAction* delete_cell_ = nullptr;
    QAction* delete_row_ = nullptr;
    QAction* clear_ = nullptr;
    std::vector<jocelin::model::ModelEntry> models_;
    std::set<std::pair<QString, QString>> checked_weights_;
    bool branch_check_selection_ = false;
    bool model_check_selection_ = false;
    bool all_models_selected_ = false;
    bool management_actions_enabled_ = true;
};

} // namespace studio
