#include "modules/task/ui/annotation/shared/annotation_panels.h"
#include "modules/task/ui/samples/annotation_samples.h"
#include "modules/task/ui/annotation/cls/classification_annotation.h"
#include "modules/task/ui/annotation/shared/annotation_panel_toggle.h"

#include "common/ui/ui_metrics.h"
#include "modules/task/presentation/sample_presentation.h"
#include "modules/task/ui/image/image_viewport_transform.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>
#include <utility>

namespace studio {

namespace {

QVector<ResultPresentationItem> present_result(
    const jocelin::result::cls::ClsResult& result, const QStringList& class_names) {
    ResultPresentationItem item;
    const int index = result.predicted_class - 1;
    item.text = result.predicted_class == -1 ? QObject::tr("Unknown")
        : index >= 0 && index < class_names.size() ? class_names[index]
        : QObject::tr("Class %1").arg(result.predicted_class);
    item.attributes.append({QObject::tr("Confidence"),
                            QString::number(result.confidence * 100.0, 'f', 1) + "%"});
    return {item};
}

QColor classification_color(int index) {
    static const QColor colors[]{QColor("#1F8A9E"), QColor("#D18432"), QColor("#458C63"),
                                 QColor("#C34655"), QColor("#5B66AD"), QColor("#B55E96")};
    return colors[index % static_cast<int>(std::size(colors))];
}

QIcon class_icon(int index, const std::vector<AnnotationClass>& classes) {
    QColor color = index < static_cast<int>(classes.size())
        ? QColor(QString::fromStdString(classes[static_cast<size_t>(index)].color)) : QColor{};
    if (!color.isValid()) {
        color = classification_color(index);
    }
    return annotation_class_icon(color);
}

} // namespace

ClassificationCanvas::ClassificationCanvas(QWidget* parent, QLabel* overlay)
    : QLabel(parent), overlay_(overlay) {
    setAlignment(Qt::AlignCenter);
    setProperty("imagePreview", true);
    setStyleSheet(QStringLiteral("padding: 0;"));
    setMinimumSize(480, 320);
    setText(tr("Select an image"));
    if (overlay_) {
        overlay_->setParent(this);
        overlay_->hide();
    }
}

void ClassificationCanvas::set_image(const QImage& image) {
    image_ = image;
    update_view();
}

void ClassificationCanvas::set_heatmap(const QImage& image) {
    heatmap_ = image;
    update_view();
}

void ClassificationCanvas::set_heatmap_visible(bool visible) {
    heatmap_visible_ = visible;
    update_view();
}

void ClassificationCanvas::clear_canvas() {
    image_ = {};
    heatmap_ = {};
    clear();
    setText(tr("Select an image"));
    if (overlay_) {
        overlay_->hide();
    }
}

void ClassificationCanvas::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    update_view();
}

void ClassificationCanvas::update_view() {
    if (image_.isNull()) {
        setPixmap({});
        if (overlay_) {
            overlay_->hide();
        }
        if (text().isEmpty()) {
            setText(tr("Select an image"));
        }
        return;
    }
    const QSize available = ImageViewportTransform::fit_viewport(contentsRect().size(), ThemeTokens::Space3).size();
    QPixmap frame = QPixmap::fromImage(image_).scaled(
        available, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!frame.isNull() && heatmap_visible_ && !heatmap_.isNull()) {
        QPainter painter(&frame);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setOpacity(0.42);
        painter.drawImage(frame.rect(), heatmap_);
    }
    setPixmap(frame);
    if (overlay_) {
        const QRect image_rect = QStyle::alignedRect(layoutDirection(), alignment(), frame.size(), contentsRect());
        const QRect bounds = image_rect.adjusted(8, 8, -8, -8);
        overlay_->setVisible(bounds.isValid() && !overlay_->text().isEmpty());
        if (bounds.isValid()) {
            const int width = std::min(bounds.width(), overlay_->sizeHint().width());
            const int height = std::min(bounds.height(), overlay_->heightForWidth(width));
            overlay_->setGeometry(bounds.right() - width + 1, bounds.top(), width, height);
        }
    }
}

