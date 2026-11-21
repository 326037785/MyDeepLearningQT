#include "modules/task/ui/annotation/sg/segmentation_classes.h"

#include "modules/task/ui/annotation/shared/annotation_classes_panel.h"
#include "modules/task/ui/annotation/shared/annotation_panels.h"
#include "modules/task/ui/annotation/sg/segmentation_annotation.h"
#include "modules/task/application/task_dataset_binding.h"
#include "modules/task/application/task_workspace_service.h"

#include <QListWidget>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QToolButton>
#include <algorithm>

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

SegmentationClasses::SegmentationClasses(QTabWidget* inspector, SegmentationAnnotationWidget* editor)
    : QObject(inspector), editor_(editor) {
    panel_ = new AnnotationClassesPanel({"segmentationClassList", "segmentationAddClassButton",
        "segmentationRenameClassButton", "segmentationDeleteClassButton"}, inspector);
    inspector->insertTab(0, panel_, tr("Classes"));
    QToolButton* lock = annotation_icon_button(panel_, panel_->tools(), "segmentationLockClassButton",
        "lock", tr("Lock or unlock the selected class"));
    QToolButton* up = annotation_icon_button(panel_, panel_->tools(), "segmentationMoveClassUpButton",
        "up", tr("Move class up"));
    QToolButton* down = annotation_icon_button(panel_, panel_->tools(), "segmentationMoveClassDownButton",
        "down", tr("Move class down"));
    lock->setCheckable(true);
    MaskCanvas* canvas = editor_->canvas();
    connect(list(), &QListWidget::currentItemChanged, this, [this, canvas, lock, up, down] {
        const AnnotationClass* selected = selected_class();
        const bool locked = selected && selected->locked;
        canvas->set_class_value(selected ? selected->mask_value : 0);
        canvas->set_editing_enabled(!locked);
        const QSignalBlocker blocker(lock);
        lock->setChecked(locked);
        lock->setEnabled(selected != nullptr);
        up->setEnabled(list()->currentRow() > 1);
        down->setEnabled(selected && list()->currentRow() + 1 < list()->count());
    });
    canvas->class_picked = [this](int value) {
        const AnnotationClass* schema = service_
            ? find_annotation_class_by_mask(service_->binding(), value) : nullptr;
        if (!schema) {
            return;
        }
        const QString id = QString::fromStdString(schema->id);
        for (int row = 1; row < list()->count(); ++row) {
            if (list()->item(row)->data(Qt::UserRole).toString() == id) {
                list()->setCurrentRow(row);
                return;
            }
        }
    };
    canvas->choose_class = [this] {
        const std::string id = choose_class ? choose_class() : std::string{};
        const AnnotationClass* schema = service_ ? find_annotation_class(service_->binding(), id) : nullptr;
        return schema ? schema->mask_value : 0;
    };
    panel_->add_class = [this](const QString& name) {
        const AnnotationClass* selected = selected_class();
        const std::string previous = selected ? selected->id : std::string{};
        service_->add_class(name.toStdString());
        refresh(previous);
        if (changed) {
            changed();
        }
    };
    panel_->rename_class = [this](const std::string& id, const QString& name) {
        service_->rename_class(id, name.toStdString());
        refresh(id);
        if (changed) {
            changed();
        }
    };
    panel_->removal_message = [this](const std::string& id) {
        const AnnotationClass* schema = find_annotation_class(service_->binding(), id);
        const int value = schema->mask_value;
        const std::vector<TaskSampleBinding>& samples = service_->binding().samples;
        const int affected = static_cast<int>(std::count_if(samples.begin(), samples.end(),
            [value](const TaskSampleBinding& sample) { return has_class_pixels(sample, value); }));
        return affected > 0 ? tr("Delete '%1' and its annotations in %2 sample(s)?")
            .arg(QString::fromStdString(schema->name)).arg(affected) : QString{};
    };
    panel_->remove_class = [this, canvas](const std::string& id) {
        const int value = find_annotation_class(service_->binding(), id)->mask_value;
        service_->remove_class(id);
        canvas->remove_class_pixels(value);
        refresh({});
        editor_->refresh_samples(service_->binding().samples, {});
        if (changed) {
            changed();
        }
    };
    const auto move = [this](int direction) {
        if (const AnnotationClass* schema = selected_class()) {
            const std::string id = schema->id;
            service_->move_class(id, direction);
            refresh(id);
            if (changed) {
                changed();
            }
        }
    };
    connect(up, &QToolButton::clicked, this, [move] { move(-1); });
    connect(down, &QToolButton::clicked, this, [move] { move(1); });
    connect(lock, &QToolButton::toggled, this, [this](bool locked) {
        if (const AnnotationClass* schema = selected_class()) {
            const std::string id = schema->id;
            service_->set_class_locked(id, locked);
            refresh(id);
            if (changed) {
                changed();
            }
        }
    });
    connect(list(), &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        const std::string id = item->data(Qt::UserRole).toString().toStdString();
        if (id.empty() || !service_) {
            return;
        }
        const AnnotationClass* selected = selected_class();
        const std::string previous = selected ? selected->id : std::string{};
        service_->set_class_visible(id, item->checkState() == Qt::Checked);
        refresh(previous);
        if (changed) {
            changed();
        }
    });
    refresh({});
}

void SegmentationClasses::set_service(TaskWorkspaceService* service) {
    service_ = service;
    panel_->set_available(service_ != nullptr);
}

QListWidget* SegmentationClasses::list() const {
    return panel_->list();
}

const AnnotationClass* SegmentationClasses::selected_class() const {
    const QListWidgetItem* item = list()->currentItem();
    return service_ && item
        ? find_annotation_class(service_->binding(), item->data(Qt::UserRole).toString().toStdString())
        : nullptr;
}

void SegmentationClasses::refresh(const std::string& selected) {
    QHash<int, QColor> colors;
    QSet<int> hidden;
    int row = 0;
    {
        const QSignalBlocker blocker(list());
        list()->clear();
        new QListWidgetItem(tr("No class selected"), list());
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
                const QString id = QString::fromStdString(schema.id);
                const QString name = QString::fromStdString(schema.name);
                QListWidgetItem* item = new QListWidgetItem(annotation_class_icon(color),
                    schema.locked ? tr("%1 · locked").arg(name) : name, list());
                item->setData(Qt::UserRole, id);
                item->setData(AnnotationClassesPanel::NameRole, name);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(schema.visible ? Qt::Checked : Qt::Unchecked);
                item->setToolTip(tr("Stable class ID: %1\nMask value: %2").arg(id).arg(schema.mask_value));
                if (schema.id == selected) {
                    row = list()->row(item);
                }
            }
        }
    }
    list()->setCurrentRow(row);
    editor_->canvas()->set_class_display(colors, hidden);
}

} // namespace studio
