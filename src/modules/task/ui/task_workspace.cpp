#include "modules/task/ui/task_workspace.h"

#include "modules/task/ui/annotation/cls/classification_annotation.h"
#include "common/ui/icon_provider.h"
#include "modules/task/ui/annotation/sg/segmentation_annotation.h"
#include "modules/task/application/task_dataset_binding.h"
#include "modules/task/ui/pages/dataset_selection_view.h"
#include "modules/task/ui/pages/dataset_split_view.h"
#include "modules/task/ui/pages/training_workspace.h"
#include "modules/task/ui/pages/inference_workspace.h"
#include "modules/task/ui/pages/results_workspace.h"
#include "modules/task/ui/common/project_model_browser.h"
#include "modules/task/application/catalog/task_module_catalog.h"
#include "common/ui/ui_metrics.h"

#include <QComboBox>
#include <QColorDialog>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <stdexcept>

namespace studio {

namespace {

bool has_class_pixels(const TaskSampleBinding& sample, int mask_value) {
    for (const jocelin::geo::Region& region : sample.regions) {
        if (std::any_of(region.vRun.begin(), region.vRun.end(),
                [mask_value](const jocelin::geo::Run& run) { return run.label == mask_value; })) {
            return true;
        }
    }
    return false;
}

} // namespace

TaskWorkspace::TaskWorkspace(QWidget* parent) : QWidget(parent) {
    setObjectName("taskWorkspace");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(ThemeTokens::Space1);

    auto* header = new QFrame(this);
    header->setObjectName("taskWorkspaceHeader");
    auto* header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(ThemeTokens::Space2, 2,
                                      ThemeTokens::Space2, 0);
    header_layout->setSpacing(0);

    auto* identity_row = new QHBoxLayout;
    identity_row->setSpacing(ThemeTokens::Space1);
    auto* back = new QToolButton(header);
    back->setObjectName("taskWorkspaceBackButton");
    IconProvider::configure(back, "previous", tr("Back to Workflow"), 16);
    identity_ = new QLabel(tr("Task"), header);
    identity_->setObjectName("taskWorkspaceIdentity");
    identity_->setProperty("workspaceTitle", true);
    context_ = new QLabel(header);
    context_->setObjectName("taskWorkspaceContext");
    context_->setProperty("workspacePath", true);
    identity_row->addWidget(back);
    identity_row->addWidget(identity_);
    identity_row->addWidget(context_, 1);
    header_layout->addLayout(identity_row);

    auto* navigation = new QHBoxLayout;
    navigation->setContentsMargins(ThemeTokens::Space2, 0, ThemeTokens::Space2, 0);
    navigation->setSpacing(0);
    const QStringList names = {
        tr("1  Data"), tr("2  Annotation"), tr("3  Split"),
        tr("4  Training"), tr("5  Inference"), tr("6  Results")};
    for (int index = 0; index < names.size(); ++index) {
        auto* button = new QPushButton(names[index], header);
        button->setObjectName(QString("workspaceStep%1Button").arg(index + 1));
        button->setProperty("stepNav", true);
        button->setProperty("compact", true);
        navigation->addWidget(button, 1);
        step_buttons_[static_cast<size_t>(index)] = button;
        connect(button, &QPushButton::clicked, this, [this, index] {
            activate_step(static_cast<Step>(index));
        });
    }
    header_layout->addLayout(navigation);
    root->addWidget(header);

    pages_ = new QStackedWidget(this);
    pages_->setObjectName("taskStepPages");
    data_ = new DatasetSelectionView(pages_);
    pages_->addWidget(data_);
    pages_->addWidget(build_annotation_page());
    split_ = new DatasetSplitView(pages_);
    pages_->addWidget(split_);
    training_ = new TrainingWorkspace(pages_);
    pages_->addWidget(training_);
    inference_ = new InferenceWorkspace(pages_);
    pages_->addWidget(inference_);
    results_ = new ResultsWorkspace(pages_);
    pages_->addWidget(results_);
    root->addWidget(pages_, 1);

    connect(back, &QToolButton::clicked, this,
            [this] { if (back_requested) back_requested(); });
    data_->changed = [this] {
        annotation_binding_dirty_ = true;
        domain_changed();
    };
    data_->open_dataset_requested = [this](int node_id) {
        if (dataset_requested) dataset_requested(node_id);
    };
    data_->dataset_replace_requested = [this](int source, DatasetConfig dataset, int owner, const TaskDatasetBinding* imported) {
        if (!dataset_replace_requested) {
            throw std::runtime_error("Dataset replacement handler unavailable");
        }
        const bool replaced = dataset_replace_requested(source, std::move(dataset), owner, imported);
        if (replaced && imported) {
            // The accepted import has replaced the Task labels and targets.
            segmentation_->canvas()->clear();
        }
        return replaced;
    };
    split_->changed = [this] { domain_changed(); };
    split_->status = [this](const QString& text) { if (status) status(text); };
    training_->changed = [this] { domain_changed(); };
    training_->start_requested = [this](const TrainingLaunchRequest& request) {
        if (train_requested) train_requested(node_id_, request);
    };
    training_->stop_requested = [this] {
        if (stop_training_requested) stop_training_requested();
    };
    training_->pause_requested = [this] {
        if (pause_training_requested) pause_training_requested();
    };
    training_->run_details_requested = [this](const QString& run_id) {
        if (run_details_requested) run_details_requested(run_id);
    };
    training_->import_model_requested = [this] {
        if (import_model_requested) import_model_requested(node_id_);
    };
    training_->export_model_requested = [this](const QString& model_id) {
        if (export_model_requested) export_model_requested(node_id_, model_id);
    };
    training_->delete_models_requested = [this](const QStringList& model_ids) {
        if (delete_models_requested) delete_models_requested(node_id_, model_ids);
    };
    training_->delete_model_weights_requested = [this](const ModelWeightSelection& weights) {
        if (delete_model_weights_requested) {
            delete_model_weights_requested(node_id_, weights);
        }
    };
    training_->delete_model_weight_requested = [this](const QString& model_id, const QString& weight_id) {
        if (delete_model_weight_requested) {
            delete_model_weight_requested(node_id_, model_id, weight_id);
        }
    };
    training_->clear_models_requested = [this] {
        if (clear_models_requested) clear_models_requested(node_id_);
    };
    inference_->import_model_requested = [this] {
        if (import_model_requested) import_model_requested(node_id_);
    };
    inference_->export_model_requested = [this](const QString& model_id) {
        if (export_model_requested) export_model_requested(node_id_, model_id);
    };
    inference_->delete_models_requested = [this](const QStringList& model_ids) {
        if (delete_models_requested) delete_models_requested(node_id_, model_ids);
    };
    inference_->delete_model_weights_requested = [this](const ModelWeightSelection& weights) {
        if (delete_model_weights_requested) {
            delete_model_weights_requested(node_id_, weights);
        }
    };
    inference_->delete_model_weight_requested = [this](const QString& model_id, const QString& weight_id) {
        if (delete_model_weight_requested) {
            delete_model_weight_requested(node_id_, model_id, weight_id);
        }
    };
    inference_->clear_models_requested = [this] {
        if (clear_models_requested) clear_models_requested(node_id_);
    };
    inference_->changed = [this] { domain_changed(); };
    inference_->start_requested = [this](const InferenceLaunchRequest& request) {
        if (infer_requested) infer_requested(node_id_, request);
    };
    inference_->convert_weight_requested = [this](const QString& model_id, const QString& weight_id,
                                                    jocelin::config::WeightType target) {
        if (convert_weight_requested) {
            convert_weight_requested(node_id_, model_id, weight_id, target);
        }
    };
    UiMetrics::apply_semantic_metrics(this);
    activate_step(Step::Data);
}

QWidget* TaskWorkspace::build_annotation_page() {
    auto* page = new QWidget(pages_);
    page->setObjectName("taskAnnotationPage");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                               ThemeTokens::Space2, ThemeTokens::Space2);
    layout->setSpacing(0);
    segmentation_inspector_ = new QTabWidget(page);
    classification_inspector_ = new QTabWidget(page);
    segmentation_ = new SegmentationAnnotationWidget(page, segmentation_inspector_);
    classification_ = new ClassificationAnnotationWidget(page, classification_inspector_);
    build_segmentation_classes();
    QListWidget* classification_classes = classification_inspector_->findChild<QListWidget*>("classificationClassList");
    for (QListWidget* list : {segmentation_classes_, classification_classes}) {
        const std::shared_ptr<bool> current_before_press = std::make_shared<bool>(true);
        const std::shared_ptr<bool> pressed_current_class = std::make_shared<bool>(false);
        connect(list, &QListWidget::currentItemChanged, this,
                [current_before_press](QListWidgetItem* current, QListWidgetItem*) {
            // Mouse presses change the current item before Qt updates the selection.
            *current_before_press = !current || current->isSelected();
        });
        connect(list, &QListWidget::itemPressed, this,
                [current_before_press, pressed_current_class](QListWidgetItem*) {
            *pressed_current_class = *current_before_press;
            *current_before_press = true;
        });
        const auto class_clicked = [this, list, pressed_current_class](QListWidgetItem* item, bool double_click) {
            if (!service_ || !item) {
                return;
            }
            QStyleOptionViewItem option;
            option.initFrom(list);
            option.rect = list->visualItemRect(item);
            option.decorationSize = list->iconSize();
            option.features = QStyleOptionViewItem::HasDecoration | QStyleOptionViewItem::HasDisplay;
            option.text = item->text();
            if (item->flags().testFlag(Qt::ItemIsUserCheckable)) {
                option.features |= QStyleOptionViewItem::HasCheckIndicator;
                option.checkState = item->checkState();
            }
            const QRect color_rect = list->style()->subElementRect(
                QStyle::SE_ItemViewItemDecoration, &option, list);
            const QPoint clicked_position = list->viewport()->mapFromGlobal(QCursor::pos());
            if (!color_rect.contains(clicked_position)) {
                const QRect check_rect = list->style()->subElementRect(
                    QStyle::SE_ItemViewItemCheckIndicator, &option, list);
                const bool selectable_class = list != segmentation_classes_ || list->row(item) > 0;
                if ((double_click || *pressed_current_class) && selectable_class &&
                    item == list->currentItem() &&
                    !check_rect.contains(clicked_position)) {
                    list->setCurrentRow(list == segmentation_classes_ ? 0 : -1);
                }
                return;
            }
            if (double_click) {
                return;
            }
            const std::string id = item->data(Qt::UserRole).toString().toStdString();
            const AnnotationClass* schema = find_annotation_class(service_->binding(), id);
            if (!schema) {
                return;
            }
            const QColor previous(QString::fromStdString(schema->color));
            const QColor selected = QColorDialog::getColor(previous, this, tr("Choose class color"),
                                                            QColorDialog::DontUseNativeDialog);
            if (!selected.isValid() || selected == previous || !service_) {
                return;
            }
            std::vector<AnnotationClass>& classes = service_->binding().classes;
            const auto found = std::find_if(classes.begin(), classes.end(),
                [&id](const AnnotationClass& value) { return value.id == id; });
            if (found == classes.end()) {
                return;
            }
            found->color = selected.name().toStdString();
            if (list == segmentation_classes_) {
                QTimer::singleShot(0, this, [this, id] { refresh_segmentation_classes(id); });
            } else {
                QPixmap chip(14, 14);
                chip.fill(selected);
                item->setIcon(QIcon(chip));
            }
            refresh_inference_result();
            if (inference_) {
                inference_->synchronize_result_selection();
            }
            domain_changed();
        };
        connect(list, &QListWidget::itemClicked, this,
                [class_clicked](QListWidgetItem* item) { class_clicked(item, false); });
        connect(list, &QListWidget::itemDoubleClicked, this,
                [class_clicked](QListWidgetItem* item) { class_clicked(item, true); });
    }
    annotation_models_tab_ = new QWidget(page);
    QVBoxLayout* models_layout = new QVBoxLayout(annotation_models_tab_);
    models_layout->setContentsMargins(8, 8, 8, 8);
    annotation_models_ = new ProjectModelBrowser(annotation_models_tab_);
    annotation_models_->setObjectName("annotationProjectModelBrowser");
    annotation_models_->set_management_actions_enabled(false);
    annotation_models_->set_detail_visible(false);
    annotation_models_->selection_changed = [this](const QString& model_id) {
        if (service_) {
            service_->select_inference_model(model_id);
        }
    };
    annotation_models_->weight_branch_selection_changed = [this](const QString& model_id, const QString& weight_id) {
        if (!service_) {
            return;
        }
        if (weight_id.isEmpty()) {
            service_->select_inference_model(model_id);
        } else {
            service_->select_inference_result(model_id, weight_id);
        }
    };
    annotation_models_->set_weight_selection_mode(true);
    models_layout->addWidget(annotation_models_, 1);
    annotation_weight_branch_ = new QComboBox(annotation_models_tab_);
    annotation_weight_branch_->setObjectName("annotationWeightBranch");
    annotation_weight_branch_->setAccessibleName(tr("Weight branch"));
    annotation_weight_branch_->setPlaceholderText(tr("No model selected"));
    annotation_weight_branch_->setEnabled(false);
    models_layout->addWidget(annotation_weight_branch_);
    connect(annotation_weight_branch_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (service_ && index >= 0) {
            service_->select_inference_result(service_->selected_model_id(),
                annotation_weight_branch_->itemData(index).toString());
        }
    });
    segmentation_inspector_->insertTab(1, annotation_models_tab_, tr("Models"));
    segmentation_->hide();
    classification_->hide();
    layout->addWidget(segmentation_, 1);
    layout->addWidget(classification_, 1);

    segmentation_->status = [this](const QString& text) {
        if (status) status(text);
    };
    segmentation_->save_mask = [this](const std::string& sample_id, const QImage& mask) {
        if (!service_) {
            return false;
        }
        try {
            service_->set_segmentation_mask(sample_id, mask);
            segmentation_->refresh_samples(service_->binding().samples, {});
            domain_changed();
            return true;
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Mask metadata failed"),
                                 QString::fromUtf8(error.what()));
            return false;
        }
    };
    auto assign_sample_split = [this](
        const std::vector<std::string>& sample_ids, const std::string& split) {
        if (!service_) return;
        for (const auto& node_id : sample_ids) service_->set_sample_split(node_id, split);
        refresh_annotation();
        if (split_) split_->refresh();
        domain_changed();
    };
    segmentation_->samples_empty_requested = [this](const std::vector<std::string>& ids) {
        if (!service_) return;
        try {
            for (const auto& node_id : ids) service_->set_segmentation_sample_empty(node_id);
            segmentation_->refresh_samples(service_->binding().samples, ids);
            domain_changed();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Cannot set empty target"), QString::fromUtf8(error.what()));
        }
    };
    segmentation_->sample_split_requested = assign_sample_split;
    classification_->sample_split_requested = assign_sample_split;
    classification_->class_assigned = [this](const QString& image, int class_index) {
        if (!service_ || class_index < 0 ||
            class_index >= static_cast<int>(service_->binding().classes.size())) {
            return;
        }
        const QString absolute = QFileInfo(image).absoluteFilePath();
        for (const auto& sample : service_->binding().samples) {
            const bool matches = std::any_of(
                sample.image_paths.begin(), sample.image_paths.end(),
                [&absolute](const std::string& path) {
                    return QFileInfo(QString::fromStdString(path)).absoluteFilePath() == absolute;
                });
            if (!matches) continue;
            service_->set_class_for_sample(sample.sample_id,
                service_->binding().classes[static_cast<size_t>(class_index)].id);
            domain_changed();
            break;
        }
    };
    classification_->class_add_requested = [this](const QString& name) {
        if (!service_) return;
        try {
            service_->add_class(name.toStdString());
            refresh_annotation();
            domain_changed();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Cannot add class"),
                                 QString::fromUtf8(error.what()));
        }
    };
    return page;
}

