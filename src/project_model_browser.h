#pragma once

#include "icon_provider.h"
#include "project_manager.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <vector>

namespace studio {

class ProjectModelBrowser final : public QWidget {
public:
    explicit ProjectModelBrowser(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName("projectModelBrowser");
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(8);

        auto* header = new QHBoxLayout;
        auto* title = new QLabel(tr("TASK MODELS"), this);
        title->setProperty("eyebrow", true);
        header->addWidget(title);
        header->addStretch();
        details_ = new QToolButton(this);
        details_->setObjectName("projectModelDetailsButton");
        details_->setCheckable(true);
        details_->setToolButtonStyle(Qt::ToolButtonIconOnly);
        IconProvider::configure(details_, "eye", tr("Show model details"), 18);
        header->addWidget(details_);
        root->addLayout(header);

        management_menu_ = new QMenu(this);
        import_ = management_menu_->addAction(tr("Import model…"));
        import_->setObjectName("projectModelImportAction");
        export_ = management_menu_->addAction(tr("Export current model…"));
        export_->setObjectName("projectModelExportAction");
        delete_ = new QAction(tr("Delete checked Weights…"), this);
        delete_->setObjectName("projectModelDeleteAction");
        delete_->setIcon(IconProvider::icon("trash"));
        select_all_ = new QCheckBox(tr("Select all"), this);
        select_all_->setObjectName("projectModelSelectAllCheckBox");
        select_all_->setToolTip(tr("Select all models, including every Weight branch and empty model row, for deletion"));
        select_all_->setTristate(true);
        select_all_->setVisible(false);
        header->insertWidget(header->count() - 1, select_all_);
        delete_button_ = new QToolButton(this);
        delete_button_->setObjectName("projectModelDeleteButton");
        delete_button_->setDefaultAction(delete_);
        delete_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
        delete_button_->setAccessibleName(tr("Delete selected models or weight branches"));
        header->insertWidget(header->count() - 1, delete_button_);
        delete_cell_ = management_menu_->addAction(tr("Delete current Weight…"));
        delete_cell_->setObjectName("projectModelDeleteCellAction");
        delete_row_ = management_menu_->addAction(tr("Delete entire Model row…"));
        delete_row_->setObjectName("projectModelDeleteRowAction");
        clear_ = new QAction(tr("Clear all models…"), this);
        clear_->setObjectName("projectModelClearAction");

        table_ = new QTreeWidget(this);
        table_->setObjectName("projectModelBrowserList");
        table_->setColumnCount(5);
        table_->setHeaderLabels({tr("Model"), tr("LibTorch"), tr("ONNX"), tr("OpenVINO"), tr("TensorRT")});
        table_->setRootIsDecorated(false);
        table_->setAlternatingRowColors(true);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        table_->setMinimumWidth(0);
        table_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        table_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        table_->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        table_->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
        root->addWidget(table_, 1);

        detail_ = new QFrame(this);
        detail_->setMinimumWidth(0);
        detail_->setObjectName("projectModelDetailCard");
        detail_->setProperty("sectionPanel", true);
        auto* detail_layout = new QVBoxLayout(detail_);
        detail_layout->setContentsMargins(12, 10, 12, 10);
        detail_layout->setSpacing(6);
        model_name_ = new QLabel(tr("No model selected"), detail_);
        model_name_->setProperty("workspaceTitle", true);
        model_name_->setWordWrap(true);
        model_name_->setMinimumWidth(0);
        model_name_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        detail_layout->addWidget(model_name_);

        auto* fields = new QGridLayout;
        fields->setHorizontalSpacing(12);
        fields->setVerticalSpacing(4);
        add_field(fields, 0, tr("Task"), task_);
        add_field(fields, 1, tr("Network"), network_);
        add_field(fields, 2, tr("Capacity"), capacity_);
        add_field(fields, 3, tr("Input"), input_);
        add_field(fields, 4, tr("Classes"), classes_);
        add_field(fields, 5, tr("Metric"), metric_);
        add_field(fields, 6, tr("Created"), created_);
        add_field(fields, 7, tr("Weights"), weights_);
        fields->setColumnStretch(1, 1);
        detail_layout->addLayout(fields);
        root->addWidget(detail_);

        connect(select_all_, &QCheckBox::clicked, this, [this](bool checked) {
            for (int row = 0; row < table_->topLevelItemCount(); ++row) {
                QTreeWidgetItem* item = table_->topLevelItem(row);
                for (int column = 1; column < table_->columnCount(); ++column) {
                    QWidget* cell = table_->itemWidget(item, column);
                    QCheckBox* target = cell ? cell->findChild<QCheckBox*>("projectModelWeightDeleteCheckBox") : nullptr;
                    if (target && target->isEnabled()) {
                        target->setChecked(checked);
                    }
                }
            }
            all_models_selected_ = checked;
            update_detail();
        });
        connect(table_, &QTreeWidget::itemSelectionChanged, this,
                [this] { update_detail(); });
        connect(table_, &QTreeWidget::currentItemChanged, this,
                [this](QTreeWidgetItem* item, QTreeWidgetItem*) {
            update_detail();
            const int column = table_->currentColumn();
            if (weight_branch_selection_changed && item && column >= 1 && column <= 4) {
                QComboBox* branch = qobject_cast<QComboBox*>(table_->itemWidget(item, column));
                weight_branch_selection_changed(current_model_id(), branch
                    ? branch->currentData().toString()
                    : item->data(column, Qt::UserRole).toString());
                return;
            }
            if (selection_changed) {
                selection_changed(current_model_id());
            }
        });
        connect(table_, &QTreeWidget::itemClicked, this,
                [this](QTreeWidgetItem* item, int column) {
            if (weight_branch_selection_changed && item && column >= 1 && column <= 4) {
                QComboBox* branch = qobject_cast<QComboBox*>(table_->itemWidget(item, column));
                weight_branch_selection_changed(current_model_id(), branch
                    ? branch->currentData().toString()
                    : item->data(column, Qt::UserRole).toString());
                return;
            }
            if (selection_changed) {
                selection_changed(current_model_id());
            }
            if (!weight_selection_mode_ || column < 1 || column > 4) {
                return;
            }
            if (model_check_selection_) {
                set_checked_model_id(current_model_id());
            }
            if (weight_selection_changed) {
                weight_selection_changed(current_model_id(), weight_format_for_column(column));
            }
        });
        connect(table_, &QTreeWidget::itemDoubleClicked, this,
                [this](QTreeWidgetItem*, int) {
            set_detail_visible(true);
        });
        connect(table_->viewport(), &QWidget::customContextMenuRequested, this,
                [this](const QPoint& position) {
            if (!management_actions_enabled_) {
                return;
            }
            const QModelIndex cell = table_->indexAt(position);
            if (!cell.isValid()) {
                return;
            }
            QTreeWidgetItem* item = table_->topLevelItem(cell.row());
            const QString model_id = item->data(0, Qt::UserRole).toString();
            const auto model = std::find_if(models_.begin(), models_.end(), [&model_id](const ProjectModel& value) {
                return QString::fromStdString(value.id) == model_id;
            });
            const bool deletable = model != models_.end() &&
                model->training_status != jocelin::model::ModelTrainingStatus::Running;
            QWidget* content = table_->itemWidget(item, cell.column());
            QComboBox* branch = qobject_cast<QComboBox*>(content);
            if (!branch && content) {
                branch = content->findChild<QComboBox*>("projectModelWeightBranch");
            }
            const QString weight_id = cell.column() > 0
                ? branch ? branch->currentData().toString() : cell.data(Qt::UserRole).toString() : QString{};
            delete_cell_->setData(QStringList{model_id, weight_id});
            delete_cell_->setEnabled(deletable && !weight_id.isEmpty());
            delete_row_->setData(model_id);
            delete_row_->setEnabled(deletable);
            management_menu_->exec(table_->viewport()->mapToGlobal(position));
        });
        connect(details_, &QToolButton::toggled, detail_, &QWidget::setVisible);
        connect(import_, &QAction::triggered, this, [this] {
            if (import_requested) import_requested();
        });
        connect(export_, &QAction::triggered, this, [this] {
            if (export_requested && !current_model_id().isEmpty())
                export_requested(current_model_id());
        });
        connect(delete_, &QAction::triggered, this, [this] {
            update_detail();
            if (all_models_selected_) {
                if (clear_models_requested) {
                    clear_models_requested();
                }
                return;
            }
            ModelWeightSelection weights;
            for (int row = 0; row < table_->topLevelItemCount(); ++row) {
                QTreeWidgetItem* item = table_->topLevelItem(row);
                for (int column = 1; column < table_->columnCount(); ++column) {
                    const QString weight_id = item->data(column, Qt::UserRole + 1).toString();
                    if (!weight_id.isEmpty()) {
                        weights.emplace_back(item->data(0, Qt::UserRole).toString(), weight_id);
                    }
                }
            }
            if (delete_model_weights_requested && !weights.empty()) {
                delete_model_weights_requested(weights);
            }
        });
        connect(delete_cell_, &QAction::triggered, this, [this] {
            const QStringList target = delete_cell_->data().toStringList();
            if (delete_model_weight_requested && target.size() == 2 && !target[1].isEmpty()) {
                delete_model_weight_requested(target[0], target[1]);
            }
        });
        connect(delete_row_, &QAction::triggered, this, [this] {
            const QString model_id = delete_row_->data().toString();
            if (delete_models_requested && !model_id.isEmpty()) {
                delete_models_requested({model_id});
            }
        });
        connect(clear_, &QAction::triggered, this, [this] {
            if (clear_models_requested) clear_models_requested();
        });
        connect(table_, &QTreeWidget::itemChanged, this,
                [this](QTreeWidgetItem* item, int column) {
            if (!model_check_selection_ || !item || column != 0) return;
            if (item->checkState(0) == Qt::Checked) {
                QSignalBlocker blocker(table_);
                for (int row = 0; row < table_->topLevelItemCount(); ++row) {
                    auto* other = table_->topLevelItem(row);
                    if (other != item && other->checkState(0) == Qt::Checked)
                        other->setCheckState(0, Qt::Unchecked);
                }
            }
            if (checked_model_changed)
                checked_model_changed(checked_model_id());
        });
        update_detail();
    }