ClassificationAnnotationWidget::ClassificationAnnotationWidget(QWidget* parent, QTabWidget* inspector) : QWidget(parent) {
    setObjectName("classificationAnnotationWorkspace");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* context_bar = new QFrame(this);
    context_bar->setObjectName("classificationContextBar");
    auto* context_layout = new QHBoxLayout(context_bar);
    context_layout->setContentsMargins(10, 6, 10, 6);
    auto* title = new QLabel(tr("CLASSIFICATION LABELING"), context_bar);
    title->setProperty("eyebrow", true);
    context_ = new QLabel(tr("Import images to begin"), context_bar);
    context_->setObjectName("classificationContextLabel");
    context_->setProperty("overviewValue", true);
    QToolButton* toggle_images = make_annotation_panel_toggle(
        context_bar, tr("Images"), "classificationToggleImagesButton");
    QToolButton* toggle_classes = make_annotation_panel_toggle(
        context_bar, tr("Inspector"), "classificationToggleClassesButton");
    context_layout->addWidget(title);
    context_layout->addSpacing(10);
    context_layout->addWidget(toggle_images);
    context_layout->addWidget(toggle_classes);
    context_layout->addWidget(context_, 1);
    heatmap_layer_ = new QCheckBox(tr("Heatmap"), context_bar);
    heatmap_layer_->setObjectName("classificationHeatmapLayerCheck");
    heatmap_layer_->setEnabled(false);
    context_layout->addWidget(heatmap_layer_);
    root->addWidget(context_bar);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName("classificationContentSplitter");

    auto* images_panel = new QFrame(splitter);
    images_panel->setObjectName("classificationImagesPanel");
    images_panel->setMinimumWidth(0);
    images_panel->setMaximumWidth(300);
    auto* images_layout = new QVBoxLayout(images_panel);
    images_layout->setContentsMargins(8, 8, 8, 8);
    images_title_ = new QLabel(tr("IMAGES"), images_panel);
    images_title_->setProperty("eyebrow", true);
    auto* images_hint = new QLabel(tr("Right-click a thumbnail to assign its class."), images_panel);
    images_hint->setProperty("meta", true);
    images_hint->setWordWrap(true);
    images_ = new SampleBrowser(images_panel);
    images_->setObjectName("classificationImageList");
    images_->setIconSize(QSize(56, 42));
    images_->setUniformItemSizes(true);
    images_->setTextElideMode(Qt::ElideMiddle);
    images_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    images_->setContextMenuPolicy(Qt::CustomContextMenu);
    images_layout->addWidget(images_title_);
    images_layout->addWidget(images_hint);
    images_layout->addWidget(images_, 1);

    auto* preview_panel = new QFrame(splitter);
    preview_panel->setObjectName("classificationPreviewPanel");
    auto* preview_layout = new QVBoxLayout(preview_panel);
    preview_layout->setContentsMargins(0, 0, 0, 0);

    auto* review_bar = new QFrame(preview_panel);
    review_bar->setObjectName("classificationReviewBar");
    auto* review_layout = new QHBoxLayout(review_bar);
    review_layout->setContentsMargins(10, 5, 10, 5);
    review_layout->setSpacing(8);
    review_state_ = new QLabel(tr("Confidence: —"), review_bar);
    review_state_->setObjectName("classificationReviewState");
    review_state_->setProperty("meta", true);
    review_layout->addStretch();
    review_layout->addWidget(review_state_);
    preview_layout->addWidget(review_bar);

    image_labels_ = new QLabel(preview_panel);
    image_labels_->setObjectName("classificationImageLabels");
    image_labels_->setTextFormat(Qt::RichText);
    image_labels_->setWordWrap(true);
    image_labels_->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    connect(image_labels_, &QLabel::linkActivated, this, [this](const QString& link) {
        if (link != "prediction" || result_items_.isEmpty()) {
            return;
        }
        if (result_popup_) {
            result_popup_->close();
        }
        result_popup_ = show_result_properties(
            image_labels_, image_labels_->mapToGlobal(image_labels_->rect().bottomLeft()), result_items_.front());
    });
    preview_ = new ClassificationCanvas(preview_panel, image_labels_);
    preview_->setObjectName("classificationPreview");
    preview_->setToolTip(tr("Right-click to assign a class to the current image."));
    preview_layout->addWidget(preview_, 1);

    auto* classes_panel = new QFrame(splitter);
    classes_panel->setObjectName("classificationClassesPanel");
    classes_panel->setMinimumWidth(0);
    auto* classes_layout = new QVBoxLayout(classes_panel);
    classes_layout->setContentsMargins(10, 10, 10, 10);
    auto* classes_title = new QLabel(tr("CLASSES"), classes_panel);
    classes_title->setProperty("eyebrow", true);
    auto* classes_hint = new QLabel(tr("Choose a class, then assign it to the selected images."), classes_panel);
    classes_hint->setProperty("meta", true);
    classes_hint->setWordWrap(true);
    empty_hint_ = new QLabel(tr("No classes yet. Add the first class or right-click an image."), classes_panel);
    empty_hint_->setProperty("inlineWarning", true);
    empty_hint_->setWordWrap(true);
    classes_ = annotation_class_list(classes_panel, "classificationClassList");
    auto* add_class = new QPushButton(tr("Add class..."), classes_panel);
    add_class->setObjectName("classificationAddClassButton");
    assign_ = new QPushButton(tr("Assign to selected images"), classes_panel);
    assign_->setObjectName("classificationAssignClassButton");
    assign_->setProperty("primary", true);
    classes_layout->addWidget(classes_title);
    classes_layout->addWidget(classes_hint);
    classes_layout->addWidget(empty_hint_);
    classes_layout->addWidget(classes_, 1);
    classes_layout->addWidget(add_class);
    classes_layout->addWidget(assign_);

    QTabWidget* tabs = inspector ? inspector : new QTabWidget(splitter);
    tabs->setObjectName("classificationAnnotationInspector");
    tabs->setMinimumWidth(0);
    tabs->addTab(classes_panel, tr("Classes"));
    result_details_ = annotation_results_tab(tabs, "classificationAnnotationResultDetails");

    splitter->addWidget(images_panel);
    splitter->addWidget(preview_panel);
    splitter->addWidget(tabs);
    splitter->setCollapsible(0, true);
    splitter->setCollapsible(1, false);
    splitter->setCollapsible(2, true);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 700, 360});
    root->addWidget(splitter, 1);
    connect(toggle_images, &QToolButton::toggled, this, [splitter](bool shown) {
        auto sizes = splitter->sizes();
        sizes[0] = shown ? 240 : 0;
        splitter->setSizes(sizes);
    });
    connect(toggle_classes, &QToolButton::toggled, this, [splitter](bool shown) {
        auto sizes = splitter->sizes();
        sizes[2] = shown ? 360 : 0;
        splitter->setSizes(sizes);
    });

    images_->current_requested = [this](int index, const QString&) {
        load_row(index);
        return true;
    };
    images_->split_requested = [this](const std::vector<std::string>& ids, const std::string& split) {
        if (sample_split_requested) {
            sample_split_requested(ids, split);
        }
    };
    images_->extend_context_menu = [this](QMenu& menu, const std::vector<std::string>&) {
        append_class_actions(menu);
    };
    for (QLabel* surface : {static_cast<QLabel*>(preview_), image_labels_}) {
        surface->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(surface, &QLabel::customContextMenuRequested, this, [this, surface](const QPoint& position) {
            show_class_menu(surface->mapToGlobal(position));
        });
    }
    connect(heatmap_layer_, &QCheckBox::toggled, preview_, &ClassificationCanvas::set_heatmap_visible);
    connect(add_class, &QPushButton::clicked, this, [this] { prompt_for_class(); });
    connect(assign_, &QPushButton::clicked, this, [this] {
        if (images_->selectedItems().isEmpty()) {
            return;
        }
        int selected = classes_->currentRow();
        if (selected < 0) {
            selected = prompt_for_class() - 1;
        }
        if (selected >= 0) {
            assign_selected(selected);
        }
    });
}

