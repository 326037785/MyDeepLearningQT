#include "segmentation_annotation.h"
#include "result_presentation.h"
#include "segmentation_annotation_state.h"

#include "icon_provider.h"
#include "sample_presentation.h"
#include "sample_browser.h"
#include "ui_metrics.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QImageReader>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QSettings>
#include <QShortcut>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <cmath>
#include <iterator>
#include <QWheelEvent>

namespace studio {

namespace {

constexpr QColor kStrokePreviewColor(244, 198, 106, 135);

QVector<ResultPresentationItem> present_result(
    const jocelin::result::sg::SgResult& result, const QHash<int, QString>& class_names) {
    QVector<ResultPresentationItem> items;
    for (const jocelin::result::sg::SgResult::Component& component : result.components) {
        if (!component.interest) {
            continue;
        }
        ResultPresentationItem item;
        item.text = class_names.value(component.label, QObject::tr("Class %1").arg(component.label));
        item.attributes.append({QObject::tr("Class"), item.text});
        item.attributes.append({QObject::tr("Area"), QObject::tr("%1 px").arg(component.area)});
        item.attributes.append({QObject::tr("Confidence"),
                                QString::number(component.confidence * 100.0, 'f', 1) + "%"});
        item.hit_test = [region = component.region](const QPointF& position) {
            return region.contains(static_cast<int>(std::floor(position.y())),
                                   static_cast<int>(std::floor(position.x())));
        };
        items.append(std::move(item));
    }
    return items;
}

void clear_class_pixels(QImage& mask, int mask_value) {
    for (int y = 0; y < mask.height(); ++y) {
        const uchar* row = mask.constScanLine(y);
        if (std::find(row, row + mask.width(), mask_value) == row + mask.width()) {
            continue;
        }
        uchar* pixels = mask.scanLine(y);
        std::replace(pixels, pixels + mask.width(), static_cast<uchar>(mask_value), uchar{0});
    }
}

class MaskCommand final : public QUndoCommand {
public:
    MaskCommand(MaskCanvas* canvas, QImage before, QImage after)
        : canvas_(canvas), before_(std::move(before)), after_(std::move(after)) {}
    void undo() override { canvas_->apply_mask(before_, true); }
    void redo() override { canvas_->apply_mask(after_, true); }
    void remove_class_pixels(int mask_value) {
        clear_class_pixels(before_, mask_value);
        clear_class_pixels(after_, mask_value);
    }
private:
    MaskCanvas* canvas_;
    QImage before_;
    QImage after_;
};

bool matches_intervals(double value,
                       const std::vector<jocelin::geo::Interval>& intervals) {
    return std::any_of(intervals.begin(), intervals.end(),
                       [value](const auto& interval) {
                           return interval.contains(value);
                       });
}

class ThresholdDialog final : public QDialog {
public:
    ThresholdDialog(const QImage& roi, int& low_threshold, int& high_threshold,
                    bool& keep_outside, bool& remember, QWidget* parent = nullptr)
        : QDialog(parent), source_(roi.convertToFormat(QImage::Format_Grayscale8))
    {
        setWindowTitle(tr("Dual-threshold binarization"));
        setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        setModal(true);
        setMinimumWidth(440);
        auto* root = new QVBoxLayout(this);
        QLabel* introduction = new QLabel(
            tr("Preview the selected ROI, then confirm how pixels are assigned to the current class."),
            this);
        introduction->setWordWrap(true);
        QToolButton* help = new QToolButton(this);
        help->setObjectName("thresholdHelp");
        help->setIcon(style()->standardIcon(QStyle::SP_MessageBoxQuestion));
        help->setAutoRaise(true);
        help->setAccessibleName(tr("Threshold help"));
        help->setToolTip(tr("Selected pixels become the current class; other pixels inside the ROI become background. "
                            "Cancel leaves the mask unchanged."));
        QHBoxLayout* header = new QHBoxLayout;
        header->addWidget(introduction, 1);
        header->addWidget(help, 0, Qt::AlignTop);
        root->addLayout(header);
        preview_ = new QLabel(this);
        preview_->setObjectName("thresholdPreview");
        preview_->setProperty("imagePreview", true);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setMinimumHeight(220);
        root->addWidget(preview_, 1);
        auto* form = new QFormLayout;
        low_ = new QSpinBox(this);
        high_ = new QSpinBox(this);
        low_->setObjectName("thresholdLow");
        high_->setObjectName("thresholdHigh");
        low_->setRange(0, 255);
        high_->setRange(0, 255);
        low_->setValue(low_threshold);
        high_->setValue(high_threshold);
        mode_ = new QComboBox(this);
        mode_->setObjectName("thresholdMode");
        mode_->addItem(tr("Keep values inside [low, high]"), false);
        mode_->addItem(tr("Keep values outside [low, high]"), true);
        mode_->setCurrentIndex(keep_outside ? 1 : 0);
        form->addRow(tr("Low threshold"), low_);
        form->addRow(tr("High threshold"), high_);
        form->addRow(tr("Selection"), mode_);
        root->addLayout(form);
        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
        apply_button_ = buttons->button(QDialogButtonBox::Ok);
        apply_button_->setText(tr("Apply"));
        QCheckBox* remember_box = new QCheckBox(tr("Keep thresholds and direction"), this);
        remember_box->setObjectName("thresholdRememberParameters");
        remember_box->setToolTip(tr("Reuse these thresholds and inside/outside selection for the next sample."));
        remember_box->setChecked(remember);
        QHBoxLayout* footer = new QHBoxLayout;
        footer->addWidget(remember_box);
        footer->addStretch();
        footer->addWidget(buttons);
        root->addLayout(footer);
        connect(buttons, &QDialogButtonBox::accepted, this,
                [this, remember_box, &low_threshold, &high_threshold, &keep_outside, &remember] {
            remember = remember_box->isChecked();
            low_threshold = remember ? low_->value() : 64;
            high_threshold = remember ? high_->value() : 192;
            keep_outside = remember && keep_outside_range();
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(low_, qOverload<int>(&QSpinBox::valueChanged), this,
                [this](int) { refresh_preview(); });
        connect(high_, qOverload<int>(&QSpinBox::valueChanged), this,
                [this](int) { refresh_preview(); });
        connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this] { refresh_preview(); });
        refresh_preview();
    }

    std::vector<jocelin::geo::Interval> intervals() const {
        return {{static_cast<double>(low_->value()),
                 static_cast<double>(high_->value())}};
    }
    bool keep_outside_range() const { return mode_->currentData().toBool(); }

private:
    void refresh_preview() {
        if (source_.isNull()) return;
        const auto values = intervals();
        const bool valid = values.front().valid();
        apply_button_->setEnabled(valid);
        if (!valid) {
            preview_->clear();
            preview_->setText(tr("Low threshold must not exceed high threshold."));
            return;
        }
        QImage preview(source_.size(), QImage::Format_RGB888);
        const bool outside = keep_outside_range();
        for (int y = 0; y < source_.height(); ++y) {
            const auto* input = source_.constScanLine(y);
            auto* output = preview.scanLine(y);
            for (int x = 0; x < source_.width(); ++x) {
                const int offset = x * 3;
                const bool inside = matches_intervals(input[x], values);
                const uchar value = (outside ? !inside : inside) ? 255 : 0;
                output[offset] = value;
                output[offset + 1] = value;
                output[offset + 2] = value;
            }
        }
        preview_->setPixmap(QPixmap::fromImage(preview).scaled(
            QSize(400, 220), Qt::KeepAspectRatio, Qt::FastTransformation));
    }