    void set_models(const std::vector<ProjectModel>& models) {
        const QString selected = current_model_id();
        const QString checked = checked_model_id();
        all_models_selected_ = false;
        models_ = models;
        QSignalBlocker blocker(table_);
        table_->clear();
        for (const auto& model : models_) {
            const QString model_id = QString::fromStdString(model.id);
            auto* item = new QTreeWidgetItem(table_);
            item->setText(0, QString::fromStdString(model.name));
            item->setData(0, Qt::UserRole, model_id);
            item->setToolTip(0, QString::fromStdString(model.asset_directory));
            set_weight_cell(item, 1, model, jocelin::config::WeightType::LibTorch);
            set_weight_cell(item, 2, model, jocelin::config::WeightType::Onnx);
            set_weight_cell(item, 3, model, jocelin::config::WeightType::OpenVino);
            set_weight_cell(item, 4, model, jocelin::config::WeightType::TensorRt);
            if (model_check_selection_) {
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(0, model_id == checked ? Qt::Checked : Qt::Unchecked);
            }
            if (model_id == selected) table_->setCurrentItem(item);
        }
        if (!table_->currentItem() && table_->topLevelItemCount() > 0)
            table_->setCurrentItem(table_->topLevelItem(table_->topLevelItemCount() - 1));
        update_detail();
    }

