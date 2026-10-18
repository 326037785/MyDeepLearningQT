#pragma once

#include "icon_provider.h"
#include "project_manager.h"

#include <QAbstractItemView>
#include <QAction>
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
        auto* title = new QLabel(tr("PROJECT MODELS"), this);
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
        delete_ = management_menu_->addAction(tr("Delete selected models…"));
        delete_->setObjectName("projectModelDeleteAction");
        management_menu_->addSeparator();
        clear_ = management_menu_->addAction(tr("Clear all models…"));
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

        connect(table_, &QTreeWidget::currentItemChanged, this,
                [this](QTreeWidgetItem*, QTreeWidgetItem*) {
            update_detail();
            if (selection_changed) selection_changed(current_model_id());
        });
        connect(table_, &QTreeWidget::itemClicked, this,
                [this](QTreeWidgetItem*, int column) {
            if (!weight_selection_mode_ || column < 1 || column > 4) return;
            if (model_check_selection_) set_checked_model_id(current_model_id());
            if (weight_selection_changed)
                weight_selection_changed(current_model_id(), weight_format_for_column(column));
        });
        connect(table_, &QTreeWidget::itemDoubleClicked, this,
                [this](QTreeWidgetItem*, int) {
            set_detail_visible(true);
        });
        connect(table_->viewport(), &QWidget::customContextMenuRequested, this,
                [this](const QPoint& position) {
            if (!management_actions_enabled_) return;
            if (auto* item = table_->itemAt(position); item && !item->isSelected())
                table_->setCurrentItem(item);
            update_detail();
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
            const auto ids = selected_model_ids();
            if (delete_models_requested && !ids.isEmpty())
                delete_models_requested(ids);
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
        for (int row = 0; row < table_->topLevelItemCount(); ++row) {
            auto* item = table_->topLevelItem(row);
            if (item->data(0, Qt::UserRole).toString() == id) {
                table_->setCurrentItem(item);
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
        if (ids.isEmpty() && !current_model_id().isEmpty()) ids.push_back(current_model_id());
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
    }

    void set_current_weight(
        const QString& model_id,
        jocelin::config::WeightType format)
    {
        const int column = column_for_weight_format(format);
        for (int row = 0; row < table_->topLevelItemCount(); ++row) {
            auto* item = table_->topLevelItem(row);
            if (item->data(0, Qt::UserRole).toString() != model_id) continue;
            table_->setCurrentItem(item, column);
            if (model_check_selection_) set_checked_model_id(model_id);
            return;
        }
    }

    std::function<void(const QString&)> selection_changed;
    std::function<void(const QString&, jocelin::config::WeightType)> weight_selection_changed;
    std::function<void(const QString&)> checked_model_changed;
    std::function<void()> import_requested;
    std::function<void(const QString&)> export_requested;
    std::function<void(const QStringList&)> delete_models_requested;
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

    static void set_weight_cell(
        QTreeWidgetItem* item,
        int column,
        const ProjectModel& model,
        jocelin::config::WeightType format)
    {
        QStringList precisions;
        QStringList branches;
        for (const auto& weight : model.weights) {
            if (weight.format != format) continue;
            const QString precision = precision_label(weight.precision);
            if (!precisions.contains(precision)) precisions.push_back(precision);
            branches.push_back(
                QString::fromStdString(weight.id) + QStringLiteral(" · ") + precision);
        }
        item->setText(column, precisions.isEmpty() ? QStringLiteral("—") : precisions.join(QStringLiteral(" / ")));
        if (!branches.isEmpty()) item->setToolTip(column, branches.join(QStringLiteral("\n")));
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
        export_->setEnabled(model != nullptr);
        delete_->setEnabled(model != nullptr);
        clear_->setEnabled(!models_.empty());
        if (!model) {
            model_name_->setText(tr("No model selected"));
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
    QMenu* management_menu_ = nullptr;
    QAction* import_ = nullptr;
    QAction* export_ = nullptr;
    QAction* delete_ = nullptr;
    QAction* clear_ = nullptr;
    std::vector<ProjectModel> models_;
    bool model_check_selection_ = false;
    bool weight_selection_mode_ = false;
    bool management_actions_enabled_ = true;
};

} // namespace studio