void TaskWorkspace::build_segmentation_classes() {
    QWidget* panel = new QWidget(segmentation_inspector_);
    panel->setObjectName("annotationClassesPanel");
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    segmentation_classes_ = new QListWidget(panel);
    segmentation_classes_->setObjectName("segmentationClassList");
    segmentation_classes_->setAccessibleName(tr("Annotation classes"));
    segmentation_classes_->setIconSize(QSize(14, 14));
    layout->addWidget(segmentation_classes_, 1);

    QHBoxLayout* actions = new QHBoxLayout;
    const auto button = [panel, actions](const char* name, const char* icon, const QString& text) {
        QToolButton* tool = new QToolButton(panel);
        tool->setObjectName(name);
        IconProvider::configure(tool, icon, text);
        actions->addWidget(tool);
        return tool;
    };
    QToolButton* add = button("segmentationAddClassButton", "add", tr("Add class"));
    QToolButton* rename = button("segmentationRenameClassButton", "rename", tr("Rename class"));
    QToolButton* remove = button("segmentationDeleteClassButton", "trash", tr("Delete class"));
    QToolButton* lock = button("segmentationLockClassButton", "lock", tr("Lock or unlock the selected class"));
    QToolButton* up = button("segmentationMoveClassUpButton", "up", tr("Move class up"));
    QToolButton* down = button("segmentationMoveClassDownButton", "down", tr("Move class down"));
    lock->setCheckable(true);
    layout->addLayout(actions);
    segmentation_inspector_->insertTab(0, panel, tr("Classes"));

    MaskCanvas* canvas = segmentation_->canvas();
    connect(segmentation_classes_, &QListWidget::currentItemChanged, this,
            [this, canvas, rename, remove, lock, up, down] {
        const AnnotationClass* selected = selected_segmentation_class();
        const bool locked = selected && selected->locked;
        canvas->set_class_value(selected ? selected->mask_value : 0);
        canvas->set_editing_enabled(!locked);
        const QSignalBlocker blocker(lock);
        lock->setChecked(locked);
        lock->setEnabled(selected != nullptr);
        rename->setEnabled(selected != nullptr);
        remove->setEnabled(selected != nullptr);
        up->setEnabled(segmentation_classes_->currentRow() > 1);
        down->setEnabled(selected && segmentation_classes_->currentRow() + 1 < segmentation_classes_->count());
    });
    canvas->class_picked = [this](int value) {
        const AnnotationClass* schema = service_
            ? find_annotation_class_by_mask(service_->binding(), value) : nullptr;
        if (schema) {
            const QString id = QString::fromStdString(schema->id);
            for (int row = 1; row < segmentation_classes_->count(); ++row) {
                if (segmentation_classes_->item(row)->data(Qt::UserRole).toString() == id) {
                    segmentation_classes_->setCurrentRow(row);
                    return;
                }
            }
        }
    };
    canvas->choose_class = [this] { return choose_segmentation_class(); };
    connect(add, &QToolButton::clicked, this, [this] {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("Add class"), tr("Class name"),
            QLineEdit::Normal, {}, &accepted).trimmed();
        if (accepted && !name.isEmpty()) {
            add_segmentation_class(name);
        }
    });
    connect(rename, &QToolButton::clicked, this, [this] {
        const AnnotationClass* schema = selected_segmentation_class();
        if (!schema) {
            return;
        }
        const std::string id = schema->id;
        const QString name = QString::fromStdString(schema->name);
        bool accepted = false;
        const QString renamed = QInputDialog::getText(this, tr("Rename class"), tr("Class name"),
            QLineEdit::Normal, name, &accepted).trimmed();
        if (!accepted || renamed.isEmpty() || renamed == name) {
            return;
        }
        try {
            service_->rename_class(id, renamed.toStdString());
            refresh_segmentation_classes(id);
            domain_changed();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Cannot rename class"), QString::fromUtf8(error.what()));
        }
    });
    connect(remove, &QToolButton::clicked, this, [this, canvas] {
        const AnnotationClass* schema = selected_segmentation_class();
        if (!schema) {
            return;
        }
        const std::string id = schema->id;
        const int value = schema->mask_value;
        const auto& samples = service_->binding().samples;
        const int affected = static_cast<int>(std::count_if(samples.begin(), samples.end(),
            [value](const TaskSampleBinding& sample) { return has_class_pixels(sample, value); }));
        if (affected > 0) {
            const QString message = tr("Delete '%1' and its annotations in %2 sample(s)?")
                .arg(QString::fromStdString(schema->name)).arg(affected);
            if (QMessageBox::question(this, tr("Delete class"), message,
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
                return;
            }
        }
        service_->remove_class(id);
        canvas->remove_class_pixels(value);
        refresh_segmentation_classes({});
        segmentation_->refresh_samples(samples, {});
        data_->refresh();
        domain_changed();
    });
    const auto move = [this](int direction) {
        if (const AnnotationClass* schema = selected_segmentation_class()) {
            const std::string id = schema->id;
            service_->move_class(id, direction);
            refresh_segmentation_classes(id);
            domain_changed();
        }
    };
    connect(up, &QToolButton::clicked, this, [move] { move(-1); });
    connect(down, &QToolButton::clicked, this, [move] { move(1); });
    connect(lock, &QToolButton::toggled, this, [this](bool locked) {
        if (const AnnotationClass* schema = selected_segmentation_class()) {
            const std::string id = schema->id;
            service_->set_class_locked(id, locked);
            refresh_segmentation_classes(id);
            domain_changed();
        }
    });
    connect(segmentation_classes_, &QListWidget::itemChanged, this,
            [this](QListWidgetItem* item) {
        const std::string id = item->data(Qt::UserRole).toString().toStdString();
        if (id.empty() || !service_) {
            return;
        }
        const AnnotationClass* selected = selected_segmentation_class();
        const std::string selected_id = selected ? selected->id : std::string{};
        service_->set_class_visible(id, item->checkState() == Qt::Checked);
        refresh_segmentation_classes(selected_id);
        domain_changed();
    });
    refresh_segmentation_classes({});
}