    const std::vector<ProjectModel>& models() const { return models_; }

    QString current_model_id() const {
        return table_->currentItem()
            ? table_->currentItem()->data(0, Qt::UserRole).toString()
            : QString{};
    }

    const ProjectModel* current_model() const {
        const QString id = current_model_id();
        const auto found = std::find_if(
            models_.begin(), models_.end(),
            [&id](const ProjectModel& model) {
                return QString::fromStdString(model.id) == id;
            });
        return found == models_.end() ? nullptr : &*found;
    }

    void set_current_model_id(const QString& id) {
        if (current_model_id() == id) {
            return;
        }
        for (int row = 0; row < table_->topLevelItemCount(); ++row) {
            auto* item = table_->topLevelItem(row);
            if (item->data(0, Qt::UserRole).toString() == id) {
                const QSignalBlocker blocker(table_);
                table_->setCurrentItem(item);
                update_detail();
                return;
            }
        }
    }

    void clear_selection() {
        table_->clearSelection();
        table_->setCurrentItem(nullptr);
        update_detail();
    }

    QStringList selected_model_ids() const {
        QStringList ids;
        for (const auto* item : table_->selectedItems()) {
            const QString id = item->data(0, Qt::UserRole).toString();
            if (!id.isEmpty() && !ids.contains(id)) ids.push_back(id);
        }
        return ids;
    }