    QImage source_;
    QLabel* preview_ = nullptr;
    QSpinBox* low_ = nullptr;
    QSpinBox* high_ = nullptr;
    QComboBox* mode_ = nullptr;
    QPushButton* apply_button_ = nullptr;
};

QColor class_color(int value) {
    static const QColor colors[]{
        QColor(0, 0, 0, 0), QColor(31, 138, 158), QColor(209, 132, 50),
        QColor(69, 140, 99), QColor(195, 70, 85), QColor(91, 102, 173),
        QColor(181, 94, 150), QColor(118, 148, 45)};
    return colors[value % static_cast<int>(std::size(colors))];
}

constexpr int kAnnotationViewportMargin = 24;

QSize annotation_viewport_size(const QSize& size) {
    return {
        std::max(1, size.width() - 2 * kAnnotationViewportMargin),
        std::max(1, size.height() - 2 * kAnnotationViewportMargin)};
}

QPointF annotation_viewport_point(const QPointF& point) {
    return point - QPointF(kAnnotationViewportMargin, kAnnotationViewportMargin);
}
QImage neutral_mask_overlay(const QImage& source) {
    if (source.isNull()) return {};
    const QImage labels = source.convertToFormat(QImage::Format_Grayscale8);
    QImage result(labels.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    for (int y = 0; y < labels.height(); ++y) {
        const uchar* input = labels.constScanLine(y);
        auto* output = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < labels.width(); ++x)
            if (input[x] != 0) output[x] = QColor(235, 244, 242, 210).rgba();
    }
    return result;
}

} // namespace

MaskCanvas::MaskCanvas(QWidget* parent)
    : QWidget(parent), overlay_opacity_(0.34), undo_stack_(new QUndoStack(this)) {
    setObjectName("segmentationMaskCanvas");
    setMinimumSize(280, 160);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    connect(undo_stack_, &QUndoStack::canUndoChanged, this, [this](bool) {
        if (undo_state_changed) undo_state_changed(can_undo(), can_redo());
    });
    connect(undo_stack_, &QUndoStack::canRedoChanged, this, [this](bool) {
        if (undo_state_changed) undo_state_changed(can_undo(), can_redo());
    });
}

bool MaskCanvas::load(const QString& image_path, const QString& mask_path, QString* error) {
    clear_transform_edit();
    QImage image(image_path);
    if (image.isNull()) { if (error) *error = tr("Cannot read image: %1").arg(image_path); return false; }
    QImage mask;
    if (QFile::exists(mask_path)) {
        mask = QImage(mask_path).convertToFormat(QImage::Format_Grayscale8);
        if (mask.size() != image.size()) {
            if (error) *error = tr("Mask size does not match image: %1").arg(mask_path);
            return false;
        }
    } else {
        mask = QImage(image.size(), QImage::Format_Grayscale8);
        mask.fill(0);
    }
    image_ = image.convertToFormat(QImage::Format_RGB888);
    mask_ = mask;
    saved_mask_ = mask_;
    prediction_overlay_ = {};
    result_items_.clear();
    if (result_popup_) {
        result_popup_->close();
    }
    heatmap_overlay_ = {};
    mask_overlay_ = {};
    viewport_.reset();
    pan_.end();
    undo_stack_->clear();
    interaction_.reset();
    stroke_before_ = {};
    stroke_preview_ = {};
    rebuild_overlay();
    update();
    if (dirty_changed) dirty_changed(false);
    if (zoom_changed) zoom_changed(zoom_percent());
    if (undo_state_changed) undo_state_changed(false, false);
    return true;
}

void MaskCanvas::mark_saved() {
    saved_mask_ = mask_;
    if (dirty_changed) {
        dirty_changed(false);
    }
}

void MaskCanvas::clear() {
    clear_transform_edit();
    image_ = {};
    mask_ = {};
    saved_mask_ = {};
    overlay_ = {};
    prediction_overlay_ = {};
    result_items_.clear();
    if (result_popup_) {
        result_popup_->close();
    }
    heatmap_overlay_ = {};
    mask_overlay_ = {};
    viewport_.reset();
    pan_.end();
    undo_stack_->clear();
    interaction_.reset();
    stroke_before_ = {};
    stroke_preview_ = {};
    update();
    if (dirty_changed) dirty_changed(false);
    if (zoom_changed) zoom_changed(zoom_percent());
    if (undo_state_changed) undo_state_changed(false, false);
}

bool MaskCanvas::save(const QString& mask_path, QString* error) {
    if (mask_.isNull()) { if (error) *error = tr("No mask is loaded"); return false; }
    if (!QDir().mkpath(QFileInfo(mask_path).absolutePath())) {
        if (error) *error = tr("Cannot create mask directory"); return false;
    }
    QSaveFile file(mask_path);
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
    QImageWriter writer(&file, "png");
    if (!writer.write(mask_) || !file.commit()) {
        if (error) *error = writer.errorString().isEmpty() ? file.errorString() : writer.errorString();
        return false;
    }
    mark_saved();
    return true;
}

void MaskCanvas::undo() {
    clear_transform_edit();
    undo_stack_->undo();
}

void MaskCanvas::redo() {
    clear_transform_edit();
    undo_stack_->redo();
}
bool MaskCanvas::can_undo() const { return undo_stack_->canUndo(); }
bool MaskCanvas::can_redo() const { return undo_stack_->canRedo(); }

void MaskCanvas::set_class_display(const QHash<int, QColor>& colors, const QSet<int>& hidden) {
    class_colors_ = colors;
    hidden_class_values_ = hidden;
    rebuild_overlay();
    update();
}

void MaskCanvas::remove_class_pixels(int mask_value) {
    clear_transform_edit();
    if (interaction_.painting() && !stroke_before_.isNull()) {
        mask_ = stroke_before_;
    }
    interaction_.reset();
    stroke_before_ = {};
    stroke_preview_ = {};
    for (int index = 0; index < undo_stack_->count(); ++index) {
        // This stack contains only commands created by finish_edit().
        auto* command = const_cast<MaskCommand*>(
            static_cast<const MaskCommand*>(undo_stack_->command(index)));
        command->remove_class_pixels(mask_value);
    }
    clear_class_pixels(mask_, mask_value);
    clear_class_pixels(saved_mask_, mask_value);
    rebuild_overlay();
    update();
    if (polygon_active_changed) {
        polygon_active_changed(false);
    }
    if (dirty_changed) {
        dirty_changed(is_dirty());
    }
}

void MaskCanvas::apply_mask(const QImage& mask, bool dirty) {
    mask_ = mask;
    if (!dirty) {
        saved_mask_ = mask;
    }
    rebuild_overlay();
    update();
    if (dirty_changed) {
        dirty_changed(is_dirty());
    }
}

void MaskCanvas::apply_threshold_roi(
    const QRect& requested_roi,
    const std::vector<jocelin::geo::Interval>& intervals,
    bool keep_outside_range)
{
    if (image_.isNull() || mask_.isNull() ||
        intervals.empty() ||
        std::any_of(intervals.begin(), intervals.end(),
                    [](const jocelin::geo::Interval& interval) { return !interval.valid(); })) {
        return;
    }
    const QRect roi = requested_roi.normalized().intersected(image_.rect());
    if (roi.isEmpty()) {
        return;
    }
    const QImage grayscale = image_.convertToFormat(QImage::Format_Grayscale8);
    if (class_value_ == 0) {
        bool has_foreground = false;
        for (int y = roi.top(); y <= roi.bottom() && !has_foreground; ++y) {
            const uchar* row = grayscale.constScanLine(y);
            has_foreground = std::any_of(row + roi.left(), row + roi.right() + 1,
                [&](uchar pixel) { return matches_intervals(pixel, intervals) != keep_outside_range; });
        }
        if (!has_foreground) {
            return;
        }
    }
    const int value = resolve_class();
    if (value <= 0) {
        return;
    }
    const QImage before = mask_;
    for (int y = roi.top(); y <= roi.bottom(); ++y) {
        const uchar* source = grayscale.constScanLine(y);
        uchar* target = mask_.scanLine(y);
        for (int x = roi.left(); x <= roi.right(); ++x) {
            const bool inside = matches_intervals(source[x], intervals);
            const bool selected = keep_outside_range ? !inside : inside;
            target[x] = static_cast<uchar>(selected ? value : 0);
        }
    }
    finish_edit(before);
}

void MaskCanvas::apply_ellipse(const QRect& requested_roi) {
    if (mask_.isNull()) {
        return;
    }
    const QRect roi = requested_roi.normalized();
    if (roi.isEmpty() || !roi.intersects(mask_.rect())) {
        return;
    }
    const int value = resolve_class();
    if (value <= 0) {
        return;
    }
    const QImage before = mask_;
    QPainter painter(&mask_);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(value, value, value));
    painter.drawEllipse(roi);
    painter.end();
    finish_edit(before);
}

void MaskCanvas::set_prediction_overlay(const QImage& image,
                                        const QVector<ResultPresentationItem>& items) {
    if (result_popup_) {
        result_popup_->close();
    }
    prediction_overlay_ = image;
    result_items_ = items;
    update();
}

void MaskCanvas::set_heatmap_overlay(const QImage& image) {
    heatmap_overlay_ = image;
    update();
}

void MaskCanvas::set_mask_overlay(const QImage& image) {
    mask_overlay_ = image;
    update();
}

QRectF MaskCanvas::image_target() const {
    const QRectF target = viewport_.target_rect(
        image_.size(), annotation_viewport_size(size()));
    return target.translated(kAnnotationViewportMargin, kAnnotationViewportMargin);
}

QPoint MaskCanvas::image_point(const QPoint& widget_point) const {
    const QPointF position = image_position(widget_point);
    if (image_.isNull() || position.x() < 0.0 || position.y() < 0.0 ||
        position.x() >= image_.width() || position.y() >= image_.height())
        return {-1, -1};
    return {static_cast<int>(std::floor(position.x())),
            static_cast<int>(std::floor(position.y()))};
}

QPoint MaskCanvas::annotation_point(const QPoint& widget_point) const {
    const QRectF target = image_target();
    if (target.isEmpty() ||
        !target.adjusted(-kAnnotationViewportMargin, -kAnnotationViewportMargin,
                         kAnnotationViewportMargin, kAnnotationViewportMargin)
             .contains(widget_point))
        return {-1, -1};
    const QPointF position = image_position(widget_point);
    return {static_cast<int>(std::floor(position.x())),
            static_cast<int>(std::floor(position.y()))};
}

QPointF MaskCanvas::image_position(const QPointF& widget_point) const {
    const QRectF target = image_target();
    if (target.isEmpty() || image_.isNull()) return {-1.0, -1.0};
    return {(widget_point.x() - target.left()) * image_.width() / target.width(),
            (widget_point.y() - target.top()) * image_.height() / target.height()};
}

QPointF MaskCanvas::widget_position(const QPointF& image_point) const {
    const QRectF target = image_target();
    if (target.isEmpty() || image_.isNull()) return {};
    return {target.left() + image_point.x() * target.width() / image_.width(),
            target.top() + image_point.y() * target.height() / image_.height()};
}