void TaskWorkspace::refresh_segmentation_classes(const std::string& class_id) {
    QHash<int, QColor> colors;
    QSet<int> hidden;
    int row = 0;
    {
        const QSignalBlocker blocker(segmentation_classes_);
        segmentation_classes_->clear();
        new QListWidgetItem(tr("No class selected"), segmentation_classes_);
        if (service_ && service_->task().task == "sg") {
            for (const AnnotationClass& schema : service_->binding().classes) {
                QColor color(QString::fromStdString(schema.color));
                if (!color.isValid()) {
                    color = QColor(QString::fromStdString(default_class_color(schema.mask_value)));
                }
                colors.insert(schema.mask_value, color);
                if (!schema.visible) {
                    hidden.insert(schema.mask_value);
                }
                QPixmap chip(14, 14);
                chip.fill(color);
                const QString id = QString::fromStdString(schema.id);
                const QString name = QString::fromStdString(schema.name);
                QListWidgetItem* item = new QListWidgetItem(QIcon(chip),
                    schema.locked ? tr("%1 · locked").arg(name) : name, segmentation_classes_);
                item->setData(Qt::UserRole, id);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(schema.visible ? Qt::Checked : Qt::Unchecked);
                item->setToolTip(tr("Stable class ID: %1\nMask value: %2").arg(id).arg(schema.mask_value));
                if (schema.id == class_id) {
                    row = segmentation_classes_->row(item);
                }
            }
        }
    }
    segmentation_classes_->setCurrentRow(row);
    segmentation_->canvas()->set_class_display(colors, hidden);
}