void ClassificationAnnotationWidget::set_inference_result(
    const InferenceResultSet* result, const QStringList& model_classes)
{
    inference_results_ = result ? result->samples : std::vector<ResultReference>{};
    prediction_class_names_ = model_classes;
    refresh_inference_result();
}

void ClassificationAnnotationWidget::refresh_inference_result() {
    if (result_popup_) {
        result_popup_->close();
    }
    result_items_.clear();
    confidence_ = 0.0;
    current_heatmap_ = {};

    if (current_row_ >= 0 && current_row_ < image_paths_.size()) {
        for (const auto& reference : inference_results_) {
            const auto* value = std::get_if<jocelin::result::cls::ClsResult>(&reference.result);
            if (!value || reference.source_sample_id != sample_bindings_[static_cast<size_t>(current_row_)].sample_id ||
                QFileInfo(QString::fromStdString(value->image_path)).absoluteFilePath() != image_paths_[current_row_]) continue;
            confidence_ = value->confidence;
            result_items_ = present_result(*value, prediction_class_names_);
            current_heatmap_ = QImage(QString::fromStdString(value->heatmap_path));
            break;
        }
    }

    heatmap_layer_->setEnabled(!current_heatmap_.isNull());
    update_review_state();
    update_preview();
}

void ClassificationAnnotationWidget::set_dataset(
    const std::vector<TaskSampleBinding>& samples,
    const QStringList& class_names,
    const QVector<int>& class_labels,
    const std::vector<AnnotationClass>& class_metadata)
{
    sample_bindings_.clear();
    image_paths_.clear();
    display_names_.clear();
    class_labels_.clear();
    class_names_ = class_names;
    class_metadata_ = class_metadata;
    for (int sample_index = 0;
         sample_index < static_cast<int>(samples.size());
         ++sample_index) {
        const auto& sample = samples[static_cast<size_t>(sample_index)];
        if (!annotation_sample_visible(sample)) {
            continue;
        }
        sample_bindings_.push_back(sample);
        image_paths_.push_back(QFileInfo(QString::fromStdString(
            sample.image_paths.front())).absoluteFilePath());
        display_names_.push_back(image_display_name(sample_index, sample));
        class_labels_.push_back(class_labels.value(sample_index, -1));
    }
    current_row_ = -1;
    current_image_ = {};
    rebuild_classes();
    images_->clear();
    refresh_images();
    if (!image_paths_.isEmpty()) {
        load_row(images_->current_source_index());
    }
    else {
        current_row_ = -1;
        current_image_ = {};
        context_->setText(tr("No valid imported images are connected"));
        context_->setToolTip({});
        refresh_inference_result();
        preview_->setText(tr("Import images in the connected Dataset node"));
    }
}

void ClassificationAnnotationWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    update_preview();
}

void ClassificationAnnotationWidget::load_row(int row) {
    if (row < 0 || row >= image_paths_.size()) return;
    current_row_ = row;
    current_image_ = QImage(image_paths_[row]);
    const QSize dimensions = current_image_.size();
    const int label = class_labels_.value(row, -1);
    const QString class_state = label >= 0 && label < class_names_.size()
        ? tr("Class: %1").arg(class_names_[label]) : tr("Unassigned");
    context_->setText(tr("%1 / %2  |  %3  |  %4 × %5")
        .arg(row + 1)
        .arg(image_paths_.size())
        .arg(display_names_.value(row))
        .arg(dimensions.width())
        .arg(dimensions.height()));
    if (row < static_cast<int>(sample_bindings_.size()))
        context_->setToolTip(sample_image_tooltip(
            display_names_.value(row),
            sample_bindings_[static_cast<size_t>(row)],
            class_state));
    classes_->setCurrentRow(label >= 0 && label < class_names_.size() ? label : -1);
    assign_->setEnabled(true);
    assign_->setToolTip({});
    refresh_inference_result();
}

void ClassificationAnnotationWidget::update_preview() {
    if (!preview_) return;
    if (current_image_.isNull()) {
        preview_->clear_canvas();
        preview_->setText(tr("The selected image could not be loaded"));
        return;
    }
    preview_->set_image(current_image_);
    preview_->set_heatmap(current_heatmap_);
}