void MaskCanvas::set_tool(AnnotationTool tool) {
    const bool polygon_was_active = interaction_.polygon_active();
    if (interaction_.painting() && !stroke_before_.isNull() &&
        (interaction_.tool() == AnnotationTool::Brush || interaction_.tool() == AnnotationTool::Eraser)) {
        mask_ = stroke_before_;
        rebuild_overlay();
    }
    if (tool != interaction_.tool()) {
        clear_transform_edit();
    }
    interaction_.set_tool(tool);
    if (!stroke_preview_.isNull()) {
        stroke_preview_ = {};
        stroke_before_ = {};
    }
    if (polygon_was_active && !interaction_.polygon_active()) {
        stroke_before_ = {};
        if (polygon_active_changed) {
            polygon_active_changed(false);
        }
    }
    setCursor(tool == AnnotationTool::Pan ? Qt::OpenHandCursor :
              tool == AnnotationTool::TransformSelection ? Qt::ArrowCursor :
              tool == AnnotationTool::Pick ? Qt::PointingHandCursor : Qt::CrossCursor);
    update();
}

int MaskCanvas::zoom_percent() const {
    return qRound(viewport_.zoom() * 100.0);
}

void MaskCanvas::zoom_by(double factor) {
    if (image_.isNull()) return;
    const QSize viewport_size = annotation_viewport_size(size());
    const QPointF center(viewport_size.width() / 2.0, viewport_size.height() / 2.0);
    if (viewport_.zoom_at(center, factor, image_.size(), viewport_size)) {
        update();
        if (zoom_changed) zoom_changed(zoom_percent());
    }
}

void MaskCanvas::reset_view() {
    viewport_.reset();
    update();
    if (zoom_changed) zoom_changed(zoom_percent());
}

void MaskCanvas::wheelEvent(QWheelEvent* event) {
    if (image_.isNull()) return;

    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    if (viewport_.zoom_at(
            annotation_viewport_point(event->position()), factor, image_.size(),
            annotation_viewport_size(size()))) {
        update();
        if (zoom_changed) zoom_changed(zoom_percent());
    }
    event->accept();
}

void MaskCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#18292D"));
    const QRectF target = image_target();
    if (target.isEmpty()) {
        painter.setPen(QColor("#B8C8C6")); painter.drawText(rect(), Qt::AlignCenter, tr("Select an image")); return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, image_);
    if (heatmap_visible_ && !heatmap_overlay_.isNull()) {
        painter.setOpacity(overlay_opacity_);
        painter.drawImage(target, heatmap_overlay_);
    }
    if (prediction_visible_ && !prediction_overlay_.isNull()) {
        painter.setOpacity(overlay_opacity_);
        painter.drawImage(target, prediction_overlay_);
    }
    if (overlay_visible_) {
        painter.setOpacity(overlay_opacity_);
        painter.drawImage(target, overlay_);
    }
    if (!stroke_preview_.isNull()) {
        painter.setOpacity(1.0);
        painter.drawImage(target, stroke_preview_);
    }
    if (mask_visible_ && !mask_overlay_.isNull()) {
        painter.setOpacity(std::min(1.0, overlay_opacity_ + 0.25));
        painter.drawImage(target, mask_overlay_);
    }
    painter.setOpacity(1.0);

    auto map_point = [target, this](const QPoint& point) {
        return QPointF(target.left() + (point.x() + 0.5) * target.width() / image_.width(),
                       target.top() + (point.y() + 0.5) * target.height() / image_.height());
    };
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor("#F4C66A"), 2, Qt::DashLine));
    painter.setBrush(QColor(244, 198, 106, 45));
    if ((interaction_.tool() == AnnotationTool::Rectangle ||
         interaction_.tool() == AnnotationTool::Ellipse ||
         interaction_.tool() == AnnotationTool::ThresholdRoi) &&
        interaction_.painting() && interaction_.shape_start().x() >= 0 &&
        interaction_.hover_point().x() >= 0) {
        const QRectF shape(
            QRectF(map_point(interaction_.shape_start()),
                   map_point(interaction_.hover_point())).normalized());
        if (interaction_.tool() == AnnotationTool::Ellipse) {
            painter.drawEllipse(shape);
        } else {
            painter.drawRect(shape);
        }
    }
    if (interaction_.tool() == AnnotationTool::Polygon && interaction_.polygon_active()) {
        QPolygonF preview;
        for (const QPoint& point : interaction_.polygon_points()) {
            preview << map_point(point);
        }
        if (interaction_.hover_point().x() >= 0) {
            preview << map_point(interaction_.hover_point());
        }
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(preview);
        for (const QPoint& point : interaction_.polygon_points()) {
            painter.drawEllipse(map_point(point), 3.5, 3.5);
        }
    }
    if (transform_region_) {
        paint_transform_controls(painter);
    }
}

void MaskCanvas::mousePressEvent(QMouseEvent* event) {
    if (image_.isNull()) {
        return;
    }
    setFocus(Qt::MouseFocusReason);
    const AnnotationTool tool = interaction_.tool();

    if (event->button() == Qt::LeftButton && tool == AnnotationTool::TransformSelection) {
        if (!overlay_visible_) {
            event->accept();
            return;
        }
        if (transform_region_) {
            const TransformHandle handle = transform_handle_at(event->pos());
            if (handle != TransformHandle::None) {
                begin_transform_drag(handle, image_position(event->pos()));
                event->accept();
                return;
            }
            clear_transform_edit();
            event->accept();
        }
        return;
    }
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && tool == AnnotationTool::Pan)) {
        pan_.begin(event->pos());
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() == Qt::RightButton && tool == AnnotationTool::Polygon) {
        finish_polygon();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }

    const QPoint image_pixel = image_point(event->pos());
    if (tool == AnnotationTool::Pick) {
        const AnnotationGesture gesture = interaction_.press(image_pixel);
        if (gesture.kind != AnnotationGesture::Kind::Point) {
            return;
        }
        const int value = mask_.constScanLine(gesture.point.y())[gesture.point.x()];
        if (class_picked) {
            class_picked(value);
        }
        return;
    }
    if (!editing_enabled_) {
        return;
    }
    if (tool == AnnotationTool::Fill) {
        const AnnotationGesture gesture = interaction_.press(image_pixel);
        if (gesture.kind == AnnotationGesture::Kind::Point) {
            fill_at(gesture.point);
        }
        return;
    }
    const QPoint point = annotation_point(event->pos());
    if (point.x() < 0) {
        return;
    }
    const bool polygon_starting = tool == AnnotationTool::Polygon &&
        !interaction_.polygon_active();
    if (tool == AnnotationTool::Brush || tool == AnnotationTool::Eraser) {
        stroke_before_ = mask_;
    }
    const AnnotationGesture gesture = interaction_.press(point);
    if (tool == AnnotationTool::Polygon) {
        update();
        if (polygon_starting && polygon_active_changed) {
            polygon_active_changed(true);
        }
    } else if (gesture.kind == AnnotationGesture::Kind::StrokeStart) {
        if (tool == AnnotationTool::Brush && class_value_ <= 0) {
            stroke_preview_ = QImage(mask_.size(), QImage::Format_ARGB32_Premultiplied);
            stroke_preview_.fill(Qt::transparent);
        }
        paint_at(gesture.point);
    }
}

void MaskCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (pan_.active()) {
        viewport_.pan_by(pan_.move(event->pos()));
        update();
        return;
    }
    const AnnotationTool tool = interaction_.tool();
    const bool extended_annotation_tool =
        tool == AnnotationTool::Brush || tool == AnnotationTool::Eraser ||
        tool == AnnotationTool::Polygon || tool == AnnotationTool::Rectangle ||
        tool == AnnotationTool::Ellipse || tool == AnnotationTool::ThresholdRoi;
    const QPoint hover = extended_annotation_tool
        ? annotation_point(event->pos()) : image_point(event->pos());
    const AnnotationGesture gesture = interaction_.move(hover);
    if (transform_.dragging()) {
        update_transform_drag(image_position(event->pos()));
        return;
    }
    if (tool == AnnotationTool::TransformSelection && transform_region_) {
        switch (transform_handle_at(event->pos())) {
        case TransformHandle::TopLeft:
        case TransformHandle::BottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case TransformHandle::TopRight:
        case TransformHandle::BottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case TransformHandle::Left:
        case TransformHandle::Right:
            setCursor(Qt::SizeHorCursor);
            break;
        case TransformHandle::Top:
        case TransformHandle::Bottom:
            setCursor(Qt::SizeVerCursor);
            break;
        case TransformHandle::Rotate:
            setCursor(Qt::CrossCursor);
            break;
        case TransformHandle::Body:
            setCursor(Qt::SizeAllCursor);
            break;
        case TransformHandle::None:
            setCursor(Qt::ArrowCursor);
            break;
        }
        return;
    }
    if (gesture.kind == AnnotationGesture::Kind::StrokeSegment) {
        paint_segment(gesture.from, gesture.point);
    } else if (tool == AnnotationTool::Rectangle || tool == AnnotationTool::Ellipse ||
               tool == AnnotationTool::ThresholdRoi || tool == AnnotationTool::Polygon) {
        update();
    }
}

void MaskCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && transform_.dragging()) {
        finish_transform_drag();
        return;
    }
    const AnnotationTool tool = interaction_.tool();
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && tool == AnnotationTool::Pan)) {
        pan_.end();
        setCursor(tool == AnnotationTool::Pan ? Qt::OpenHandCursor :
                  tool == AnnotationTool::TransformSelection ? Qt::ArrowCursor : Qt::CrossCursor);
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const AnnotationGesture gesture = interaction_.release();
    if (gesture.kind == AnnotationGesture::Kind::None) {
        return;
    }
    QImage before = std::exchange(stroke_before_, {});
    if (gesture.kind == AnnotationGesture::Kind::ThresholdRoi) {
        const QRect roi = gesture.rect.intersected(image_.rect());
        update();
        if (!roi.isEmpty() && threshold_roi_selected) {
            threshold_roi_selected(roi, image_.copy(roi));
        }
        return;
    }
    if (gesture.kind == AnnotationGesture::Kind::Ellipse) {
        apply_ellipse(gesture.rect);
        update();
        return;
    }
    if (gesture.kind == AnnotationGesture::Kind::Rectangle) {
        update();
        if (!gesture.rect.intersects(mask_.rect())) {
            return;
        }
        const int value = resolve_class();
        if (value <= 0) {
            return;
        }
        before = mask_;
        QPainter painter(&mask_);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(value, value, value));
        painter.drawRect(gesture.rect);
    } else if (!stroke_preview_.isNull()) {
        const QImage preview = std::exchange(stroke_preview_, {});
        update();
        bool has_pixels = false;
        for (int y = 0; y < preview.height() && !has_pixels; ++y) {
            const auto* row = reinterpret_cast<const QRgb*>(preview.constScanLine(y));
            has_pixels = std::any_of(row, row + preview.width(), [](QRgb pixel) { return qAlpha(pixel) > 0; });
        }
        if (!has_pixels) {
            return;
        }
        const int value = resolve_class();
        if (value <= 0) {
            return;
        }
        for (int y = 0; y < mask_.height(); ++y) {
            const auto* source = reinterpret_cast<const QRgb*>(preview.constScanLine(y));
            uchar* target = mask_.scanLine(y);
            for (int x = 0; x < mask_.width(); ++x) {
                if (qAlpha(source[x]) > 0) {
                    target[x] = static_cast<uchar>(value);
                }
            }
        }
    }
    finish_edit(before);
}

void MaskCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !overlay_visible_ && prediction_visible_ &&
        (interaction_.tool() == AnnotationTool::TransformSelection ||
         interaction_.tool() == AnnotationTool::Pan)) {
        if (result_popup_) {
            result_popup_->close();
        }
        const QPoint point = image_point(event->pos());
        if (point.x() >= 0) {
            const int index = result_item_at(result_items_, image_position(event->pos()));
            if (index >= 0) {
                result_popup_ = show_result_properties(this, event->globalPos(), result_items_[index]);
            }
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && interaction_.tool() == AnnotationTool::Polygon) {
        finish_polygon();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton &&
        interaction_.tool() == AnnotationTool::TransformSelection && overlay_visible_ &&
        editing_enabled_) {
        const QPoint point = image_point(event->pos());
        if (point.x() >= 0 && begin_transform_edit(point)) {
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void MaskCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && !stroke_preview_.isNull()) {
        interaction_.reset();
        stroke_preview_ = {};
        stroke_before_ = {};
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && transform_region_) {
        clear_transform_edit();
        event->accept();
        return;
    }
    if (interaction_.tool() == AnnotationTool::Polygon &&
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        if (interaction_.polygon_points().size() >= 3) {
            finish_polygon();
        }
        event->accept();
        return;
    }
    if (interaction_.tool() == AnnotationTool::Polygon && event->key() == Qt::Key_Escape) {
        cancel_polygon();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

int MaskCanvas::resolve_class() const {
    if (!editing_enabled_) {
        return 0;
    }
    return class_value_ > 0 ? class_value_ : (choose_class ? choose_class() : 0);
}

void MaskCanvas::paint_at(const QPoint& point) {
    if (point.x() < 0 || mask_.isNull()) {
        return;
    }
    QPainter painter(stroke_preview_.isNull() ? &mask_ : &stroke_preview_);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    const int value = interaction_.tool() == AnnotationTool::Eraser ? 0 : class_value_;
    painter.setBrush(stroke_preview_.isNull() ? QColor(value, value, value) : kStrokePreviewColor);
    painter.drawEllipse(point, interaction_.brush_size() / 2, interaction_.brush_size() / 2);
    painter.end();
    if (stroke_preview_.isNull()) {
        rebuild_overlay();
    }
    update();
}

void MaskCanvas::paint_segment(const QPoint& from, const QPoint& to) {
    if (from.x() < 0 || to.x() < 0 || mask_.isNull()) {
        return;
    }
    const int value = interaction_.tool() == AnnotationTool::Eraser ? 0 : class_value_;
    QPainter painter(stroke_preview_.isNull() ? &mask_ : &stroke_preview_);
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QColor color = stroke_preview_.isNull() ? QColor(value, value, value) : kStrokePreviewColor;
    painter.setPen(QPen(color, interaction_.brush_size(),
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(from, to);
    painter.end();
    if (stroke_preview_.isNull()) {
        rebuild_overlay();
    }
    update();
}

void MaskCanvas::fill_at(const QPoint& point) {
    if (mask_.isNull() || point.x() < 0) {
        return;
    }
    const int value = resolve_class();
    if (value <= 0) {
        return;
    }
    const QImage before = mask_;
    const uchar source = mask_.constScanLine(point.y())[point.x()];
    const uchar target = static_cast<uchar>(value);
    if (source == target) {
        return;
    }
    QList<QPoint> pending{point};
    mask_.scanLine(point.y())[point.x()] = target;
    while (!pending.isEmpty()) {
        const QPoint current = pending.takeLast();
        const QPoint neighbours[] = {{current.x() - 1, current.y()}, {current.x() + 1, current.y()},
                                     {current.x(), current.y() - 1}, {current.x(), current.y() + 1}};
        for (const QPoint& next : neighbours) {
            if (next.x() < 0 || next.y() < 0 || next.x() >= mask_.width() || next.y() >= mask_.height()) {
                continue;
            }
            uchar& value = mask_.scanLine(next.y())[next.x()];
            if (value != source) {
                continue;
            }
            value = target;
            pending.append(next);
        }
    }
    finish_edit(before);
}

bool MaskCanvas::begin_transform_edit(const QPoint& point) {
    if (mask_.isNull() || !mask_.rect().contains(point)) {
        return false;
    }
    const int label = mask_.constScanLine(point.y())[point.x()];
    if (label <= 0) {
        return false;
    }

    std::optional<LabelRegionSelection> selection = AnnotationRegionEditService::select_connected_region(
        mask_, point, label);
    if (!selection) {
        return false;
    }
    transform_region_ = std::move(selection);
    transform_base_mask_ = mask_;
    transform_drag_before_ = {};
    transform_.reset();
    setCursor(Qt::SizeAllCursor);
    update();
    return true;
}

void MaskCanvas::clear_transform_edit() {
    const bool was_active = transform_region_.has_value() || transform_.dragging();
    if (transform_.dragging() && !transform_drag_before_.isNull()) {
        mask_ = transform_drag_before_;
        rebuild_overlay();
    }
    transform_region_.reset();
    transform_base_mask_ = {};
    transform_drag_before_ = {};
    transform_.reset();
    if (interaction_.tool() == AnnotationTool::TransformSelection) {
        setCursor(Qt::ArrowCursor);
    }
    if (was_active) {
        update();
    }
}

QPolygonF MaskCanvas::transform_frame() const {
    if (!transform_region_) {
        return {};
    }
    const QRectF bounds = AnnotationRegionEditService::region_bounds(*transform_region_);
    if (bounds.isEmpty()) {
        return {};
    }
    return transform_.frame(bounds);
}

void MaskCanvas::paint_transform_controls(QPainter& painter) const {
    const QPolygonF frame = transform_frame();
    if (frame.size() != 4) {
        return;
    }
    QPolygonF widget_frame;
    for (const QPointF& point : frame) {
        widget_frame << widget_position(point);
    }
    SelectionTransformInteraction::paint_controls(painter, widget_frame);
}

MaskCanvas::TransformHandle MaskCanvas::transform_handle_at(
    const QPointF& widget_point) const {
    const QPolygonF frame = transform_frame();
    if (frame.size() != 4) {
        return TransformHandle::None;
    }
    QPolygonF widget_frame;
    for (const QPointF& point : frame) {
        widget_frame << widget_position(point);
    }
    return SelectionTransformInteraction::handle_at(
        widget_point, widget_frame,
        transform_region_contains(image_position(widget_point)));
}

bool MaskCanvas::transform_region_contains(const QPointF& image_point) const {
    if (!transform_region_) {
        return false;
    }
    bool invertible = false;
    const QTransform inverse = transform_.matrix().inverted(&invertible);
    if (!invertible) {
        return false;
    }
    const QPointF source_point = inverse.map(image_point);
    const int x = static_cast<int>(std::floor(source_point.x()));
    const int y = static_cast<int>(std::floor(source_point.y()));
    return transform_region_->region.contains(y, x);
}

void MaskCanvas::begin_transform_drag(TransformHandle handle, const QPointF& point) {
    if (!transform_region_ || transform_base_mask_.isNull()) {
        return;
    }
    transform_drag_before_ = mask_;
    transform_.begin_drag(handle, point);
}

void MaskCanvas::update_transform_drag(const QPointF& point) {
    if (!transform_region_ || transform_base_mask_.isNull()) {
        return;
    }
    const QRectF bounds = AnnotationRegionEditService::region_bounds(*transform_region_);
    if (!transform_.update_drag(bounds, point)) {
        return;
    }
    mask_ = AnnotationRegionEditService::transformed_mask(
        transform_base_mask_, *transform_region_, transform_.matrix());
    rebuild_overlay();
    update();
}

void MaskCanvas::finish_transform_drag() {
    if (!transform_.dragging()) {
        return;
    }
    transform_.end_drag();
    const QImage before = transform_drag_before_;
    transform_drag_before_ = {};
    finish_edit(before);
}

void MaskCanvas::finish_edit(const QImage& before) {
    if (before.isNull() || mask_ == before) {
        update();
        return;
    }
    undo_stack_->push(new MaskCommand(this, before, mask_));
}

void MaskCanvas::finish_polygon() {
    const AnnotationGesture gesture = interaction_.finish_polygon();
    if (gesture.kind != AnnotationGesture::Kind::Polygon) {
        update();
        return;
    }
    update();
    if (polygon_active_changed) {
        polygon_active_changed(false);
    }
    if (!gesture.polygon.boundingRect().intersects(mask_.rect())) {
        return;
    }
    const int value = resolve_class();
    if (value <= 0) {
        return;
    }
    const QImage before = mask_;
    QPainter painter(&mask_);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(value, value, value));
    painter.drawPolygon(gesture.polygon);
    painter.end();
    finish_edit(before);
}

void MaskCanvas::cancel_polygon() {
    const bool active = interaction_.polygon_active();
    interaction_.cancel_polygon();
    stroke_before_ = {};
    update();
    if (active && polygon_active_changed) {
        polygon_active_changed(false);
    }
}

void MaskCanvas::rebuild_overlay() {
    if (mask_.isNull()) { overlay_ = {}; return; }
    overlay_ = QImage(mask_.size(), QImage::Format_ARGB32_Premultiplied);
    overlay_.fill(Qt::transparent);
    for (int y = 0; y < mask_.height(); ++y) {
        const uchar* source = mask_.constScanLine(y);
        QRgb* destination = reinterpret_cast<QRgb*>(overlay_.scanLine(y));
        for (int x = 0; x < mask_.width(); ++x) {
            const int value = source[x];
            destination[x] = hidden_class_values_.contains(value) ? 0 :
                class_colors_.value(value, class_color(value)).rgba();
        }
    }
}

SegmentationAnnotationWidget::SegmentationAnnotationWidget(QWidget* parent, QTabWidget* inspector) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(ThemeTokens::Space1);

    auto* context_bar = new QFrame(this);
    context_bar->setObjectName("annotationContextBar");
    auto* context = new QHBoxLayout(context_bar);
    context->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                                ThemeTokens::Space2, ThemeTokens::Space1);
    context->setSpacing(ThemeTokens::Space1);
    auto make_icon_button = [](QWidget* parent, const QString& icon,
                               const QString& text, const QString& object_name,
                               bool checkable = false) {
        auto* button = new QToolButton(parent);
        button->setObjectName(object_name);
        button->setCheckable(checkable);
        button->setAutoRaise(false);
        IconProvider::configure(button, icon, text);
        return button;
    };
    auto* previous = make_icon_button(context_bar, "previous", tr("Previous sample"),
                                      "annotationPreviousButton");
    auto* next = make_icon_button(context_bar, "next", tr("Next sample"),
                                  "annotationNextButton");
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
    auto* toggle_samples = make_panel_toggle(
        context_bar, tr("Samples"), "annotationToggleSamplesButton");
    auto* toggle_classes = make_panel_toggle(
        context_bar, tr("Inspector"), "annotationToggleClassesButton");
    toggle_samples_ = toggle_samples;
    toggle_classes_ = toggle_classes;
    context_ = new QLabel(tr("No dataset selected"), context_bar);
    context_->setObjectName("annotationContextLabel");
    context_->setProperty("annotationContext", true);
    context_->setMinimumWidth(0);
    context_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    state_ = new QLabel(tr("Ready"), context_bar);
    state_->setObjectName("annotationState");
    state_->setProperty("muted", true);
    state_->setMinimumWidth(0);
    auto* undo = make_icon_button(context_bar, "undo", tr("Undo (Ctrl+Z)"),
                                  "segmentationUndoButton");
    auto* redo = make_icon_button(context_bar, "redo", tr("Redo (Ctrl+Y)"),
                                  "segmentationRedoButton");
    undo->setEnabled(false);
    redo->setEnabled(false);
    auto* save = make_icon_button(context_bar, "save", tr("Save mask"),
                                  "segmentationSaveMaskButton");
    save->setObjectName("segmentationSaveMaskButton");
    save->setProperty("accentSecondary", true);
    auto* zoom_out = make_icon_button(context_bar, "zoom-out", tr("Zoom out"),
                                      "annotationZoomOutButton");
    auto* zoom_value = new QLabel(tr("100%"), context_bar);
    zoom_value->setObjectName("annotationZoomValue");
    zoom_value->setMinimumWidth(QFontMetrics(zoom_value->font()).horizontalAdvance("1000%"));
    zoom_value->setAlignment(Qt::AlignCenter);
    auto* zoom_in = make_icon_button(context_bar, "zoom-in", tr("Zoom in"),
                                     "annotationZoomInButton");
    auto* zoom_fit = make_icon_button(context_bar, "fit", tr("Fit image"),
                                      "annotationZoomFitButton");
    context->addWidget(previous); context->addWidget(next);
    context->addWidget(toggle_samples); context->addWidget(toggle_classes);
    context->addWidget(context_, 1);
    context->addWidget(state_); context->addSpacing(ThemeTokens::Space1);
    context->addWidget(zoom_out); context->addWidget(zoom_value); context->addWidget(zoom_in); context->addWidget(zoom_fit);
    context->addWidget(undo); context->addWidget(redo); context->addWidget(save);
    root->addWidget(context_bar);

    auto* content = new QSplitter(Qt::Horizontal, this);
    content_splitter_ = content;
    content->setObjectName("annotationContentSplitter");
    auto* samples_panel = new QFrame(content);
    samples_panel->setObjectName("annotationSamplesPanel");
    samples_panel->setMinimumWidth(0);
    auto* samples_layout = new QVBoxLayout(samples_panel);
    samples_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                                       ThemeTokens::Space2, ThemeTokens::Space2);
    samples_layout->setSpacing(ThemeTokens::Space1);
    samples_title_ = new QLabel(tr("SAMPLES"), samples_panel);
    samples_title_->setProperty("eyebrow", true);
    samples_layout->addWidget(samples_title_);
    quality_ = new QLabel(samples_panel);
    quality_->setObjectName("annotationQualitySummary");
    quality_->setProperty("meta", true);
    quality_->setWordWrap(true);
    quality_->hide();
    samples_layout->addWidget(quality_);
    images_ = new SampleBrowser(samples_panel);
    images_->setIconSize(QSize(56, 42));
    images_->setObjectName("annotationImageList");
    samples_layout->addWidget(images_, 1);

    auto* viewport_panel = new QFrame(content);
    viewport_panel->setObjectName("annotationViewportPanel");
    auto* viewport_layout = new QVBoxLayout(viewport_panel);
    viewport_layout->setContentsMargins(0, 0, 0, 0);
    viewport_layout->setSpacing(0);

    auto* tools_panel = new QFrame(viewport_panel);
    tools_panel->setObjectName("annotationToolBar");
    auto* tools = new QHBoxLayout(tools_panel);
    tools->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                              ThemeTokens::Space2, ThemeTokens::Space1);
    tools->setSpacing(ThemeTokens::Space1);
    auto* pan = make_icon_button(tools_panel, "pan", tr("Select and pan"),
                                 "segmentationPanButton", true);
    auto* transform_region = make_icon_button(
        tools_panel, "move", tr("Transform region"),
        "segmentationTransformRegionButton", true);
    QToolButton* brush = make_icon_button(tools_panel, "brush", tr("Brush"),
                              "segmentationBrushButton", true);
    auto* eraser = make_icon_button(tools_panel, "eraser", tr("Eraser"),
                                    "segmentationEraserButton", true);
    auto* rectangle = make_icon_button(tools_panel, "rectangle", tr("Rectangle"),
                                       "segmentationRectangleButton", true);
    auto* ellipse = make_icon_button(tools_panel, "ellipse", tr("Ellipse"),
                                     "segmentationEllipseButton", true);
    auto* polygon = make_icon_button(tools_panel, "polygon", tr("Polygon"),
                                     "segmentationPolygonButton", true);
    auto* threshold = make_icon_button(
        tools_panel, "threshold", tr("Dual-threshold ROI"),
        "segmentationThresholdButton", true);
    auto* fill = make_icon_button(tools_panel, "fill", tr("Fill"),
                                  "segmentationFillButton", true);
    auto* eyedropper = make_icon_button(tools_panel, "pick", tr("Pick class"),
                                        "segmentationPickButton", true);
    pan->setChecked(true);
    auto* tool_group = new QButtonGroup(tools_panel);
    tool_group->setExclusive(true);
    for (auto* tool : {
             pan, transform_region, brush, eraser, rectangle, ellipse, polygon,
             threshold, fill, eyedropper})
        tool_group->addButton(tool);
    pan->setToolTip(tr("Pan (Alt+S)"));
    transform_region->setToolTip(
        tr("Transform region · double-click a region to edit (Alt+M)"));
    brush->setToolTip(tr("Brush (Alt+B)"));
    polygon->setToolTip(tr("Polygon (Alt+P)"));
    rectangle->setToolTip(tr("Rectangle (Alt+R)"));
    ellipse->setToolTip(tr("Ellipse (Alt+O)"));
    threshold->setToolTip(tr("Dual-threshold ROI (Alt+T)"));
    eraser->setToolTip(tr("Eraser (Alt+E)"));
    fill->setToolTip(tr("Fill (Alt+F)"));
    eyedropper->setToolTip(tr("Pick class (Alt+I)"));
    pan->setAccessibleName(tr("Pan annotation canvas"));
    transform_region->setAccessibleName(
        tr("Transform selected connected mask region"));
    brush->setAccessibleName(tr("Brush annotation tool"));
    polygon->setAccessibleName(tr("Polygon annotation tool"));
    rectangle->setAccessibleName(tr("Rectangle annotation tool"));
    ellipse->setAccessibleName(tr("Ellipse annotation tool"));
    threshold->setAccessibleName(tr("Dual-threshold ROI annotation tool"));
    eraser->setAccessibleName(tr("Eraser annotation tool"));
    fill->setAccessibleName(tr("Fill annotation tool"));
    eyedropper->setAccessibleName(tr("Pick annotation class"));
    brush_size_slider_ = new QSlider(Qt::Horizontal, tools_panel); brush_size_slider_->setObjectName("annotationBrushSizeSlider"); brush_size_slider_->setRange(2, 100); brush_size_slider_->setValue(18); brush_size_slider_->setMaximumWidth(90);
    auto* size_value = new QLabel(tr("18 px"), tools_panel); size_value->setObjectName("annotationBrushSizeValue"); size_value->setMinimumWidth(38);
    tools->addWidget(pan); tools->addWidget(transform_region); tools->addWidget(brush); tools->addWidget(eraser);
    tools->addWidget(rectangle); tools->addWidget(ellipse);
    tools->addWidget(polygon); tools->addWidget(threshold); tools->addWidget(fill);
    tools->addWidget(eyedropper); tools->addStretch();
    brush_size_caption_ = new QLabel(tr("Size"), tools_panel);
    tools->addWidget(brush_size_caption_); tools->addWidget(brush_size_slider_); tools->addWidget(size_value);
    viewport_layout->addWidget(tools_panel);

    auto* layers_panel = new QFrame(viewport_panel);
    layers_panel->setObjectName("annotationLayerBar");
    auto* layers = new QHBoxLayout(layers_panel);
    layers->setContentsMargins(10, 5, 10, 5);
    layers->setSpacing(10);
    annotation_layer_ = new QCheckBox(tr("Annotation"), layers_panel);
    annotation_layer_->setObjectName("annotationLayerCheck"); annotation_layer_->setChecked(true);
    annotation_layer_->setToolTip(tr("Show or hide the editable annotation overlay."));
    mask_layer_ = new QCheckBox(tr("Mask"), layers_panel);
    mask_layer_->setObjectName("maskLayerCheck"); mask_layer_->setChecked(true);
    prediction_layer_ = new QCheckBox(tr("Inference"), layers_panel);
    prediction_layer_->setObjectName("predictionLayerCheck"); prediction_layer_->setChecked(true);
    heatmap_layer_ = new QCheckBox(tr("Heatmap"), layers_panel);
    heatmap_layer_->setObjectName("heatmapLayerCheck"); heatmap_layer_->setChecked(false);
    opacity_slider_ = new QSlider(Qt::Horizontal, layers_panel);
    opacity_slider_->setObjectName("overlayOpacitySlider"); opacity_slider_->setRange(10, 100); opacity_slider_->setValue(42); opacity_slider_->setMaximumWidth(120);
    layer_caption_ = new QLabel(tr("SHOW"), layers_panel); layer_caption_->setProperty("eyebrow", true);
    auto* opacity_value = new QLabel(tr("42%"), layers_panel); opacity_value->setObjectName("overlayOpacityValue"); opacity_value->setMinimumWidth(34);
    opacity_caption_ = new QLabel(tr("Opacity"), layers_panel);
    layers->addWidget(layer_caption_); layers->addWidget(annotation_layer_); layers->addWidget(prediction_layer_);
    layers->addWidget(heatmap_layer_); layers->addWidget(mask_layer_);
    layers->addStretch(); layers->addWidget(opacity_caption_); layers->addWidget(opacity_slider_); layers->addWidget(opacity_value);
    viewport_layout->addWidget(layers_panel);
    canvas_ = new MaskCanvas(viewport_panel);
    canvas_->setMinimumWidth(560);
    canvas_->set_brush_size(18);
    canvas_->set_overlay_opacity(0.34);
    canvas_->set_tool(AnnotationTool::Pan);
    viewport_layout->addWidget(canvas_, 1);

    QTabWidget* tabs = inspector ? inspector : new QTabWidget(content);
    tabs->setObjectName("segmentationAnnotationInspector");
    tabs->setMinimumWidth(0);
    QWidget* results_panel = new QWidget(tabs);
    QVBoxLayout* results_layout = new QVBoxLayout(results_panel);
    results_layout->setContentsMargins(8, 8, 8, 8);
    result_details_ = new QLabel(tr("No inference result for this image."), results_panel);
    result_details_->setObjectName("segmentationAnnotationResultDetails");
    result_details_->setWordWrap(true);
    result_details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    results_layout->addWidget(result_details_);
    results_layout->addStretch();
    tabs->addTab(results_panel, tr("Results"));

    content->addWidget(samples_panel); content->addWidget(viewport_panel); content->addWidget(tabs);
    content->setCollapsible(0, true); content->setCollapsible(1, false); content->setCollapsible(2, true);
    content->setStretchFactor(0, 0); content->setStretchFactor(1, 1); content->setStretchFactor(2, 0);
    content->setSizes({210, 740, 360});
    root->addWidget(content, 1);
    connect(toggle_samples, &QToolButton::toggled, this, [this](bool shown) {
        set_side_panel_width(0, shown, 210);
    });
    connect(toggle_classes, &QToolButton::toggled, this, [this](bool shown) {
        set_side_panel_width(2, shown, 360);
    });
    connect(previous, &QToolButton::clicked, this, [this] { if (current_index_ > 0 && confirm_transition()) load_index(current_index_ - 1); });
    connect(next, &QToolButton::clicked, this, [this] {
        if (current_index_ + 1 < static_cast<int>(sample_bindings_.size()) && confirm_transition()) {
            load_index(current_index_ + 1);
        }
    });
    auto select_tool = [this](AnnotationTool tool) {
        canvas_->set_tool(tool);
        canvas_->setFocus(Qt::ShortcutFocusReason);
        if (tool == AnnotationTool::TransformSelection) {
            state_->setText(
                tr("Transform · double-click a region, then drag its body or handles"));
        } else {
            state_->setText(canvas_->is_dirty()
                ? tr("Mask has unsaved changes")
                : tr("Ready"));
        }
    };
    connect(pan, &QToolButton::clicked, canvas_, [select_tool] { select_tool(AnnotationTool::Pan); });
    connect(transform_region, &QToolButton::clicked, canvas_, [select_tool] {
        select_tool(AnnotationTool::TransformSelection);
    });
    connect(brush, &QToolButton::clicked, canvas_, [select_tool] { select_tool(AnnotationTool::Brush); });
    connect(polygon, &QToolButton::clicked, canvas_, [this] {
        canvas_->set_tool(AnnotationTool::Polygon);
        canvas_->setFocus(Qt::ShortcutFocusReason);
        state_->setText(tr("Polygon · Right-click/Enter finish · Esc cancel"));
    });
    connect(rectangle, &QToolButton::clicked, canvas_, [select_tool] { select_tool(AnnotationTool::Rectangle); });
    connect(ellipse, &QToolButton::clicked, canvas_, [select_tool] {
        select_tool(AnnotationTool::Ellipse);
    });
    connect(threshold, &QToolButton::clicked, canvas_, [this] {
        canvas_->set_tool(AnnotationTool::ThresholdRoi);
        canvas_->setFocus(Qt::ShortcutFocusReason);
        state_->setText(tr("Dual threshold · drag an ROI to preview"));
    });
    connect(eraser, &QToolButton::clicked, canvas_, [select_tool] { select_tool(AnnotationTool::Eraser); });
    connect(fill, &QToolButton::clicked, canvas_, [select_tool] { select_tool(AnnotationTool::Fill); });
    connect(eyedropper, &QToolButton::clicked, canvas_, [select_tool] { select_tool(AnnotationTool::Pick); });
    connect(zoom_out, &QToolButton::clicked, canvas_, [this] { canvas_->zoom_by(1.0 / 1.2); });
    connect(zoom_in, &QToolButton::clicked, canvas_, [this] { canvas_->zoom_by(1.2); });
    connect(zoom_fit, &QToolButton::clicked, canvas_, [this] { canvas_->reset_view(); });
    canvas_->zoom_changed = [zoom_value](int value) { zoom_value->setText(QString::number(value) + "%"); };
    canvas_->undo_state_changed = [undo, redo](bool can_undo, bool can_redo) {
        undo->setEnabled(can_undo);
        redo->setEnabled(can_redo);
    };
    connect(undo, &QToolButton::clicked, canvas_, [this] { canvas_->undo(); });
    connect(redo, &QToolButton::clicked, canvas_, [this] { canvas_->redo(); });
    connect(save, &QToolButton::clicked, this, [this] { save_current(); });
    connect(annotation_layer_, &QCheckBox::toggled, canvas_, [this](bool visible) { canvas_->set_overlay_visible(visible); });
    connect(mask_layer_, &QCheckBox::toggled, canvas_, [this](bool visible) { canvas_->set_mask_visible(visible); });
    connect(prediction_layer_, &QCheckBox::toggled, canvas_, [this](bool visible) { canvas_->set_prediction_visible(visible); });
    connect(heatmap_layer_, &QCheckBox::toggled, canvas_, [this](bool visible) { canvas_->set_heatmap_visible(visible); });
    connect(opacity_slider_, &QSlider::valueChanged, canvas_, [this, opacity_value](int value) { canvas_->set_overlay_opacity(value / 100.0); opacity_value->setText(QString::number(value) + "%"); });
    connect(brush_size_slider_, &QSlider::valueChanged, canvas_, [this, size_value](int value) { canvas_->set_brush_size(value); size_value->setText(tr("%1 px").arg(value)); });
    canvas_->polygon_active_changed = [this](bool active) {
        state_->setText(active
            ? tr("Polygon · Right-click/Enter finish · Esc cancel")
            : canvas_->is_dirty()
                ? tr("Mask has unsaved changes")
                : tr("Ready"));
    };
    canvas_->threshold_roi_selected = [this](const QRect& roi, const QImage& image) {
        ThresholdDialog dialog(image, threshold_low_, threshold_high_,
                               threshold_outside_, remember_thresholds_, this);
        if (dialog.exec() == QDialog::Accepted) {
            canvas_->apply_threshold_roi(
                roi, dialog.intervals(), dialog.keep_outside_range());
        } else {
            state_->setText(tr("Dual-threshold ROI cancelled"));
        }
    };
    images_->current_requested = [this](int index, const QString&) {
        if (index == current_index_) return true;
        if (!confirm_transition()) return false;
        load_index(index);
        return current_index_ == index;
    };
    images_->split_requested = [this](const std::vector<std::string>& ids,
                                      const std::string& value) {
        if (sample_split_requested && !ids.empty()) sample_split_requested(ids, value);
    };
    images_->extend_context_menu = [this](QMenu& menu, const std::vector<std::string>& ids) {
        auto* empty = menu.addAction(tr("Set as empty"));
        empty->setObjectName("annotationSetAsEmptyAction");
        connect(empty, &QAction::triggered, this, [this, ids] { set_selected_empty(ids); });
    };
    canvas_->dirty_changed = [this](bool dirty) {
        state_->setText(dirty ? tr("Mask has unsaved changes") : tr("Mask saved"));
    };
    auto add_shortcut = [this](const QKeySequence& sequence, QAbstractButton* button) {
        auto* shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        shortcut->setAutoRepeat(false);
        connect(shortcut, &QShortcut::activated, button, &QAbstractButton::click);
    };
    add_shortcut(QKeySequence("Alt+S"), pan);
    add_shortcut(QKeySequence("Alt+M"), transform_region);
    add_shortcut(QKeySequence("Alt+B"), brush);
    add_shortcut(QKeySequence("Alt+P"), polygon);
    add_shortcut(QKeySequence("Alt+R"), rectangle);
    add_shortcut(QKeySequence("Alt+O"), ellipse);
    add_shortcut(QKeySequence("Alt+T"), threshold);
    add_shortcut(QKeySequence("Alt+E"), eraser);
    add_shortcut(QKeySequence("Alt+F"), fill);
    add_shortcut(QKeySequence("Alt+I"), eyedropper);
    add_shortcut(QKeySequence::Undo, undo);
    add_shortcut(QKeySequence::Redo, redo);
    auto* fit_shortcut = new QShortcut(QKeySequence("0"), this);
    fit_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(fit_shortcut, &QShortcut::activated, zoom_fit, &QAbstractButton::click);
    UiMetrics::apply_semantic_metrics(this);
    update_layer_controls();
}

