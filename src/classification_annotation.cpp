#include "classification_annotation.h"

#include "sample_presentation.h"

#include <QAbstractItemView>
#include <QAction>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSplitter>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>
#include <utility>

namespace studio {

namespace {

QColor classification_color(int index) {
    static const QColor colors[]{QColor("#1F8A9E"), QColor("#D18432"), QColor("#458C63"),
                                 QColor("#C34655"), QColor("#5B66AD"), QColor("#B55E96")};
    return colors[index % static_cast<int>(std::size(colors))];
}

QIcon class_icon(int index) {
    QPixmap chip(14, 14);
    chip.fill(classification_color(index));
    return QIcon(chip);
}

} // namespace

ClassificationCanvas::ClassificationCanvas(QWidget* parent) : QLabel(parent) {
    setAlignment(Qt::AlignCenter);
    setProperty("imagePreview", true);
    setMinimumSize(480, 320);
    setText(tr("Select an image"));
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
}

void ClassificationCanvas::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    update_view();
}

void ClassificationCanvas::update_view() {
    const QImage& shown = heatmap_visible_ && !heatmap_.isNull() ? heatmap_ : image_;
    if (shown.isNull()) {
        setPixmap({});
        if (text().isEmpty()) setText(tr("Select an image"));
        return;
    }
    setText({});
    const QSize available = size() - QSize(24, 24);
    setPixmap(QPixmap::fromImage(shown).scaled(
        available, Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
    auto make_panel_toggle = [](QWidget* parent, const QString& text,
                                const QString& object_name) {
        auto* button = new QToolButton(parent);
        button->setObjectName(object_name);
        button->setText(text);
        button->setCheckable(true);
        button->setChecked(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        return button;
    };
    auto* toggle_images = make_panel_toggle(
        context_bar, tr("Images"), "classificationToggleImagesButton");
    auto* toggle_classes = make_panel_toggle(
        context_bar, tr("Inspector"), "classificationToggleClassesButton");
    context_layout->addWidget(title);
    context_layout->addSpacing(10);
    context_layout->addWidget(toggle_images);
    context_layout->addWidget(toggle_classes);
    context_layout->addWidget(context_, 1);
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
    images_ = new QListWidget(images_panel);
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
    auto* review_caption = new QLabel(tr("SHOW"), review_bar);
    review_caption->setProperty("eyebrow", true);
    ground_truth_ = new QPushButton(tr("Ground truth"), review_bar);
    ground_truth_->setObjectName("classificationGroundTruthButton");
    prediction_ = new QPushButton(tr("Prediction"), review_bar);
    prediction_->setObjectName("classificationPredictionButton");
    heatmap_ = new QPushButton(tr("Heatmap"), review_bar);
    heatmap_->setObjectName("classificationHeatmapButton");
    for (auto* button : {ground_truth_, prediction_, heatmap_}) {
        button->setCheckable(true);
        button->setProperty("compact", true);
    }
    ground_truth_->setChecked(true);
    prediction_->setEnabled(false);
    heatmap_->setEnabled(false);
    review_state_ = new QLabel(tr("Ground truth: unassigned"), review_bar);
    review_state_->setObjectName("classificationReviewState");
    review_state_->setProperty("meta", true);
    review_layout->addWidget(review_caption);
    review_layout->addWidget(ground_truth_);
    review_layout->addWidget(prediction_);
    review_layout->addWidget(heatmap_);
    review_layout->addStretch();
    review_layout->addWidget(review_state_);
    preview_layout->addWidget(review_bar);

    preview_ = new ClassificationCanvas(preview_panel);
    preview_->setObjectName("classificationPreview");
    preview_->setContextMenuPolicy(Qt::CustomContextMenu);
    preview_->setToolTip(tr("Right-click to assign a class to the current image."));
    preview_layout->addWidget(preview_, 1);

    auto* classes_panel = new QFrame(splitter);
    classes_panel->setObjectName("classificationClassesPanel");
    classes_panel->setMinimumWidth(0);
    auto* classes_layout = new QVBoxLayout(classes_panel);
    classes_layout->setContentsMargins(10, 10, 10, 10);
    auto* classes_title = new QLabel(tr("CLASSES"), classes_panel);
    classes_title->setProperty("eyebrow", true);
    auto* classes_hint = new QLabel(tr("Choose a class, then assign it to the selected image."), classes_panel);
    classes_hint->setProperty("meta", true);
    classes_hint->setWordWrap(true);
    empty_hint_ = new QLabel(tr("No classes yet. Add the first class or right-click an image."), classes_panel);
    empty_hint_->setProperty("inlineWarning", true);
    empty_hint_->setWordWrap(true);
    classes_ = new QListWidget(classes_panel);
    classes_->setObjectName("classificationClassList");
    classes_->setIconSize(QSize(14, 14));
    auto* add_class = new QPushButton(tr("Add class..."), classes_panel);
    add_class->setObjectName("classificationAddClassButton");
    assign_ = new QPushButton(tr("Assign to selected image"), classes_panel);
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
    QWidget* results_panel = new QWidget(tabs);
    QVBoxLayout* results_layout = new QVBoxLayout(results_panel);
    results_layout->setContentsMargins(8, 8, 8, 8);
    result_details_ = new QLabel(tr("No inference result for this image."), results_panel);
    result_details_->setObjectName("classificationAnnotationResultDetails");
    result_details_->setWordWrap(true);
    result_details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    results_layout->addWidget(result_details_);
    results_layout->addStretch();
    tabs->addTab(results_panel, tr("Results"));

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

    connect(images_, &QListWidget::currentRowChanged, this, [this](int row) { load_row(row); });
    connect(images_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        if (auto* item = images_->itemAt(position)) {
            if (!item->isSelected()) {
                images_->clearSelection();
                item->setSelected(true);
            }
            images_->setCurrentItem(item);
        }
        show_class_menu(images_->viewport()->mapToGlobal(position));
    });
    connect(preview_, &QLabel::customContextMenuRequested, this, [this](const QPoint& position) {
        show_class_menu(preview_->mapToGlobal(position));
    });
    connect(ground_truth_, &QPushButton::clicked, this, [this] { set_view_mode(0); });
    connect(prediction_, &QPushButton::clicked, this, [this] { set_view_mode(1); });
    connect(heatmap_, &QPushButton::clicked, this, [this] { set_view_mode(2); });
    connect(add_class, &QPushButton::clicked, this, [this] { prompt_for_class(); });
    connect(assign_, &QPushButton::clicked, this, [this] {
        int selected = classes_->currentRow();
        if (selected < 0) selected = prompt_for_class() - 1;
        if (selected >= 0) assign_current(selected);
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
    predicted_class_ = -1;
    confidence_ = 0.0;
    has_prediction_ = false;
    current_heatmap_ = {};

    if (current_row_ >= 0 && current_row_ < image_paths_.size()) {
        for (const auto& reference : inference_results_) {
            const auto* value = std::get_if<jocelin::result::cls::ClsResult>(&reference.result);
            if (!value || reference.source_sample_id != sample_bindings_[static_cast<size_t>(current_row_)].sample_id ||
                QFileInfo(QString::fromStdString(value->image_path)).absoluteFilePath() != image_paths_[current_row_]) continue;
            predicted_class_ = value->predicted_class;
            confidence_ = value->confidence;
            has_prediction_ = true;
            current_heatmap_ = QImage(QString::fromStdString(value->heatmap_path));
            break;
        }
    }

    prediction_->setEnabled(has_prediction_);
    heatmap_->setEnabled(!current_heatmap_.isNull());
    if ((view_mode_ == 1 && !has_prediction_) ||
        (view_mode_ == 2 && current_heatmap_.isNull()))
        view_mode_ = 0;
    set_view_mode(view_mode_);
}

void ClassificationAnnotationWidget::set_dataset(
    const std::vector<TaskSampleBinding>& samples,
    const QStringList& class_names,
    const QVector<int>& class_labels)
{
    sample_bindings_.clear();
    image_paths_.clear();
    display_names_.clear();
    class_labels_.clear();
    class_names_ = class_names;
    for (int sample_index = 0;
         sample_index < static_cast<int>(samples.size());
         ++sample_index) {
        const auto& sample = samples[static_cast<size_t>(sample_index)];
        if (!sample.selected || sample.image_paths.empty()) continue;
        sample_bindings_.push_back(sample);
        image_paths_.push_back(QFileInfo(QString::fromStdString(
            sample.image_paths.front())).absoluteFilePath());
        display_names_.push_back(image_display_name(sample_index, sample));
        class_labels_.push_back(class_labels.value(sample_index, -1));
    }
    current_row_ = -1;
    current_image_ = {};
    rebuild_classes();
    rebuild_images();
    if (!image_paths_.isEmpty()) images_->setCurrentRow(0);
    else {
        current_row_ = -1;
        current_image_ = {};
        context_->setText(tr("No valid imported images are connected"));
        context_->setToolTip({});
        preview_->clear_canvas();
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
    context_->setText(tr("%1 / %2  |  %3  |  %4 × %5  |  %6")
        .arg(row + 1)
        .arg(image_paths_.size())
        .arg(display_names_.value(row))
        .arg(dimensions.width())
        .arg(dimensions.height())
        .arg(class_state));
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
    preview_->set_heatmap_visible(view_mode_ == 2);
}

void ClassificationAnnotationWidget::set_view_mode(int mode) {
    if (mode == 1 && !has_prediction_) mode = 0;
    if (mode == 2 && current_heatmap_.isNull()) mode = 0;
    view_mode_ = mode;
    ground_truth_->setChecked(mode == 0);
    prediction_->setChecked(mode == 1);
    heatmap_->setChecked(mode == 2);
    update_review_state();
    update_preview();
}

void ClassificationAnnotationWidget::update_review_state() {
    if (!review_state_) {
        return;
    }
    const int label = class_labels_.value(current_row_, -1);
    const QString ground_truth = label >= 0 && label < class_names_.size()
        ? class_names_[label] : tr("Unassigned");
    const int predicted_index = predicted_class_ - 1;
    const QString prediction = predicted_index >= 0 && predicted_index < prediction_class_names_.size()
        ? prediction_class_names_[predicted_index]
        : predicted_class_ > 0 ? tr("Class %1").arg(predicted_class_) : tr("Unavailable");

    if (result_details_) {
        result_details_->setText(has_prediction_
            ? tr("Prediction: %1\nConfidence: %2%")
                  .arg(predicted_class_ == -1 ? tr("Unknown") : prediction)
                  .arg(confidence_ * 100.0, 0, 'f', 1)
            : tr("No inference result for this image."));
    }

    if (view_mode_ == 0) {
        review_state_->setText(tr("Ground truth: %1").arg(ground_truth));
    } else if (view_mode_ == 1) {
        review_state_->setText(tr("Prediction: %1 · Confidence: %2% · Ground truth: %3")
            .arg(prediction)
            .arg(confidence_ * 100.0, 0, 'f', 1)
            .arg(ground_truth));
    } else {
        review_state_->setText(tr("Heatmap · Prediction: %1 · Confidence: %2%")
            .arg(prediction)
            .arg(confidence_ * 100.0, 0, 'f', 1));
    }
}

void ClassificationAnnotationWidget::rebuild_images() {
    images_->clear();
    int assigned = 0;
    for (int row = 0; row < image_paths_.size(); ++row) {
        const int label = class_labels_.value(row, -1);
        const bool valid_label = label >= 0 && label < class_names_.size();
        assigned += valid_label ? 1 : 0;
        const QString state = valid_label
            ? tr("Class: %1").arg(class_names_[label])
            : tr("Unassigned - right-click to set class");
        auto* item = new QListWidgetItem(
            display_names_.value(row) + "\n" + state,
            images_);
        item->setData(Qt::UserRole, image_paths_[row]);
        item->setData(Qt::UserRole + 1, label);
        item->setSizeHint(QSize(0, 58));
        QImageReader reader(image_paths_[row]);
        reader.setAutoTransform(true);
        reader.setScaledSize(QSize(96, 72));
        const QImage thumbnail = reader.read();
        if (!thumbnail.isNull()) item->setIcon(QPixmap::fromImage(thumbnail));
        if (row < static_cast<int>(sample_bindings_.size()))
            item->setToolTip(sample_image_tooltip(
                display_names_.value(row),
                sample_bindings_[static_cast<size_t>(row)],
                state));
    }
    images_title_->setText(tr("IMAGES  (%1 assigned / %2)").arg(assigned).arg(image_paths_.size()));
}

void ClassificationAnnotationWidget::rebuild_classes() {
    const int selected = classes_->currentRow();
    classes_->clear();
    for (int index = 0; index < class_names_.size(); ++index) {
        auto* item = new QListWidgetItem(class_icon(index), class_names_[index], classes_);
        item->setSizeHint(QSize(0, 28));
    }
    classes_->setCurrentRow(std::clamp(selected, -1, class_names_.size() - 1));
    empty_hint_->setVisible(class_names_.isEmpty());
    assign_->setEnabled(current_row_ >= 0);
    assign_->setToolTip(current_row_ >= 0 ? QString{} : tr("Select an image before assigning a class."));
}

void ClassificationAnnotationWidget::show_class_menu(const QPoint& global_position) {
    if (current_row_ < 0) return;
    QMenu menu(this);
    auto* class_menu = menu.addMenu(tr("Assign class"));
    if (!class_names_.isEmpty()) {
        for (int index = 0; index < class_names_.size(); ++index) {
            auto* action = class_menu->addAction(class_icon(index), class_names_[index]);
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

    auto* split_menu = menu.addMenu(tr("Assign split"));
    for (const auto& option : {
             std::pair<const char*, const char*>("Train", "train"),
             {"Validation", "validation"}, {"Test", "test"},
             {"Unassigned", "unassigned"}}) {
        auto* action = split_menu->addAction(tr(option.first));
        connect(action, &QAction::triggered, this, [this, value = std::string(option.second)] {
            if (!sample_split_requested) return;
            std::vector<std::string> ids;
            for (auto* item : images_->selectedItems()) {
                const int row = images_->row(item);
                if (row >= 0 && row < static_cast<int>(sample_bindings_.size()))
                    ids.push_back(sample_bindings_[static_cast<size_t>(row)].sample_id);
            }
            if (!ids.empty()) sample_split_requested(ids, value);
        });
    }
    menu.addSeparator();
    auto* empty = menu.addAction(tr("Set as empty"));
    empty->setObjectName("annotationSetAsEmptyAction");
    empty->setEnabled(false);
    empty->setToolTip(tr("Classification requires an assigned class."));
    menu.exec(global_position);
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
        if (!class_add_requested) return 0;
        class_add_requested(name);
        index = find_name();
        if (index < 0) return 0;
    }
    classes_->setCurrentRow(index);
    return index + 1;
}

void ClassificationAnnotationWidget::assign_current(int class_index) {
    if (current_row_ < 0 || current_row_ >= image_paths_.size() ||
        class_index < 0 || class_index >= class_names_.size()) return;
    class_labels_[current_row_] = class_index;
    if (class_assigned) class_assigned(image_paths_[current_row_], class_index);
    rebuild_images();
    images_->setCurrentRow(current_row_);
    classes_->setCurrentRow(class_index);
    load_row(current_row_);
}

void ClassificationAnnotationWidget::assign_selected(int class_index) {
    if (class_index < 0 || class_index >= class_names_.size()) return;
    const auto selected = images_->selectedItems();
    if (selected.size() <= 1) {
        assign_current(class_index);
        return;
    }
    const int current = current_row_;
    for (auto* item : selected) {
        const int row = images_->row(item);
        if (row < 0 || row >= image_paths_.size()) continue;
        class_labels_[row] = class_index;
        if (class_assigned) class_assigned(image_paths_[row], class_index);
    }
    rebuild_images();
    if (current >= 0 && current < images_->count()) images_->setCurrentRow(current);
    classes_->setCurrentRow(class_index);
    load_row(current);
}

} // namespace studio