void ClassificationAnnotationWidget::update_review_state() {
    if (!review_state_) {
        return;
    }
    const int label = class_labels_.value(current_row_, -1);
    const QString ground_truth_name = label >= 0 && label < class_names_.size()
        ? class_names_[label] : tr("Unassigned");
    const QString ground_truth = tr("Ground truth: %1").arg(ground_truth_name.toHtmlEscaped());
    if (result_items_.isEmpty()) {
        image_labels_->setText(ground_truth);
        review_state_->setText(tr("Confidence: —"));
        result_details_->setText(tr("No inference result for this image."));
        return;
    }

    const QString prediction = result_items_.front().text;
    const QString confidence = QString::number(confidence_ * 100.0, 'f', 1);
    const QString prediction_link = QStringLiteral("<a href=\"prediction\" style=\"color:#ACE2DD\">%1</a>")
        .arg(prediction.toHtmlEscaped());
    result_details_->setText(tr("Prediction: %1\nConfidence: %2%").arg(prediction, confidence));
    image_labels_->setText(tr("Prediction: %1").arg(prediction_link) + QStringLiteral("<br/>") + ground_truth);
    review_state_->setText(tr("Confidence: %1%").arg(confidence));
}

void ClassificationAnnotationWidget::refresh_images() {
    int assigned = 0;
    std::vector<SampleBrowserEntry> entries;
    for (int row = 0; row < image_paths_.size(); ++row) {
        const int label = class_labels_.value(row, -1);
        const bool valid_label = label >= 0 && label < class_names_.size();
        assigned += valid_label ? 1 : 0;
        const QString state = valid_label ? tr("Class: %1").arg(class_names_[label])
            : tr("Unassigned - right-click to set class");
        SampleBrowserEntry entry = annotation_sample_entry(
            sample_bindings_[static_cast<size_t>(row)], row, row, state);
        entry.display_name = display_names_.value(row);
        entry.tooltip = sample_image_tooltip(entry.display_name,
            sample_bindings_[static_cast<size_t>(row)], state);
        if (images_->count() == image_paths_.size()) {
            images_->update_entry_state(row, entry.display_name, entry.annotation_state,
                entry.split_state, entry.tooltip);
        } else {
            entries.push_back(std::move(entry));
        }
    }
    if (images_->count() != image_paths_.size()) {
        images_->set_entries(entries);
    }
    images_title_->setText(tr("IMAGES  (%1 assigned / %2)").arg(assigned).arg(image_paths_.size()));
}

void ClassificationAnnotationWidget::rebuild_classes() {
    const int selected = classes_->currentRow();
    classes_->clear();
    for (int index = 0; index < class_names_.size(); ++index) {
        QListWidgetItem* item = new QListWidgetItem(class_icon(index, class_metadata_), class_names_[index], classes_);
        if (index < static_cast<int>(class_metadata_.size())) {
            item->setData(Qt::UserRole, QString::fromStdString(class_metadata_[static_cast<size_t>(index)].id));
        }
        item->setSizeHint(QSize(0, 28));
    }
    classes_->setCurrentRow(std::clamp(selected, -1, class_names_.size() - 1));
    empty_hint_->setVisible(class_names_.isEmpty());
    assign_->setEnabled(current_row_ >= 0);
    assign_->setToolTip(current_row_ >= 0 ? QString{} : tr("Select an image before assigning a class."));
}