    QString checked_model_id() const {
        for (int row = 0; row < table_->topLevelItemCount(); ++row) {
            const auto* item = table_->topLevelItem(row);
            if (item->checkState(0) == Qt::Checked)
                return item->data(0, Qt::UserRole).toString();
        }
        return {};
    }

    const ProjectModel* checked_model() const {
        const QString id = checked_model_id();
        const auto found = std::find_if(
            models_.begin(), models_.end(),
            [&id](const ProjectModel& model) {
                return QString::fromStdString(model.id) == id;
            });
        return found == models_.end() ? nullptr : &*found;
    }

    void set_checked_model_id(const QString& id) {
        if (!model_check_selection_) return;
        QSignalBlocker blocker(table_);
        for (int row = 0; row < table_->topLevelItemCount(); ++row) {
            auto* item = table_->topLevelItem(row);
            const bool match = item->data(0, Qt::UserRole).toString() == id;
            if (item->flags().testFlag(Qt::ItemIsUserCheckable))
                item->setCheckState(0, match ? Qt::Checked : Qt::Unchecked);
        }
    }

    void set_model_check_selection(bool enabled) {
        model_check_selection_ = enabled;
        QSignalBlocker blocker(table_);
        for (int row = 0; row < table_->topLevelItemCount(); ++row) {
            auto* item = table_->topLevelItem(row);
            auto flags = item->flags();
            if (enabled) {
                item->setFlags(flags | Qt::ItemIsUserCheckable);
                if (item->checkState(0) != Qt::Checked)
                    item->setCheckState(0, Qt::Unchecked);
            } else {
                item->setCheckState(0, Qt::Unchecked);
                item->setFlags(flags & ~Qt::ItemIsUserCheckable);
            }
        }
    }

    void set_management_multi_selection(bool enabled) {
        table_->setSelectionMode(
            enabled ? QAbstractItemView::ExtendedSelection : QAbstractItemView::SingleSelection);
        select_all_->setVisible(enabled && management_actions_enabled_);
        update_detail();
    }

    void set_model_check_eligible(const QString& id, bool eligible) {
        if (!model_check_selection_) return;
        QSignalBlocker blocker(table_);
        for (int row = 0; row < table_->topLevelItemCount(); ++row) {
            auto* item = table_->topLevelItem(row);
            if (item->data(0, Qt::UserRole).toString() != id) continue;
            auto flags = item->flags();
            if (eligible) {
                item->setFlags(flags | Qt::ItemIsUserCheckable);
            } else {
                item->setCheckState(0, Qt::Unchecked);
                item->setFlags(flags & ~Qt::ItemIsUserCheckable);
            }
            return;
        }
    }

    void set_management_actions_enabled(bool enabled) {
        management_actions_enabled_ = enabled;
        delete_button_->setVisible(enabled);
        select_all_->setVisible(enabled && table_->selectionMode() == QAbstractItemView::ExtendedSelection);
        update_detail();
    }