void SegmentationAnnotationWidget::set_side_panel_width(
    int index, bool shown, int preferred_width)
{
    if (!content_splitter_) return;
    auto sizes = content_splitter_->sizes();
    if (sizes.size() != 3) return;
    sizes[index] = shown ? preferred_width : 0;
    content_splitter_->setSizes(sizes);
}

void SegmentationAnnotationWidget::apply_responsive_layout(bool force) {
    if (!content_splitter_ || !toggle_samples_ || !toggle_classes_) return;
    const int available_width = width();
    const int mode = available_width < 980
        ? 2
        : (available_width < 1120 ||
           (current_image_portrait_ && available_width < 1500))
            ? 1
            : 0;
    if (!force && mode == responsive_layout_mode_) return;
    responsive_layout_mode_ = mode;

    const bool show_samples = toggle_samples_->isChecked();
    const bool show_classes = toggle_classes_->isChecked();
    const int center_width = std::max(
        560, available_width - (show_samples ? 210 : 0) -
                 (show_classes ? 360 : 0));
    content_splitter_->setSizes({
        show_samples ? 210 : 0,
        center_width,
        show_classes ? 360 : 0});
    content_splitter_->setProperty(
        "responsiveMode",
        mode == 0 ? "wide" : mode == 1 ? "narrow" : "compact");
}

void SegmentationAnnotationWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const bool compact = event->size().width() < 900;
    if (state_) state_->setVisible(!compact);
    if (brush_size_caption_) brush_size_caption_->setVisible(!compact);
    if (layer_caption_) layer_caption_->setVisible(!compact);
    if (opacity_caption_) opacity_caption_->setVisible(!compact);
    if (brush_size_slider_)
        brush_size_slider_->setMaximumWidth(compact ? 56 : 90);
    if (opacity_slider_)
        opacity_slider_->setMaximumWidth(compact ? 72 : 120);
    if (annotation_layer_)
        annotation_layer_->setText(compact ? tr("Ann.") : tr("Annotation"));
    if (prediction_layer_)
        prediction_layer_->setText(compact ? tr("Pred.") : tr("Inference"));
    if (heatmap_layer_)
        heatmap_layer_->setText(compact ? tr("Heat") : tr("Heatmap"));
    if (mask_layer_)
        mask_layer_->setText(tr("Mask"));
    if (images_)
        images_->setIconSize(
            compact ? QSize(40, 30) : QSize(56, 42));
    apply_responsive_layout();
    update_context_label();
}

void SegmentationAnnotationWidget::set_task_dataset(
    const std::vector<TaskSampleBinding>& samples)
{
    if (!confirm_transition()) {
        return;
    }
    current_index_ = -1;
    sample_bindings_.clear();
    display_names_.clear();
    std::vector<SampleBrowserEntry> entries;
    for (int sample_index = 0;
         sample_index < static_cast<int>(samples.size());
         ++sample_index) {
        const TaskSampleBinding& sample = samples[static_cast<size_t>(sample_index)];
        if (!sample.selected || sample.image_paths.empty()) {
            continue;
        }
        const QString image = QString::fromStdString(sample.image_paths.front());
        QImageReader reader(image);
        reader.setDecideFormatFromContent(true);
        if (!QFileInfo(image).isFile() || !reader.canRead()) {
            continue;
        }
        const QString display_name = image_display_name(sample_index, sample);
        entries.push_back({sample.sample_id, image, display_name, {}, {}, {},
            static_cast<int>(entries.size())});
        sample_bindings_.push_back(sample);
        display_names_.push_back(display_name);
    }
    images_->set_entries(entries);
    refresh_sample_status();
    if (sample_bindings_.empty()) {
        canvas_->clear();
        context_->setText(tr("No dataset selected"));
        context_->setToolTip({});
        state_->setText(tr("No valid imported images are connected."));
        refresh_inference_layers();
    } else if (current_index_ < 0) {
        load_index(0);
    }
}

void SegmentationAnnotationWidget::refresh_samples(
    const std::vector<TaskSampleBinding>& samples,
    const std::vector<std::string>& reload_sample_ids)
{
    bool reload_current = false;
    for (size_t index = 0; index < sample_bindings_.size(); ++index) {
        auto& displayed = sample_bindings_[index];
        const auto source = std::find_if(samples.begin(), samples.end(),
            [&](const auto& sample) { return sample.sample_id == displayed.sample_id; });
        if (source == samples.end()) continue;
        displayed = *source;
        reload_current = reload_current || (static_cast<int>(index) == current_index_ &&
            std::find(reload_sample_ids.begin(), reload_sample_ids.end(), displayed.sample_id) != reload_sample_ids.end());
    }
    if (reload_current) load_index(current_index_);
    refresh_sample_status();
    update_context_label();
}

void SegmentationAnnotationWidget::set_inference_result(
    const InferenceResultSet* result, const QHash<int, QString>& model_classes,
    const std::vector<jocelin::model::ModelClass>& model_schema,
    const std::vector<AnnotationClass>& task_classes) {
    inference_results_ = result ? result->samples : std::vector<ResultReference>{};
    prediction_class_names_ = model_classes;
    prediction_model_schema_ = model_schema;
    prediction_task_classes_ = task_classes;
    refresh_inference_layers();
}