void ClassificationAnnotationWidget::show_class_menu(const QPoint& global_position) {
    if (current_row_ < 0) {
        return;
    }
    QMenu menu(this);
    images_->append_context_actions(menu, images_->selected_sample_ids());
    menu.exec(global_position);
}

void ClassificationAnnotationWidget::append_class_actions(QMenu& menu) {
    auto* class_menu = menu.addMenu(tr("Assign class"));
    if (!class_names_.isEmpty()) {
        for (int index = 0; index < class_names_.size(); ++index) {
            auto* action = class_menu->addAction(classes_->item(index)->icon(), class_names_[index]);
            action->setCheckable(true);
            action->setChecked(class_labels_.value(current_row_, -1) == index);
            connect(action, &QAction::triggered, this, [this, index] { assign_selected(index); });
        }
        class_menu->addSeparator();
    }
    auto* add = class_menu->addAction(
        class_names_.isEmpty() ? tr("Add first class...") : tr("Add new class..."));
    connect(add, &QAction::triggered, this, [this] {
        const int value = prompt_for_class();
        if (value > 0) assign_selected(value - 1);
    });

    menu.addSeparator();
    auto* empty = menu.addAction(tr("Set as empty"));
    empty->setObjectName("annotationSetAsEmptyAction");
    empty->setEnabled(false);
    empty->setToolTip(tr("Classification requires an assigned class."));
}

int ClassificationAnnotationWidget::prompt_for_class() {
    bool accepted = false;
    const QString selected = QInputDialog::getItem(this,
        class_names_.isEmpty() ? tr("Create the first classification class") : tr("Choose classification class"),
        class_names_.isEmpty() ? tr("Class name") : tr("Choose an existing class or type a new name"),
        class_names_, std::max(0, classes_->currentRow()), true, &accepted).trimmed();
    return accepted ? add_or_select_class(selected) : 0;
}

int ClassificationAnnotationWidget::add_or_select_class(const QString& requested_name) {
    const QString name = requested_name.trimmed();
    if (name.isEmpty()) return 0;
    auto find_name = [this, &name] {
        for (int candidate = 0; candidate < class_names_.size(); ++candidate)
            if (class_names_[candidate].compare(name, Qt::CaseInsensitive) == 0)
                return candidate;
        return -1;
    };
    int index = find_name();
    if (index < 0) {
        if (!class_add_requested) {
            return 0;
        }
        std::vector<AnnotationClass> classes = class_add_requested(name);
        if (classes.empty()) {
            return 0;
        }
        index = class_names_.size();
        class_names_.push_back(QString::fromStdString(classes.back().name));
        class_metadata_ = std::move(classes);
        rebuild_classes();
    }
    classes_->setCurrentRow(index);
    return index + 1;
}

void ClassificationAnnotationWidget::assign_selected(int class_index) {
    if (class_index < 0 || class_index >= class_names_.size()) {
        return;
    }
    const QList<QListWidgetItem*> selected = images_->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    std::vector<std::string> sample_ids;
    sample_ids.reserve(static_cast<size_t>(selected.size()));
    for (QListWidgetItem* item : selected) {
        const int row = images_->row(item);
        class_labels_[row] = class_index;
        sample_ids.push_back(sample_bindings_[static_cast<size_t>(row)].sample_id);
    }
    refresh_images();
    load_row(current_row_);
    if (class_assigned) {
        class_assigned(sample_ids, class_index);
    }
}

} // namespace studio