    void set_detail_visible(bool visible) {
        const QSignalBlocker blocker(details_);
        details_->setChecked(visible);
        detail_->setVisible(visible);
    }

    void set_weight_selection_mode(bool enabled) {
        weight_selection_mode_ = enabled;
        table_->setSelectionBehavior(
            enabled ? QAbstractItemView::SelectItems : QAbstractItemView::SelectRows);
        if (weight_branch_selection_changed) {
            table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
    }

    void set_current_weight(
        const QString& model_id,
        jocelin::config::WeightType format,
        const QString& weight_id = {})
    {
        const int column = column_for_weight_format(format);
        for (int row = 0; row < table_->topLevelItemCount(); ++row) {
            auto* item = table_->topLevelItem(row);
            if (item->data(0, Qt::UserRole).toString() != model_id) {
                continue;
            }
            const QSignalBlocker blocker(table_);
            table_->setCurrentItem(item, column);
            QComboBox* branch = qobject_cast<QComboBox*>(table_->itemWidget(item, column));
            if (branch && !weight_id.isEmpty()) {
                const int index = branch->findData(weight_id);
                if (index >= 0) {
                    const QSignalBlocker branch_blocker(branch);
                    branch->setCurrentIndex(index);
                }
            }
            if (model_check_selection_) {
                set_checked_model_id(model_id);
            }
            update_detail();
            return;
        }
    }

    std::function<void(const QString&)> selection_changed;
    std::function<void(const QString&, jocelin::config::WeightType)> weight_selection_changed;
    std::function<void(const QString&, const QString&)> weight_branch_selection_changed;
    std::function<void(const QString&)> checked_model_changed;
    std::function<void()> import_requested;
    std::function<void(const QString&)> export_requested;
    std::function<void(const QStringList&)> delete_models_requested;
    std::function<void(const ModelWeightSelection&)> delete_model_weights_requested;
    std::function<void(const QString&, const QString&)> delete_model_weight_requested;
    std::function<void()> clear_models_requested;

private:
    static int column_for_weight_format(jocelin::config::WeightType format) {
        switch (format) {
        case jocelin::config::WeightType::LibTorch: return 1;
        case jocelin::config::WeightType::Onnx: return 2;
        case jocelin::config::WeightType::OpenVino: return 3;
        case jocelin::config::WeightType::TensorRt: return 4;
        }
        return 1;
    }

    static jocelin::config::WeightType weight_format_for_column(int column) {
        switch (column) {
        case 2: return jocelin::config::WeightType::Onnx;
        case 3: return jocelin::config::WeightType::OpenVino;
        case 4: return jocelin::config::WeightType::TensorRt;
        default: return jocelin::config::WeightType::LibTorch;
        }
    }

    static QString precision_label(jocelin::model::WeightPrecision precision) {
        switch (precision) {
        case jocelin::model::WeightPrecision::Float32: return QStringLiteral("FP32");
        case jocelin::model::WeightPrecision::Float16: return QStringLiteral("FP16");
        case jocelin::model::WeightPrecision::Int8: return QStringLiteral("INT8");
        }
        return QStringLiteral("—");
    }

    void set_weight_cell(
        QTreeWidgetItem* item,
        int column,
        const ProjectModel& model,
        jocelin::config::WeightType format)
    {
        QStringList precisions;
        QStringList branches;
        const std::vector<const jocelin::model::ModelWeight*> weights = jocelin::model::find_weights(model, format);
        for (const jocelin::model::ModelWeight* weight : weights) {
            const QString precision = precision_label(weight->precision);
            if (!precisions.contains(precision)) {
                precisions.push_back(precision);
            }
            branches.push_back(QString::fromStdString(weight->id) + QStringLiteral(" · ") + precision);
        }
        item->setText(column, precisions.isEmpty() ? QStringLiteral("—") : precisions.join(QStringLiteral(" / ")));
        if (!branches.isEmpty()) {
            item->setToolTip(column, branches.join(QStringLiteral("\n")));
        }
        if (weights.empty() || (!weight_branch_selection_changed && !management_actions_enabled_)) {
            return;
        }
        item->setData(column, Qt::UserRole, QString::fromStdString(weights.front()->id));
        QComboBox* branch = nullptr;
        if (weights.size() == 1) {
            item->setText(column, precision_label(weights.front()->precision));
        } else {
            branch = new QComboBox(table_);
            branch->setObjectName("projectModelWeightBranch");
            for (const jocelin::model::ModelWeight* weight : weights) {
                const bool repeated_precision = std::count_if(weights.begin(), weights.end(),
                    [weight](const jocelin::model::ModelWeight* other) {
                        return other->precision == weight->precision;
                    }) > 1;
                const QString id = QString::fromStdString(weight->id);
                branch->addItem(precision_label(weight->precision) +
                    (repeated_precision ? QStringLiteral(" · ") + id : QString{}), id);
            }
            branch->setToolTip(branches.join(QStringLiteral("\n")));
        }
        if (management_actions_enabled_) {
            QWidget* cell = new QWidget(table_);
            cell->setMinimumWidth(0);
            cell->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            QHBoxLayout* cell_layout = new QHBoxLayout(cell);
            cell_layout->setContentsMargins(0, 0, 0, 0);
            cell_layout->setSpacing(3);
            cell_layout->setSizeConstraint(QLayout::SetNoConstraint);
            QCheckBox* target = new QCheckBox(cell);
            target->setObjectName("projectModelWeightDeleteCheckBox");
            target->setToolTip(tr("Select Weight %1 for deletion").arg(QString::fromStdString(weights.front()->id)));
            target->setEnabled(model.training_status != jocelin::model::ModelTrainingStatus::Running);
            cell_layout->addWidget(target);
            if (branch) {
                branch->setMinimumWidth(0);
                branch->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
                cell_layout->addWidget(branch, 1);
            } else {
                QLabel* label = new QLabel(item->text(column), cell);
                label->setMinimumWidth(0);
                label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
                label->setAttribute(Qt::WA_TransparentForMouseEvents);
                cell_layout->addWidget(label, 1);
            }
            table_->setItemWidget(item, column, cell);
            connect(target, &QCheckBox::toggled, this, [this, item, column](bool checked) {
                all_models_selected_ = false;
                item->setData(column, Qt::UserRole + 1,
                    checked ? item->data(column, Qt::UserRole).toString() : QString{});
                update_detail();
            });
            cell->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(cell, &QWidget::customContextMenuRequested, this, [this, cell](const QPoint& position) {
                table_->viewport()->customContextMenuRequested(cell->mapTo(table_->viewport(), position));
            });
        } else if (branch) {
            table_->setItemWidget(item, column, branch);
        }
        if (!branch) {
            return;
        }
        if (management_actions_enabled_) {
            branch->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(branch, &QWidget::customContextMenuRequested, this, [this, branch](const QPoint& position) {
                table_->viewport()->customContextMenuRequested(branch->mapTo(table_->viewport(), position));
            });
        }
        const QString model_id = QString::fromStdString(model.id);
        connect(branch, qOverload<int>(&QComboBox::activated), this,
            [this, item, column, branch, model_id](int index) {
                const QString weight_id = branch->itemData(index).toString();
                {
                    const QSignalBlocker blocker(table_);
                    table_->setCurrentItem(item, column);
                    item->setData(column, Qt::UserRole, weight_id);
                }
                update_detail();
                if (weight_branch_selection_changed) {
                    weight_branch_selection_changed(model_id, weight_id);
                }
            });
    }

    void add_field(QGridLayout* layout, int row, const QString& name, QLabel*& value) {
        auto* label = new QLabel(name, detail_);
        label->setProperty("meta", true);
        value = new QLabel(QStringLiteral("—"), detail_);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setWordWrap(true);
        value->setMinimumWidth(0);
        value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(label, row, 0);
        layout->addWidget(value, row, 1);
    }

    void update_detail() {
        const auto* model = current_model();
        int selected_count = 0;
        for (int row = 0; row < table_->topLevelItemCount(); ++row) {
            QTreeWidgetItem* item = table_->topLevelItem(row);
            for (int column = 1; column < table_->columnCount(); ++column) {
                QWidget* cell = table_->itemWidget(item, column);
                QCheckBox* target = cell ? cell->findChild<QCheckBox*>("projectModelWeightDeleteCheckBox") : nullptr;
                if (!target) {
                    continue;
                }
                QComboBox* branch = cell->findChild<QComboBox*>("projectModelWeightBranch");
                const QString current = branch ? branch->currentData().toString() : item->data(column, Qt::UserRole).toString();
                target->setToolTip(tr("Select Weight %1 for deletion").arg(current));
                item->setData(column, Qt::UserRole, current);
                const QString selected = item->data(column, Qt::UserRole + 1).toString();
                if (!selected.isEmpty() && (selected != current || !target->isEnabled())) {
                    all_models_selected_ = false;
                    item->setData(column, Qt::UserRole + 1, QString{});
                    const QSignalBlocker blocker(target);
                    target->setChecked(false);
                }
                if (target->isEnabled() && !current.isEmpty()) {
                    if (!item->data(column, Qt::UserRole + 1).toString().isEmpty()) {
                        ++selected_count;
                    }
                }
            }
        }
        const QSignalBlocker select_all_blocker(select_all_);
        const bool can_clear_models = !models_.empty() && std::none_of(models_.begin(), models_.end(),
            [](const ProjectModel& value) {
                return value.training_status == jocelin::model::ModelTrainingStatus::Running;
            });
        if (!can_clear_models) {
            all_models_selected_ = false;
        }
        select_all_->setEnabled(can_clear_models);
        select_all_->setCheckState(all_models_selected_ ? Qt::Checked
            : selected_count == 0 ? Qt::Unchecked : Qt::PartiallyChecked);
        export_->setEnabled(model != nullptr);
        delete_->setEnabled(management_actions_enabled_ && (all_models_selected_ || selected_count > 0));
        delete_->setText(all_models_selected_ ? tr("Clear all models…") : tr("Delete checked Weights…"));
        delete_->setToolTip(all_models_selected_ ? tr("Clear all models…")
            : tr("Delete %1 checked Weight branch(es)").arg(selected_count));
        clear_->setEnabled(!models_.empty());
        if (!model) {
            model_name_->setText(tr("No model selected"));
            weights_->setToolTip({});
            for (auto* label : {task_, network_, capacity_, input_, classes_,
                                metric_, created_, weights_})
                label->setText(QStringLiteral("—"));
            return;
        }
        model_name_->setText(QString::fromStdString(model->name));
        task_->setText(QString::fromStdString(jocelin::config::to_string(model->task)));
        network_->setText(QString::number(model->model_index));
        capacity_->setText(QString::fromStdString(jocelin::config::to_string(model->model_scale)));
        input_->setText(tr("%1 × %2").arg(model->image_width).arg(model->image_height));
        classes_->setText(QString::number(model->classes.size()));
        metric_->setText(model->metric_name.empty()
            ? QStringLiteral("—")
            : tr("%1 %2").arg(QString::fromStdString(model->metric_name)).arg(model->metric, 0, 'g', 5));
        created_->setText(QString::fromStdString(model->created_at));
        QStringList branches;
        for (const auto& weight : model->weights) {
            branches.push_back(
                QString::fromStdString(jocelin::config::to_string(weight.format)) +
                QStringLiteral(" / ") + precision_label(weight.precision));
        }
        weights_->setText(branches.isEmpty() ? QStringLiteral("—") : branches.join(QStringLiteral(", ")));
        weights_->setToolTip(QString::fromStdString(model->asset_directory));
    }

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
    std::vector<ProjectModel> models_;
    bool model_check_selection_ = false;
    bool all_models_selected_ = false;
    bool weight_selection_mode_ = false;
    bool management_actions_enabled_ = true;
};

} // namespace studio