void SegmentationAnnotationWidget::refresh_inference_layers() {
    if (result_details_) {
        result_details_->setText(tr("No inference result for this image."));
    }
    if (!canvas_ || current_index_ < 0 || current_index_ >= static_cast<int>(sample_bindings_.size()) ||
        sample_bindings_[static_cast<size_t>(current_index_)].image_paths.empty()) {
        if (canvas_) {
            canvas_->set_prediction_overlay({});
            canvas_->set_heatmap_overlay({});
            canvas_->set_mask_overlay({});
        }
        update_layer_controls();
        return;
    }
    canvas_->set_prediction_overlay({});
    canvas_->set_heatmap_overlay({});
    const TaskSampleBinding& sample = sample_bindings_[static_cast<size_t>(current_index_)];
    for (const auto& reference : inference_results_) {
        const auto* value = std::get_if<jocelin::result::sg::SgResult>(&reference.result);
        if (!value || reference.source_sample_id != sample.sample_id ||
            QFileInfo(QString::fromStdString(value->image_path)).absoluteFilePath() !=
                QFileInfo(QString::fromStdString(sample.image_paths.front())).absoluteFilePath()) {
            continue;
        }
        if (result_details_) {
            const QString unavailable = QStringLiteral("—");
            result_details_->setText(
                tr("Confidence: %1\nPredicted area: %2 px\nMean IoU: %3\nPrecision: %4\nRecall: %5\nF1: %6")
                    .arg(value->mean_confidence, 0, 'f', 4)
                    .arg(value->predicted_area)
                    .arg(value->metrics.available ? QString::number(value->metrics.mean_iou, 'f', 4) : unavailable)
                    .arg(value->metrics.available ? QString::number(value->metrics.precision, 'f', 4) : unavailable)
                    .arg(value->metrics.available ? QString::number(value->metrics.recall, 'f', 4) : unavailable)
                    .arg(value->metrics.available ? QString::number(value->metrics.f1, 'f', 4) : unavailable));
        }
        canvas_->set_prediction_overlay(prediction_overlay_from_result(
                                            *value, prediction_model_schema_, prediction_task_classes_),
                                        present_result(*value, prediction_class_names_));
        if (!value->result_name.empty() && !value->save_dir.empty())
            canvas_->set_heatmap_overlay(QImage(QDir(QString::fromStdString(value->save_dir))
                .filePath(QString::fromStdString(value->result_name) + "_heatmap.png")));
        break;
    }
    const QImage training_mask(QString::fromStdString(sample.training_mask_path));
    canvas_->set_mask_overlay(neutral_mask_overlay(training_mask));
    update_layer_controls();
}

void SegmentationAnnotationWidget::update_layer_controls() {
    if (!prediction_layer_ || !heatmap_layer_ || !mask_layer_ || !canvas_) return;
    prediction_layer_->setEnabled(canvas_->has_prediction_overlay());
    prediction_layer_->setToolTip(prediction_layer_->isEnabled() ? QString{} : tr("No inference mask is available for this image."));
    heatmap_layer_->setEnabled(canvas_->has_heatmap_overlay());
    heatmap_layer_->setToolTip(heatmap_layer_->isEnabled() ? QString{} : tr("No heatmap is available for this image."));
    mask_layer_->setEnabled(canvas_->has_mask_overlay());
    mask_layer_->setToolTip(mask_layer_->isEnabled()
        ? tr("Show or hide the training exclusion mask.")
        : tr("No training exclusion mask is configured for this image."));
}

void SegmentationAnnotationWidget::refresh_sample_status() {
    if (!images_ || !samples_title_) return;
    int annotated = 0;
    int quality_issues = 0;
    for (int row = 0; row < images_->count(); ++row) {
        auto* item = images_->item(row);
        const auto* sample = row < static_cast<int>(sample_bindings_.size())
            ? &sample_bindings_[static_cast<size_t>(row)]
            : nullptr;
        const QString annotation_state = sample
            ? QString::fromStdString(sample->annotation_state)
            : QStringLiteral("unannotated");
        const bool issue = sample &&
            !sample->quality_issues.empty();
        QString state = tr("Not annotated");
        if (annotation_state == QStringLiteral("annotated"))
            state = tr("Annotated");
        const bool complete =
            annotation_state == QStringLiteral("annotated");
        annotated += complete ? 1 : 0;
        quality_issues += issue ? 1 : 0;
        const int image_count = sample ? static_cast<int>(sample->image_paths.size()) : 1;
        QString marker;
        if (complete && !issue)
            marker = QStringLiteral("  ✓");
        else if (issue)
            marker = QStringLiteral("  !");

        const QString display_name = display_names_.value(row);
        const QString browser_name =
            display_name +
            (image_count > 1
                ? tr("  +%1").arg(image_count - 1)
                : QString{}) +
            marker;
        QString split_state = sample
            ? QString::fromStdString(sample->split)
            : QString{};
        if (!split_state.isEmpty()) split_state[0] = split_state[0].toUpper();
        images_->update_entry_state(
            row,
            browser_name,
            state,
            split_state,
            sample ? sample_image_tooltip(display_name, *sample, state) : QString{});
        item->setForeground(QColor("#173F46"));
    }
    samples_title_->setText(
        tr("SAMPLES · %1/%2")
            .arg(annotated)
            .arg(images_->count()));
    samples_title_->setToolTip(
        tr("%1 of %2 samples have valid supervised targets (including background-only)")
            .arg(annotated)
            .arg(images_->count()));
    if (quality_) {
        quality_->setVisible(quality_issues > 0);
        quality_->setText(quality_issues == 1
            ? tr("1 mask needs review")
            : tr("%1 masks need review").arg(quality_issues));
        quality_->setProperty("qualityState", "warning");
        quality_->style()->unpolish(quality_);
        quality_->style()->polish(quality_);
    }
}

bool SegmentationAnnotationWidget::confirm_transition() {
    if (!canvas_ || !canvas_->is_dirty()) {
        return true;
    }
    if (QSettings(QSettings::IniFormat, QSettings::UserScope, "Jocelin", "DeepLearningStudio")
            .value("annotation/autoSave", false).toBool()) {
        return save_current();
    }
    const QMessageBox::StandardButton answer = QMessageBox::warning(this, tr("Unsaved mask"), tr("Save the current mask before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) {
        return false;
    }
    if (answer == QMessageBox::Save) {
        return save_current();
    }
    return true;
}

void SegmentationAnnotationWidget::load_index(int index) {
    if (index < 0 || index >= static_cast<int>(sample_bindings_.size())) {
        return;
    }
    const TaskSampleBinding& sample = sample_bindings_[static_cast<size_t>(index)];
    if (sample.image_paths.empty()) {
        return;
    }
    const QString path = QString::fromStdString(sample.image_paths.front());
    QString error;
    if (!canvas_->load(path, {}, &error)) {
        state_->setText(error); if (status) status(error); return;
    }
    canvas_->apply_mask(segmentation_annotation_image(sample), false);
    current_index_ = index;
    const QSize dimensions = QImageReader(path).size();
    const bool portrait = dimensions.isValid() &&
        dimensions.height() > dimensions.width() * 11 / 10;
    if (portrait != current_image_portrait_) {
        current_image_portrait_ = portrait;
        apply_responsive_layout();
    }
    if (images_->current_source_index() != index)
        images_->set_current_source_index(index);
    refresh_inference_layers();
    update_context_label();
    state_->setText(tr("Ready"));
}

void SegmentationAnnotationWidget::update_context_label() {
    if (!context_ || current_index_ < 0 ||
        current_index_ >= static_cast<int>(sample_bindings_.size())) {
        return;
    }
    const TaskSampleBinding& sample = sample_bindings_[static_cast<size_t>(current_index_)];
    if (sample.image_paths.empty()) {
        return;
    }
    const QString path = QString::fromStdString(sample.image_paths.front());
    const QSize dimensions = QImageReader(path).size();
    const QString concise =
        tr("%1 / %2  |  %3  |  %4 × %5")
            .arg(current_index_ + 1)
            .arg(static_cast<int>(sample_bindings_.size()))
            .arg(display_names_.value(current_index_))
            .arg(dimensions.width())
            .arg(dimensions.height());
    context_->setText(concise);
    context_->setToolTip(
        images_ && images_->item(current_index_)
            ? images_->item(current_index_)->toolTip()
            : QString{});
}

void SegmentationAnnotationWidget::set_selected_empty(const std::vector<std::string>& ids) {
    if (!samples_empty_requested) return;
    if (QMessageBox::question(this, tr("Set as empty"),
            tr("Confirm %1 sample(s) as pure background? Their foreground annotation will be cleared.")
                .arg(static_cast<int>(ids.size())),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    samples_empty_requested(ids);
}

bool SegmentationAnnotationWidget::save_current() {
    if (current_index_ < 0) {
        return false;
    }
    if (!canvas_->is_dirty()) {
        return true;
    }
    if (!save_mask || !save_mask(
            sample_bindings_[static_cast<size_t>(current_index_)].sample_id, canvas_->mask_image())) {
        return false;
    }
    canvas_->mark_saved();
    refresh_sample_status();
    update_context_label();
    state_->setText(tr("Annotation updated"));
    if (status) status(state_->text());
    return true;
}

} // namespace studio