const AnnotationClass* TaskWorkspace::selected_segmentation_class() const {
    const QListWidgetItem* item = segmentation_classes_->currentItem();
    return service_ && item
        ? find_annotation_class(service_->binding(), item->data(Qt::UserRole).toString().toStdString())
        : nullptr;
}

std::string TaskWorkspace::add_segmentation_class(const QString& name) {
    try {
        const std::string id = service_->add_class(name.toStdString());
        const AnnotationClass* selected = selected_segmentation_class();
        refresh_segmentation_classes(selected ? selected->id : std::string{});
        domain_changed();
        return id;
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Cannot add class"), QString::fromUtf8(error.what()));
        return {};
    }
}

int TaskWorkspace::choose_segmentation_class() {
    if (!service_) {
        return 0;
    }
    QDialog board(this);
    board.setObjectName("annotationClassBoard");
    board.setWindowTitle(tr("Choose annotation class"));
    QVBoxLayout* layout = new QVBoxLayout(&board);
    QComboBox* existing = new QComboBox(&board);
    existing->setObjectName("annotationExistingClass");
    existing->setAccessibleName(tr("Existing class"));
    for (const AnnotationClass& schema : service_->binding().classes) {
        if (!schema.locked) {
            existing->addItem(QString::fromStdString(schema.name), QString::fromStdString(schema.id));
        }
    }
    QLineEdit* name = new QLineEdit(&board);
    name->setObjectName("annotationNewClassName");
    name->setPlaceholderText(tr("Class name"));
    name->setAccessibleName(tr("New class name"));
    QToolButton* add = new QToolButton(&board);
    add->setObjectName("annotationAddClassButton");
    IconProvider::configure(add, "add", tr("Add class"));
    add->setFocusPolicy(Qt::StrongFocus);
    add->setCheckable(true);
    const bool has_existing = existing->count() > 0;
    add->setVisible(has_existing);
    add->setChecked(!has_existing);
    QHBoxLayout* input = new QHBoxLayout;
    input->addWidget(existing, 1);
    input->addWidget(name, 1);
    input->addWidget(add);
    layout->addLayout(input);
    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &board);
    layout->addWidget(buttons);
    QPushButton* confirm = buttons->button(QDialogButtonBox::Ok);
    const auto update_confirm = [add, name, confirm] {
        confirm->setEnabled(!add->isChecked() || !name->text().trimmed().isEmpty());
    };
    const auto show_input = [existing, name, update_confirm](bool creating) {
        existing->setVisible(!creating);
        name->setVisible(creating);
        if (creating) {
            name->setFocus();
        } else {
            existing->setFocus();
        }
        update_confirm();
    };
    connect(add, &QToolButton::toggled, &board, show_input);
    connect(name, &QLineEdit::textChanged, &board, update_confirm);
    show_input(add->isChecked());
    int value = 0;
    connect(buttons, &QDialogButtonBox::accepted, &board,
            [this, &board, &value, add, existing, name] {
        const std::string id = add->isChecked()
            ? add_segmentation_class(name->text().trimmed())
            : existing->currentData().toString().toStdString();
        const AnnotationClass* schema = find_annotation_class(service_->binding(), id);
        if (schema && !schema->locked) {
            value = schema->mask_value;
            board.accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &board, &QDialog::reject);
    board.exec();
    return value;
}

void TaskWorkspace::bind(
    WorkflowGraph* graph, int node_id)
{
    graph_ = graph;
    node_id_ = node_id;
    segmentation_->canvas()->clear();
    {
        const QSignalBlocker blocker(segmentation_classes_);
        segmentation_classes_->setCurrentRow(0);
    }
    training_->set_service(nullptr);
    inference_->set_service(nullptr);
    results_->set_service(nullptr);
    service_.reset();
    if (!graph_ || graph_->nodes().count(node_id_) == 0 ||
        graph_->node(node_id_).kind != NodeKind::TaskConfig) {
        clear();
        return;
    }
    try {
        service_ = std::make_unique<TaskWorkspaceService>(*graph_, node_id_);
        service_->inference_selection_changed = [this] {
            inference_->synchronize_result_selection();
            results_->refresh();
            refresh_inference_result();
        };
        service_->synchronize();
        refresh();
        activate_step(Step::Data);
    } catch (const std::exception& error) {
        context_->setText(QString::fromUtf8(error.what()));
        refresh_step_availability();
    }
}

void TaskWorkspace::refresh_models() {
    const auto models = service_ ? service_->models() : std::vector<jocelin::model::ModelEntry>{};
    annotation_models_->set_models(models);
    if (training_) training_->set_models(models);
    if (inference_) inference_->set_models(models);
    if (results_) results_->set_models(models);
    if (service_) {
        const auto* selected = service_->find_model(service_->selected_model_id());
        if (!selected) {
            service_->select_inference_model(models.empty() ? QString{} : QString::fromStdString(models.front().id));
        } else if (!jocelin::model::find_weight(*selected, service_->selected_weight_id().toStdString())) {
            service_->select_inference_model(service_->selected_model_id());
        }
    }
    if (inference_) inference_->synchronize_result_selection();
    if (results_) results_->refresh();
    refresh_inference_result();
}

void TaskWorkspace::clear() {
    service_.reset();
    graph_ = nullptr;
    node_id_ = 0;
    annotation_binding_dirty_ = true;
    segmentation_->canvas()->clear();
    refresh_segmentation_classes({});
    identity_->setText(tr("Task"));
    context_->clear();
    data_->set_service(nullptr);
    split_->set_service(nullptr);
    training_->set_service(nullptr);
    training_->set_models({});
    inference_->set_service(nullptr);
    inference_->set_models({});
    results_->set_service(nullptr);
    results_->set_models({});
    annotation_models_->set_models({});
    refresh_inference_result();
    refresh_step_availability();
}

void TaskWorkspace::refresh_annotation() {
    if (!service_) {
        segmentation_->set_task_dataset({});
        classification_->set_dataset({}, {}, {});
        segmentation_->set_inference_result(nullptr);
        classification_->set_inference_result(nullptr);
        return;
    }
    const TaskNodeConfig& task = service_->task();
    const bool segmentation = task.task == "sg";
    QTabWidget* inspector = segmentation ? segmentation_inspector_ : classification_inspector_;
    QTabWidget* other_inspector = segmentation ? classification_inspector_ : segmentation_inspector_;
    const int previous_tab = other_inspector->indexOf(annotation_models_tab_);
    if (previous_tab >= 0) {
        other_inspector->removeTab(previous_tab);
    }
    if (inspector->indexOf(annotation_models_tab_) < 0) {
        inspector->insertTab(1, annotation_models_tab_, tr("Models"));
    }
    segmentation_->setVisible(segmentation);
    classification_->setVisible(!segmentation);
    if (segmentation) {
        const AnnotationClass* selected = selected_segmentation_class();
        refresh_segmentation_classes(selected ? selected->id : std::string{});
        if (service_->source_node_id() != 0) {
            segmentation_->set_task_dataset(service_->binding().samples);
        } else {
            segmentation_->set_task_dataset({});
        }
    } else if (service_->source_node_id() == 0) {
        classification_->set_dataset({}, {}, {});
    } else {
        QVector<int> labels;
        QStringList class_names;
        const std::vector<AnnotationClass>& classes = service_->binding().classes;
        for (const AnnotationClass& value : classes) {
            class_names.push_back(QString::fromStdString(value.name));
        }
        for (const auto& sample : service_->binding().samples) {
            int label = -1;
            for (int index = 0; index < static_cast<int>(classes.size()); ++index) {
                if (classes[static_cast<size_t>(index)].id == sample.class_id) {
                    label = index;
                    break;
                }
            }
            labels.push_back(label);
        }
        classification_->set_dataset(
            service_->binding().samples,
            class_names,
            labels,
            classes);
    }
    annotation_binding_dirty_ = false;
    refresh_inference_result();
}

void TaskWorkspace::refresh_inference_result() {
    const jocelin::model::ModelEntry* model = service_ ? service_->find_model(service_->selected_model_id()) : nullptr;
    const QSignalBlocker branch_blocker(annotation_weight_branch_);
    annotation_weight_branch_->clear();
    annotation_weight_branch_->setPlaceholderText(model ? tr("No weight selected") : tr("No model selected"));
    if (model) {
        for (const jocelin::model::ModelWeight& branch : model->weights) {
            const QString id = QString::fromStdString(branch.id);
            annotation_weight_branch_->addItem(id, id);
        }
        const QString weight_id = service_->selected_weight_id();
        annotation_weight_branch_->setCurrentIndex(annotation_weight_branch_->findData(weight_id));
        const jocelin::model::ModelWeight* weight = jocelin::model::find_weight(*model, weight_id.toStdString());
        if (weight) {
            annotation_models_->set_current_weight(service_->selected_model_id(), weight->format, weight_id);
        } else {
            annotation_models_->set_current_model_id(service_->selected_model_id());
        }
    }
    annotation_weight_branch_->setEnabled(annotation_weight_branch_->count() > 0);
    const InferenceResultSet* selected = service_ && service_->source_node_id() != 0 ? service_->selected_inference_result() : nullptr;
    QStringList classes;
    QHash<int, QString> segmentation_class_names;
    if (model) {
        for (const jocelin::model::ModelClass& value : model->classes) {
            const QString name = QString::fromStdString(value.name);
            classes.push_back(name);
            segmentation_class_names.insert(value.mask_value, name);
        }
    }
    if (model && service_) {
        segmentation_->set_inference_result(selected, segmentation_class_names,
                                            model->classes, service_->binding().classes);
    } else {
        segmentation_->set_inference_result(selected, segmentation_class_names);
    }
    classification_->set_inference_result(selected, classes);
}

void TaskWorkspace::refresh() {
    if (!service_) return;
    try {
        service_->synchronize();
        const auto* module = find_task_module(service_->task().task);
        identity_->setText(
            module ? QString::fromLatin1(module->id).toUpper() : tr("TASK"));
        context_->clear();
        context_->hide();
        data_->set_service(service_.get());
        split_->set_service(service_.get());
        training_->set_service(service_.get());
        inference_->set_service(service_.get());
        results_->set_service(service_.get());
        refresh_models();
        refresh_annotation();
        refresh_step_availability();
    } catch (const std::exception& error) {
        context_->setText(QString::fromUtf8(error.what()));
        context_->show();
        refresh_step_availability();
    }
}

void TaskWorkspace::refresh_step_availability() {
    const bool bound = service_ && service_->source_node_id() != 0;
    const bool has_samples = bound && !service_->binding().samples.empty();
    const bool manage_classes = service_ && service_->task().task == "sg";
    for (int index = 0; index < static_cast<int>(step_buttons_.size()); ++index) {
        const bool enabled = index == static_cast<int>(Step::Data) ||
            (index == static_cast<int>(Step::Annotation) && manage_classes) ||
            (index == static_cast<int>(Step::Training) && bound) ||
            (index == static_cast<int>(Step::Inference) && bound) ||
            (index == static_cast<int>(Step::Results) && bound) ||
            has_samples;
        auto* button = step_buttons_[static_cast<size_t>(index)];
        if (!button) continue;
        button->setEnabled(enabled && (!runtime_busy_ || index == static_cast<int>(active_step_)));
        button->setToolTip(enabled ? QString{} :
            !bound ? tr("Connect one Dataset node to this Task first.")
                   : tr("Import at least one valid sample in the connected Dataset."));
    }
}

void TaskWorkspace::activate_step(Step step) {
    const int index = static_cast<int>(step);
    if (index < 0 || index >= static_cast<int>(step_buttons_.size())) return;
    auto* selected_button = step_buttons_[static_cast<size_t>(index)];
    if (!selected_button || !selected_button->isEnabled()) return;
    active_step_ = step;
    pages_->setCurrentIndex(index);
    for (int item = 0; item < static_cast<int>(step_buttons_.size()); ++item) {
        auto* button = step_buttons_[static_cast<size_t>(item)];
        if (!button) continue;
        button->setProperty("active", item == index);
        button->setProperty("done", item < index);
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
    if (step == Step::Annotation && annotation_binding_dirty_) {
        // The Data page may replace the complete Task binding through an
        // explicit annotation-package import. Rebind the editor on entry so
        // it cannot retain its pre-import snapshot, while keeping the import
        // action itself responsive.
        refresh_annotation();
    }
    if (step == Step::Annotation && segmentation_)
        segmentation_->refresh_inference_layers();
    if (step == Step::Annotation && classification_)
        classification_->refresh_inference_result();
    if (step == Step::Training && training_) training_->refresh();
    if (step == Step::Results && results_) results_->refresh();
}

void TaskWorkspace::domain_changed() {
    if (changed) changed();
    refresh_step_availability();
    results_->refresh();
}

void TaskWorkspace::set_runtime_busy(bool busy) {
    runtime_busy_ = busy;
    bool training_busy = false;
    if (busy && service_) {
        const std::string& model_id = service_->task().runtime.training_model_id;
        const auto model = std::find_if(
            service_->models().begin(), service_->models().end(),
            [&model_id](const jocelin::model::ModelEntry& value) { return value.id == model_id; });
        training_busy = model != service_->models().end() &&
            model->training_status == jocelin::model::ModelTrainingStatus::Running;
    }
    const bool inference_busy =
        busy && service_ &&
        service_->task().runtime.inference_status == "running";
    training_->set_busy(training_busy);
    inference_->set_busy(inference_busy);
    refresh_step_availability();
}

void TaskWorkspace::training_started() {
    activate_step(Step::Training);
    training_->begin();
    set_runtime_busy(true);
    if (changed) changed();
}

void TaskWorkspace::update_training_progress(
    const jocelin::train::TrainingProgress& progress)
{
    training_->update_progress(progress);
}

void TaskWorkspace::finish_training(
    const jocelin::training::TrainingResult& result,
    const QString& error)
{
    set_runtime_busy(false);
    training_->finish(result, error);
    results_->refresh();
    if (changed) changed();
}

void TaskWorkspace::inference_started() {
    activate_step(Step::Inference);
    inference_->begin();
    set_runtime_busy(true);
    if (changed) changed();
}

void TaskWorkspace::update_inference_progress(
    size_t done, size_t total, const QString& image)
{
    inference_->update_progress(done, total, image);
}

void TaskWorkspace::finish_inference(const QString& error)
{
    set_runtime_busy(false);
    inference_->finish(error);
    refresh_annotation();
    results_->refresh();
    if (error.isEmpty()) activate_step(Step::Annotation);
    if (changed) changed();
}

} // namespace studio
