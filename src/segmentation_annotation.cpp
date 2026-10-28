#include "segmentation_annotation.h"
#include "result_presentation.h"
#include "segmentation_annotation_state.h"

#include "icon_provider.h"
#include "sample_presentation.h"
#include "sample_browser.h"
#include "task_dataset_binding.h"
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
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QImageReader>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineF>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QToolButton>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <cmath>
#include <iterator>
#include <set>
#include <QWheelEvent>

namespace studio {

namespace {

class MaskCommand final : public QUndoCommand {
public:
    MaskCommand(MaskCanvas* canvas, QImage before, QImage after)
        : canvas_(canvas), before_(std::move(before)), after_(std::move(after)) {}
    void undo() override { canvas_->apply_mask(before_, true); }
    void redo() override {
        if (first_) { first_ = false; return; }
        canvas_->apply_mask(after_, true);
    }
private:
    MaskCanvas* canvas_;
    QImage before_;
    QImage after_;
    bool first_ = true;
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
    explicit ThresholdDialog(const QImage& roi, QWidget* parent = nullptr)
        : QDialog(parent), source_(roi.convertToFormat(QImage::Format_Grayscale8))
    {
        setWindowTitle(tr("Dual-threshold binarization"));
        setModal(true);
        setMinimumWidth(440);
        auto* root = new QVBoxLayout(this);
        root->addWidget(new QLabel(
            tr("Preview the selected ROI, then confirm how pixels are assigned to the current class."),
            this));
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
        low_->setValue(64);
        high_->setValue(192);
        mode_ = new QComboBox(this);
        mode_->setObjectName("thresholdMode");
        mode_->addItem(tr("Keep values inside [low, high]"), false);
        mode_->addItem(tr("Keep values outside [low, high]"), true);
        form->addRow(tr("Low threshold"), low_);
        form->addRow(tr("High threshold"), high_);
        form->addRow(tr("Selection"), mode_);
        root->addLayout(form);
        auto* note = new QLabel(
            tr("Selected pixels become the current class; other pixels inside the ROI become background. "
               "Cancel leaves the mask unchanged."),
            this);
        note->setProperty("meta", true);
        note->setWordWrap(true);
        root->addWidget(note);
        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
        apply_button_ = buttons->button(QDialogButtonBox::Ok);
        apply_button_->setText(tr("Apply"));
        root->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
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

constexpr qreal kTransformHandleRadius = 5.0;
constexpr qreal kRotationHandleDistance = 24.0;
constexpr int kAnnotationViewportMargin = 24;
constexpr qreal kMinimumTransformScale = 0.05;

QSize annotation_viewport_size(const QSize& size) {
    return {
        std::max(1, size.width() - 2 * kAnnotationViewportMargin),
        std::max(1, size.height() - 2 * kAnnotationViewportMargin)};
}

QPointF annotation_viewport_point(const QPointF& point) {
    return point - QPointF(kAnnotationViewportMargin, kAnnotationViewportMargin);
}
constexpr qreal kMaximumTransformScale = 32.0;
constexpr double kDegreesPerRadian = 57.29577951308232;

QTransform transform_about(const QPointF& anchor, qreal rotation_degrees,
                           qreal scale_x, qreal scale_y) {
    QTransform transform;
    transform.translate(anchor.x(), anchor.y());
    transform.rotate(rotation_degrees);
    transform.scale(scale_x, scale_y);
    transform.translate(-anchor.x(), -anchor.y());
    return transform;
}

qreal dot_product(const QPointF& left, const QPointF& right) {
    return left.x() * right.x() + left.y() * right.y();
}

qreal vector_length(const QPointF& vector) {
    return std::hypot(vector.x(), vector.y());
}

qreal normalized_angle_delta(qreal degrees) {
    while (degrees > 180.0) degrees -= 360.0;
    while (degrees < -180.0) degrees += 360.0;
    return degrees;
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

MaskCanvas::MaskCanvas(QWidget* parent) : QWidget(parent), undo_stack_(new QUndoStack(this)) {
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
    prediction_overlay_ = {};
    heatmap_overlay_ = {};
    mask_overlay_ = {};
    viewport_.reset();
    undo_stack_->clear();
    polygon_points_.clear();
    stroke_before_ = {};
    dirty_ = false;
    rebuild_overlay();
    update();
    if (dirty_changed) dirty_changed(false);
    if (zoom_changed) zoom_changed(zoom_percent());
    if (undo_state_changed) undo_state_changed(false, false);
    return true;
}

void MaskCanvas::mark_saved() {
    dirty_ = false;
    if (dirty_changed) dirty_changed(false);
}

void MaskCanvas::clear() {
    clear_transform_edit();
    image_ = {};
    mask_ = {};
    overlay_ = {};
    prediction_overlay_ = {};
    heatmap_overlay_ = {};
    mask_overlay_ = {};
    viewport_.reset();
    undo_stack_->clear();
    polygon_points_.clear();
    stroke_before_ = {};
    dirty_ = false;
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
    dirty_ = false;
    if (dirty_changed) dirty_changed(false);
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

void MaskCanvas::set_class_colors(const QHash<int, QColor>& colors) {
    class_colors_ = colors;
    rebuild_overlay();
    update();
}

void MaskCanvas::set_hidden_class_values(const QSet<int>& values) {
    hidden_class_values_ = values;
    rebuild_overlay();
    update();
}

void MaskCanvas::apply_mask(const QImage& mask, bool dirty) {
    mask_ = mask;
    dirty_ = dirty;
    rebuild_overlay();
    update();
    if (dirty_changed) dirty_changed(dirty_);
}

void MaskCanvas::apply_threshold_roi(
    const QRect& requested_roi,
    const std::vector<jocelin::geo::Interval>& intervals,
    bool keep_outside_range)
{
    if (image_.isNull() || mask_.isNull() || class_value_ <= 0 ||
        intervals.empty() ||
        std::any_of(intervals.begin(), intervals.end(),
                    [](const auto& interval) { return !interval.valid(); }))
        return;
    const QRect roi = requested_roi.normalized().intersected(image_.rect());
    if (roi.isEmpty()) return;
    const QImage before = mask_;
    const QImage grayscale = image_.convertToFormat(QImage::Format_Grayscale8);
    for (int y = roi.top(); y <= roi.bottom(); ++y) {
        const uchar* source = grayscale.constScanLine(y);
        uchar* target = mask_.scanLine(y);
        for (int x = roi.left(); x <= roi.right(); ++x) {
            const bool inside = matches_intervals(source[x], intervals);
            const bool selected = keep_outside_range ? !inside : inside;
            target[x] = static_cast<uchar>(selected ? class_value_ : 0);
        }
    }
    finish_edit(before);
}

void MaskCanvas::apply_ellipse(const QRect& requested_roi) {
    if (mask_.isNull() || class_value_ <= 0) return;
    const QRect roi = requested_roi.normalized();
    if (roi.isEmpty() || !roi.intersects(mask_.rect())) return;
    const QImage before = mask_;
    QPainter painter(&mask_);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(class_value_, class_value_, class_value_));
    painter.drawEllipse(roi);
    painter.end();
    finish_edit(before);
}

void MaskCanvas::set_prediction_overlay(const QImage& image) {
    prediction_overlay_ = image;
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

void MaskCanvas::set_tool(Tool tool) {
    if (tool != Tool::Polygon && !polygon_points_.isEmpty())
        cancel_polygon();
    if (tool != tool_) clear_transform_edit();
    tool_ = tool;
    painting_ = false;
    transform_dragging_ = false;
    active_transform_handle_ = TransformHandle::None;
    shape_start_ = {-1, -1};
    setCursor(tool_ == Tool::Pan ? Qt::OpenHandCursor :
              tool_ == Tool::TransformRegion ? Qt::ArrowCursor :
              tool_ == Tool::Pick ? Qt::PointingHandCursor : Qt::CrossCursor);
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
    if ((tool_ == Tool::Rectangle || tool_ == Tool::Ellipse ||
         tool_ == Tool::ThresholdRoi) &&
        painting_ && shape_start_.x() >= 0 && hover_point_.x() >= 0) {
        const QRectF shape(
            QRectF(map_point(shape_start_), map_point(hover_point_)).normalized());
        if (tool_ == Tool::Ellipse)
            painter.drawEllipse(shape);
        else
            painter.drawRect(shape);
    }
    if (tool_ == Tool::Polygon && !polygon_points_.isEmpty()) {
        QPolygonF preview;
        for (const QPoint& point : polygon_points_) preview << map_point(point);
        if (hover_point_.x() >= 0) preview << map_point(hover_point_);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(preview);
        for (const QPoint& point : polygon_points_) painter.drawEllipse(map_point(point), 3.5, 3.5);
    }
    if (transform_region_) paint_transform_controls(painter);
}

void MaskCanvas::mousePressEvent(QMouseEvent* event) {
    if (image_.isNull()) return;
    setFocus(Qt::MouseFocusReason);

    if (event->button() == Qt::LeftButton && tool_ == Tool::TransformRegion) {
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
        (event->button() == Qt::LeftButton && tool_ == Tool::Pan)) {
        panning_ = true;
        pan_last_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() == Qt::RightButton && tool_ == Tool::Polygon) {
        if (polygon_points_.size() >= 3)
            finish_polygon();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    const QPoint image_pixel = image_point(event->pos());
    if (tool_ == Tool::Pick) {
        if (image_pixel.x() < 0) return;
        const int value = mask_.constScanLine(image_pixel.y())[image_pixel.x()];
        if (class_picked) class_picked(value);
        return;
    }
    if (!editing_enabled_) return;
    if (tool_ != Tool::Eraser && class_value_ <= 0) {
        const int requested = class_required ? class_required() : 0;
        if (requested <= 0) return;
        class_value_ = requested;
    }
    if (tool_ == Tool::Fill) {
        if (image_pixel.x() >= 0) fill_at(image_pixel);
        return;
    }
    const QPoint point = annotation_point(event->pos());
    if (point.x() < 0) return;
    if (tool_ == Tool::Polygon) {
        const bool starting = polygon_points_.isEmpty();
        if (starting) stroke_before_ = mask_;
        polygon_points_.append(point);
        hover_point_ = point;
        update();
        if (starting && polygon_active_changed) polygon_active_changed(true);
        return;
    }

    stroke_before_ = mask_;
    painting_ = true;
    stroke_last_point_ = point;
    hover_point_ = point;
    if (tool_ == Tool::Rectangle || tool_ == Tool::Ellipse ||
        tool_ == Tool::ThresholdRoi)
        shape_start_ = point;
    else paint_at(point);
}

void MaskCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        const QPointF delta = event->pos() - pan_last_;
        viewport_.pan_by(delta);
        pan_last_ = event->pos();
        update();
        return;
    }

    const bool extended_annotation_tool =
        tool_ == Tool::Brush || tool_ == Tool::Eraser || tool_ == Tool::Polygon ||
        tool_ == Tool::Rectangle || tool_ == Tool::Ellipse || tool_ == Tool::ThresholdRoi;
    hover_point_ = extended_annotation_tool
        ? annotation_point(event->pos())
        : image_point(event->pos());
    if (transform_dragging_) {
        update_transform_drag(image_position(event->pos()));
        return;
    }
    if (tool_ == Tool::TransformRegion && transform_region_) {
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
    if (painting_ && (tool_ == Tool::Brush || tool_ == Tool::Eraser)) {
        const QPoint point = annotation_point(event->pos());
        if (point.x() >= 0) {
            paint_segment(stroke_last_point_, point);
            stroke_last_point_ = point;
        }
    } else if (tool_ == Tool::Rectangle || tool_ == Tool::Ellipse ||
               tool_ == Tool::ThresholdRoi || tool_ == Tool::Polygon) update();
}

void MaskCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && transform_dragging_) {
        finish_transform_drag();
        return;
    }
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && tool_ == Tool::Pan)) {
        panning_ = false;
        setCursor(tool_ == Tool::Pan ? Qt::OpenHandCursor :
                  tool_ == Tool::TransformRegion ? Qt::ArrowCursor : Qt::CrossCursor);
        return;
    }
    if (event->button() != Qt::LeftButton || !painting_) return;
    painting_ = false;
    stroke_last_point_ = {-1, -1};
    if ((tool_ == Tool::Rectangle || tool_ == Tool::Ellipse) &&
        shape_start_.x() >= 0 && hover_point_.x() >= 0) {
        const QRect shape = QRect(shape_start_, hover_point_).normalized();
        if (tool_ == Tool::Ellipse)
            apply_ellipse(shape);
        else {
            const int value = class_value_;
            QPainter painter(&mask_);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(value, value, value));
            painter.drawRect(shape);
            painter.end();
        }
    }
    if (tool_ == Tool::ThresholdRoi &&
        shape_start_.x() >= 0 && hover_point_.x() >= 0) {
        const QRect roi = QRect(shape_start_, hover_point_).normalized()
                              .intersected(image_.rect());
        stroke_before_ = {};
        shape_start_ = {-1, -1};
        update();
        if (!roi.isEmpty() && threshold_roi_selected)
            threshold_roi_selected(roi, image_.copy(roi));
        return;
    }
    shape_start_ = {-1, -1};
    if (tool_ != Tool::Ellipse) finish_edit(stroke_before_);
    stroke_before_ = {};
}

void MaskCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && tool_ == Tool::Polygon) {
        finish_polygon();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && tool_ == Tool::TransformRegion &&
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
    if (event->key() == Qt::Key_Escape && transform_region_) {
        clear_transform_edit();
        event->accept();
        return;
    }
    if (tool_ == Tool::Polygon &&
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        if (polygon_points_.size() >= 3)
            finish_polygon();
        event->accept();
        return;
    }
    if (tool_ == Tool::Polygon && event->key() == Qt::Key_Escape) {
        cancel_polygon();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void MaskCanvas::paint_at(const QPoint& point) {
    if (point.x() < 0 || mask_.isNull()) return;
    if (tool_ != Tool::Eraser && class_value_ <= 0) return;
    QPainter painter(&mask_);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    const int value = tool_ == Tool::Eraser ? 0 : class_value_;
    painter.setBrush(QColor(value, value, value));
    painter.drawEllipse(point, brush_size_ / 2, brush_size_ / 2);
    painter.end();
    dirty_ = true;
    rebuild_overlay();
    update();
    if (dirty_changed) dirty_changed(true);
}

void MaskCanvas::paint_segment(const QPoint& from, const QPoint& to) {
    if (from.x() < 0 || to.x() < 0 || mask_.isNull()) return;
    if (tool_ != Tool::Eraser && class_value_ <= 0) return;
    const int value = tool_ == Tool::Eraser ? 0 : class_value_;
    QPainter painter(&mask_);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(QColor(value, value, value), brush_size_, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(from, to);
    painter.end();
    dirty_ = true;
    rebuild_overlay();
    update();
    if (dirty_changed) dirty_changed(true);
}

void MaskCanvas::fill_at(const QPoint& point) {
    if (mask_.isNull() || point.x() < 0 || class_value_ <= 0) return;
    const QImage before = mask_;
    const uchar source = mask_.constScanLine(point.y())[point.x()];
    const uchar target = static_cast<uchar>(class_value_);
    if (source == target) return;
    QList<QPoint> pending{point};
    mask_.scanLine(point.y())[point.x()] = target;
    while (!pending.isEmpty()) {
        const QPoint current = pending.takeLast();
        const QPoint neighbours[] = {{current.x() - 1, current.y()}, {current.x() + 1, current.y()},
                                     {current.x(), current.y() - 1}, {current.x(), current.y() + 1}};
        for (const QPoint& next : neighbours) {
            if (next.x() < 0 || next.y() < 0 || next.x() >= mask_.width() || next.y() >= mask_.height()) continue;
            uchar& value = mask_.scanLine(next.y())[next.x()];
            if (value != source) continue;
            value = target;
            pending.append(next);
        }
    }
    finish_edit(before);
}

bool MaskCanvas::begin_transform_edit(const QPoint& point) {
    if (mask_.isNull() || !mask_.rect().contains(point)) return false;
    const int label = mask_.constScanLine(point.y())[point.x()];
    if (label <= 0) return false;

    auto selection = AnnotationRegionEditService::select_connected_region(
        mask_, point, label);
    if (!selection) return false;
    transform_region_ = std::move(selection);
    transform_base_mask_ = mask_;
    transform_drag_before_ = {};
    transform_matrix_ = {};
    transform_drag_matrix_ = {};
    transform_dragging_ = false;
    active_transform_handle_ = TransformHandle::None;
    setCursor(Qt::SizeAllCursor);
    update();
    return true;
}

void MaskCanvas::clear_transform_edit() {
    const bool was_active = transform_region_.has_value() || transform_dragging_;
    if (transform_dragging_ && !transform_drag_before_.isNull()) {
        mask_ = transform_drag_before_;
        rebuild_overlay();
    }
    transform_region_.reset();
    transform_base_mask_ = {};
    transform_drag_before_ = {};
    transform_matrix_ = {};
    transform_drag_matrix_ = {};
    transform_dragging_ = false;
    active_transform_handle_ = TransformHandle::None;
    if (tool_ == Tool::TransformRegion) setCursor(Qt::ArrowCursor);
    if (was_active) update();
}

QPolygonF MaskCanvas::transform_frame() const {
    if (!transform_region_) return {};
    const QRectF bounds = AnnotationRegionEditService::region_bounds(*transform_region_);
    if (bounds.isEmpty()) return {};
    QPolygonF frame;
    for (const QPointF& point :
         {bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()})
        frame << transform_matrix_.map(point);
    return frame;
}

QPointF MaskCanvas::rotation_handle_position() const {
    const QPolygonF frame = transform_frame();
    if (frame.size() != 4) return {};
    const QPointF top_left = widget_position(frame[0]);
    const QPointF top_right = widget_position(frame[1]);
    const QPointF edge = top_right - top_left;
    const qreal length = vector_length(edge);
    if (length <= 0.0) return top_right;
    const QPointF outward(edge.y() / length, -edge.x() / length);
    return top_right + outward * kRotationHandleDistance;
}

void MaskCanvas::paint_transform_controls(QPainter& painter) const {
    const QPolygonF frame = transform_frame();
    if (frame.size() != 4) return;

    QPolygonF widget_frame;
    for (const QPointF& point : frame) widget_frame << widget_position(point);
    const QPointF rotation = rotation_handle_position();
    const QPointF top = (widget_frame[0] + widget_frame[1]) / 2.0;
    const QPointF right = (widget_frame[1] + widget_frame[2]) / 2.0;
    const QPointF bottom = (widget_frame[2] + widget_frame[3]) / 2.0;
    const QPointF left = (widget_frame[3] + widget_frame[0]) / 2.0;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor("#6BD0E0"), 1.5, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(widget_frame);
    painter.setPen(QPen(QColor("#6BD0E0"), 1.5));
    painter.drawLine(widget_frame[1], rotation);
    painter.setBrush(QColor("#F4C66A"));
    for (const QPointF& point : {widget_frame[0], widget_frame[1],
                                 widget_frame[2], widget_frame[3],
                                 top, right, bottom, left}) {
        painter.drawRect(QRectF(point.x() - kTransformHandleRadius,
                                point.y() - kTransformHandleRadius,
                                kTransformHandleRadius * 2.0,
                                kTransformHandleRadius * 2.0));
    }
    painter.setBrush(QColor("#6BD0E0"));
    painter.drawEllipse(rotation, kTransformHandleRadius + 1.0,
                        kTransformHandleRadius + 1.0);
    painter.restore();
}

MaskCanvas::TransformHandle MaskCanvas::transform_handle_at(
    const QPointF& widget_point) const
{
    const QPolygonF frame = transform_frame();
    if (frame.size() != 4) return TransformHandle::None;

    QPolygonF widget_frame;
    for (const QPointF& point : frame) widget_frame << widget_position(point);
    const auto hit = [&widget_point](const QPointF& point, qreal radius) {
        return QLineF(widget_point, point).length() <= radius;
    };
    const QPointF top = (widget_frame[0] + widget_frame[1]) / 2.0;
    const QPointF right = (widget_frame[1] + widget_frame[2]) / 2.0;
    const QPointF bottom = (widget_frame[2] + widget_frame[3]) / 2.0;
    const QPointF left = (widget_frame[3] + widget_frame[0]) / 2.0;

    if (hit(rotation_handle_position(), kTransformHandleRadius + 3.0))
        return TransformHandle::Rotate;
    if (hit(widget_frame[0], kTransformHandleRadius + 2.0)) return TransformHandle::TopLeft;
    if (hit(widget_frame[1], kTransformHandleRadius + 2.0)) return TransformHandle::TopRight;
    if (hit(widget_frame[2], kTransformHandleRadius + 2.0)) return TransformHandle::BottomRight;
    if (hit(widget_frame[3], kTransformHandleRadius + 2.0)) return TransformHandle::BottomLeft;
    if (hit(top, kTransformHandleRadius + 2.0)) return TransformHandle::Top;
    if (hit(right, kTransformHandleRadius + 2.0)) return TransformHandle::Right;
    if (hit(bottom, kTransformHandleRadius + 2.0)) return TransformHandle::Bottom;
    if (hit(left, kTransformHandleRadius + 2.0)) return TransformHandle::Left;
    return transform_region_contains(image_position(widget_point))
        ? TransformHandle::Body : TransformHandle::None;
}

bool MaskCanvas::transform_region_contains(const QPointF& image_point) const {
    if (!transform_region_) return false;
    bool invertible = false;
    const QTransform inverse = transform_matrix_.inverted(&invertible);
    if (!invertible) return false;
    const QPointF source_point = inverse.map(image_point);
    const int x = static_cast<int>(std::floor(source_point.x()));
    const int y = static_cast<int>(std::floor(source_point.y()));
    return transform_region_->region.contains(y, x);
}

void MaskCanvas::begin_transform_drag(TransformHandle handle, const QPointF& point) {
    if (!transform_region_ || transform_base_mask_.isNull()) return;
    active_transform_handle_ = handle;
    transform_drag_start_ = point;
    transform_drag_before_ = mask_;
    transform_drag_matrix_ = transform_matrix_;
    transform_dragging_ = true;
}

void MaskCanvas::update_transform_drag(const QPointF& point) {
    if (!transform_dragging_ || !transform_region_ || transform_base_mask_.isNull()) return;
    const QRectF bounds = AnnotationRegionEditService::region_bounds(*transform_region_);
    if (bounds.isEmpty()) return;

    const QPointF top_left = bounds.topLeft();
    const QPointF top_right = bounds.topRight();
    const QPointF bottom_right = bounds.bottomRight();
    const QPointF bottom_left = bounds.bottomLeft();
    const QPointF top_center = (top_left + top_right) / 2.0;
    const QPointF right_center = (top_right + bottom_right) / 2.0;
    const QPointF bottom_center = (bottom_left + bottom_right) / 2.0;
    const QPointF left_center = (top_left + bottom_left) / 2.0;
    const auto map = [this](const QPointF& source) {
        return transform_drag_matrix_.map(source);
    };

    QTransform next = transform_drag_matrix_;
    switch (active_transform_handle_) {
    case TransformHandle::Body: {
        QTransform translation;
        translation.translate(point.x() - transform_drag_start_.x(),
                              point.y() - transform_drag_start_.y());
        next = translation * transform_drag_matrix_;
        break;
    }
    case TransformHandle::TopLeft:
    case TransformHandle::TopRight:
    case TransformHandle::BottomRight:
    case TransformHandle::BottomLeft: {
        QPointF handle;
        QPointF anchor;
        if (active_transform_handle_ == TransformHandle::TopLeft) {
            handle = top_left; anchor = bottom_right;
        } else if (active_transform_handle_ == TransformHandle::TopRight) {
            handle = top_right; anchor = bottom_left;
        } else if (active_transform_handle_ == TransformHandle::BottomRight) {
            handle = bottom_right; anchor = top_left;
        } else {
            handle = bottom_left; anchor = top_right;
        }
        const QPointF mapped_anchor = map(anchor);
        const qreal initial_distance = vector_length(map(handle) - mapped_anchor);
        if (initial_distance <= 0.0) return;
        const qreal factor = std::clamp(
            vector_length(point - mapped_anchor) / initial_distance,
            kMinimumTransformScale, kMaximumTransformScale);
        next = transform_about(mapped_anchor, 0.0, factor, factor) *
            transform_drag_matrix_;
        break;
    }
    case TransformHandle::Top:
    case TransformHandle::Right:
    case TransformHandle::Bottom:
    case TransformHandle::Left: {
        const bool horizontal = active_transform_handle_ == TransformHandle::Left ||
            active_transform_handle_ == TransformHandle::Right;
        QPointF handle;
        QPointF anchor;
        if (active_transform_handle_ == TransformHandle::Top) {
            handle = top_center; anchor = bottom_center;
        } else if (active_transform_handle_ == TransformHandle::Right) {
            handle = right_center; anchor = left_center;
        } else if (active_transform_handle_ == TransformHandle::Bottom) {
            handle = bottom_center; anchor = top_center;
        } else {
            handle = left_center; anchor = right_center;
        }
        const QPointF mapped_anchor = map(anchor);
        const QPointF axis = horizontal
            ? map(top_right) - map(top_left)
            : map(bottom_left) - map(top_left);
        const qreal axis_length = vector_length(axis);
        if (axis_length <= 0.0) return;
        const QPointF unit_axis(axis.x() / axis_length, axis.y() / axis_length);
        const qreal denominator = dot_product(map(handle) - mapped_anchor, unit_axis);
        if (std::abs(denominator) <= 0.0001) return;
        const qreal factor = std::clamp(
            dot_product(point - mapped_anchor, unit_axis) / denominator,
            kMinimumTransformScale, kMaximumTransformScale);
        const qreal axis_degrees = std::atan2(axis.y(), axis.x()) * kDegreesPerRadian;
        QTransform scale;
        scale.translate(mapped_anchor.x(), mapped_anchor.y());
        scale.rotate(axis_degrees);
        scale.scale(factor, 1.0);
        scale.rotate(-axis_degrees);
        scale.translate(-mapped_anchor.x(), -mapped_anchor.y());
        next = scale * transform_drag_matrix_;
        break;
    }
    case TransformHandle::Rotate: {
        const QPointF center = map(bounds.center());
        const QPointF start_vector = transform_drag_start_ - center;
        const QPointF current_vector = point - center;
        if (vector_length(start_vector) <= 0.0 || vector_length(current_vector) <= 0.0)
            return;
        const qreal start_degrees = std::atan2(start_vector.y(), start_vector.x()) *
            kDegreesPerRadian;
        const qreal current_degrees = std::atan2(current_vector.y(), current_vector.x()) *
            kDegreesPerRadian;
        next = transform_about(center,
                               normalized_angle_delta(current_degrees - start_degrees),
                               1.0, 1.0) * transform_drag_matrix_;
        break;
    }
    case TransformHandle::None:
        return;
    }

    transform_matrix_ = next;
    mask_ = AnnotationRegionEditService::transformed_mask(
        transform_base_mask_, *transform_region_, transform_matrix_);
    rebuild_overlay();
    update();
}

void MaskCanvas::finish_transform_drag() {
    if (!transform_dragging_) return;
    transform_dragging_ = false;
    active_transform_handle_ = TransformHandle::None;
    const QImage before = transform_drag_before_;
    transform_drag_before_ = {};
    finish_edit(before);
}

void MaskCanvas::finish_edit(const QImage& before) {
    if (before.isNull() || mask_ == before) { update(); return; }
    dirty_ = true;
    rebuild_overlay();
    update();
    undo_stack_->push(new MaskCommand(this, before, mask_));
    if (dirty_changed) dirty_changed(true);
}

void MaskCanvas::finish_polygon() {
    if (polygon_points_.size() < 3 || stroke_before_.isNull()) {
        update();
        return;
    }
    QPainter painter(&mask_);
    painter.setPen(Qt::NoPen);
    const int value = class_value_;
    painter.setBrush(QColor(value, value, value));
    QPolygon polygon;
    for (const QPoint& point : polygon_points_) polygon << point;
    painter.drawPolygon(polygon);
    painter.end();
    const QImage before = stroke_before_;
    polygon_points_.clear();
    stroke_before_ = {};
    finish_edit(before);
    if (polygon_active_changed) polygon_active_changed(false);
}

void MaskCanvas::cancel_polygon() {
    const bool active = !polygon_points_.isEmpty();
    polygon_points_.clear();
    stroke_before_ = {};
    hover_point_ = {-1, -1};
    update();
    if (active && polygon_active_changed) polygon_active_changed(false);
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

SegmentationAnnotationWidget::SegmentationAnnotationWidget(QWidget* parent) : QWidget(parent) {
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
        context_bar, tr("Classes"), "annotationToggleClassesButton");
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
    samples_panel_ = samples_panel;
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
    brush_ = make_icon_button(tools_panel, "brush", tr("Brush"),
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
             pan, transform_region, brush_, eraser, rectangle, ellipse, polygon,
             threshold, fill, eyedropper})
        tool_group->addButton(tool);
    pan->setToolTip(tr("Pan (Alt+S)"));
    transform_region->setToolTip(
        tr("Transform region · double-click a region to edit (Alt+M)"));
    brush_->setToolTip(tr("Brush (Alt+B)"));
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
    brush_->setAccessibleName(tr("Brush annotation tool"));
    polygon->setAccessibleName(tr("Polygon annotation tool"));
    rectangle->setAccessibleName(tr("Rectangle annotation tool"));
    ellipse->setAccessibleName(tr("Ellipse annotation tool"));
    threshold->setAccessibleName(tr("Dual-threshold ROI annotation tool"));
    eraser->setAccessibleName(tr("Eraser annotation tool"));
    fill->setAccessibleName(tr("Fill annotation tool"));
    eyedropper->setAccessibleName(tr("Pick annotation class"));
    classes_ = new QComboBox(content);
    classes_->setObjectName("segmentationClassCombo");
    classes_->setEditable(true);
    classes_->setInsertPolicy(QComboBox::NoInsert);
    classes_->setPlaceholderText(tr("Class name"));
    classes_->setMinimumWidth(80);
    add_class_ = new QPushButton(tr("Add class"), content);
    add_class_->setObjectName("segmentationAddClassButton");
    add_class_->setProperty("compact", true);
    brush_size_slider_ = new QSlider(Qt::Horizontal, tools_panel); brush_size_slider_->setObjectName("annotationBrushSizeSlider"); brush_size_slider_->setRange(2, 100); brush_size_slider_->setValue(18); brush_size_slider_->setMaximumWidth(90);
    auto* size_value = new QLabel(tr("18 px"), tools_panel); size_value->setObjectName("annotationBrushSizeValue"); size_value->setMinimumWidth(38);
    tools->addWidget(pan); tools->addWidget(transform_region); tools->addWidget(brush_); tools->addWidget(eraser);
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
    canvas_->set_tool(MaskCanvas::Tool::Pan);
    viewport_layout->addWidget(canvas_, 1);

    auto* classes_panel = new QFrame(content);
    classes_panel_ = classes_panel;
    classes_panel->setObjectName("annotationClassesPanel");
    classes_panel->setMinimumWidth(0);
    auto* classes_layout = new QVBoxLayout(classes_panel);
    classes_layout->setContentsMargins(8, 8, 8, 8);
    classes_layout->setSpacing(8);
    auto* classes_title = new QLabel(tr("CLASSES"), classes_panel);
    classes_title->setProperty("eyebrow", true);
    classes_title->setToolTip(tr("One annotation is shared by all images in the sample."));
    classes_layout->addWidget(classes_title);
    class_empty_hint_ = new QLabel(tr("No classes"), classes_panel);
    class_empty_hint_->setObjectName("segmentationClassEmptyHint");
    class_empty_hint_->setProperty("emptyHint", true);
    class_empty_hint_->setWordWrap(true);
    classes_layout->addWidget(class_empty_hint_);
    class_list_ = new QListWidget(classes_panel);
    class_list_->setObjectName("segmentationClassList");
    class_list_->setIconSize(QSize(14, 14));
    class_list_->setTextElideMode(Qt::ElideMiddle);
    class_list_->setWordWrap(true);
    class_list_->setUniformItemSizes(false);
    classes_layout->addWidget(class_list_, 1);
    class_identity_ = new QLabel(classes_panel);
    class_identity_->setObjectName(
        "segmentationClassIdentity");
    class_identity_->setProperty("meta", true);
    class_identity_->setWordWrap(true);
    class_identity_->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    classes_layout->addWidget(class_identity_);
    classes_layout->addWidget(classes_);
    classes_layout->addWidget(add_class_);
    auto* class_actions = new QGridLayout;
    class_actions->setSpacing(ThemeTokens::Space1);
    auto* rename_class = make_icon_button(classes_panel, "rename", tr("Rename class"),
                                          "segmentationRenameClassButton");
    auto* retire_class = make_icon_button(classes_panel, "trash", tr("Delete class"),
                                          "segmentationDeleteClassButton");
    auto* move_up = make_icon_button(classes_panel, "up", tr("Move class up"),
                                     "segmentationMoveClassUpButton");
    auto* move_down = make_icon_button(classes_panel, "down", tr("Move class down"),
                                       "segmentationMoveClassDownButton");
    auto* lock_class = new QToolButton(classes_panel);
    lock_class->setObjectName("segmentationLockClassButton");
    lock_class->setCheckable(true);
    IconProvider::configure(lock_class, "lock", tr("Lock or unlock the selected class"));
    lock_class->setProperty("compact", true);
    class_actions->addWidget(rename_class, 0, 0);
    class_actions->addWidget(retire_class, 0, 1);
    class_actions->addWidget(lock_class, 0, 2);
    class_actions->addWidget(move_up, 1, 0);
    class_actions->addWidget(move_down, 1, 1);
    class_actions->setColumnStretch(2, 1);
    classes_layout->addLayout(class_actions);

    content->addWidget(samples_panel); content->addWidget(viewport_panel); content->addWidget(classes_panel);
    content->setCollapsible(0, true); content->setCollapsible(1, false); content->setCollapsible(2, true);
    content->setStretchFactor(0, 0); content->setStretchFactor(1, 1); content->setStretchFactor(2, 0);
    content->setSizes({210, 740, 230});
    root->addWidget(content, 1);
    connect(toggle_samples, &QToolButton::toggled, this, [this](bool shown) {
        set_side_panel_width(0, shown, 210);
    });
    connect(toggle_classes, &QToolButton::toggled, this, [this](bool shown) {
        set_side_panel_width(2, shown, 230);
    });
    connect(previous, &QToolButton::clicked, this, [this] { if (current_index_ > 0 && confirm_transition()) load_index(current_index_ - 1); });
    connect(next, &QToolButton::clicked, this, [this] { if (current_index_ + 1 < image_files_.size() && confirm_transition()) load_index(current_index_ + 1); });
    auto select_tool = [this](MaskCanvas::Tool tool) {
        canvas_->set_tool(tool);
        canvas_->setFocus(Qt::ShortcutFocusReason);
        if (tool == MaskCanvas::Tool::TransformRegion)
            state_->setText(
                tr("Transform · double-click a region, then drag its body or handles"));
        else
            state_->setText(canvas_->is_dirty()
                ? tr("Mask has unsaved changes")
                : tr("Ready"));
    };
    connect(pan, &QToolButton::clicked, canvas_, [select_tool] { select_tool(MaskCanvas::Tool::Pan); });
    connect(transform_region, &QToolButton::clicked, canvas_, [select_tool] {
        select_tool(MaskCanvas::Tool::TransformRegion);
    });
    connect(brush_, &QToolButton::clicked, canvas_, [select_tool] { select_tool(MaskCanvas::Tool::Brush); });
    connect(polygon, &QToolButton::clicked, canvas_, [this] {
        canvas_->set_tool(MaskCanvas::Tool::Polygon);
        canvas_->setFocus(Qt::ShortcutFocusReason);
        state_->setText(tr("Polygon · Right-click/Enter finish · Esc cancel"));
    });
    connect(rectangle, &QToolButton::clicked, canvas_, [select_tool] { select_tool(MaskCanvas::Tool::Rectangle); });
    connect(ellipse, &QToolButton::clicked, canvas_, [select_tool] {
        select_tool(MaskCanvas::Tool::Ellipse);
    });
    connect(threshold, &QToolButton::clicked, canvas_, [this] {
        canvas_->set_tool(MaskCanvas::Tool::ThresholdRoi);
        canvas_->setFocus(Qt::ShortcutFocusReason);
        state_->setText(tr("Dual threshold · drag an ROI to preview"));
    });
    connect(eraser, &QToolButton::clicked, canvas_, [select_tool] { select_tool(MaskCanvas::Tool::Eraser); });
    connect(fill, &QToolButton::clicked, canvas_, [select_tool] { select_tool(MaskCanvas::Tool::Fill); });
    connect(eyedropper, &QToolButton::clicked, canvas_, [select_tool] { select_tool(MaskCanvas::Tool::Pick); });
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
    connect(classes_, qOverload<int>(&QComboBox::currentIndexChanged), canvas_, [this](int index) {
        if (rebuilding_classes_) return;
        canvas_->set_class_value(index >= 0 ? current_mask_value() : 0);
        if (class_list_ && class_list_->currentRow() != index) class_list_->setCurrentRow(index);
        update_class_state();
    });
    connect(class_list_, &QListWidget::currentRowChanged, classes_, [this](int index) {
        if (rebuilding_classes_) return;
        if (index >= 0 && classes_->currentIndex() != index) classes_->setCurrentIndex(index);
    });
    canvas_->class_picked = [this](int value) {
        const int index = class_index_for_mask_value(value);
        if (index >= 0 && index < classes_->count()) {
            classes_->setCurrentIndex(index);
            state_->setText(tr("Picked class '%1'").arg(classes_->itemText(index)));
        } else {
            state_->setText(tr("Background pixel - class unchanged"));
        }
    };
    canvas_->class_required = [this] { return prompt_for_class(); };
    canvas_->polygon_active_changed = [this](bool active) {
        state_->setText(active
            ? tr("Polygon · Right-click/Enter finish · Esc cancel")
            : canvas_->is_dirty()
                ? tr("Mask has unsaved changes")
                : tr("Ready"));
    };
    canvas_->threshold_roi_selected = [this](const QRect& roi, const QImage& image) {
        ThresholdDialog dialog(image, this);
        if (dialog.exec() == QDialog::Accepted) {
            canvas_->apply_threshold_roi(
                roi, dialog.intervals(), dialog.keep_outside_range());
            state_->setText(tr("Dual-threshold ROI applied · undo is available"));
        } else {
            state_->setText(tr("Dual-threshold ROI cancelled"));
        }
    };
    connect(add_class_, &QPushButton::clicked, this, [this] {
        const QString name = classes_->currentText().trimmed();
        if (name.isEmpty()) { prompt_for_class(); return; }
        add_or_select_class(name);
    });
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
    connect(rename_class, &QToolButton::clicked, this, [this] {
        const QString id = current_class_id();
        if (id.isEmpty()) return;
        bool accepted = false;
        const QString current = classes_->currentText();
        const QString renamed = QInputDialog::getText(
            this, tr("Rename class"), tr("Class name"), QLineEdit::Normal,
            current, &accepted).trimmed();
        if (!accepted || renamed.isEmpty() || renamed == current) return;
        if (class_rename_requested) class_rename_requested(id, renamed);
        else {
            for (auto& value : configured_classes_)
                if (QString::fromStdString(value.id) == id) value.name = renamed.toStdString();
            apply_class_schema();
        }
    });
    connect(retire_class, &QToolButton::clicked, this, [this] {
        const QString id = current_class_id();
        if (id.isEmpty()) return;
        if (QMessageBox::question(
                this, tr("Delete class"),
                tr("Retire '%1'? Existing mask pixels keep their stable mask value.")
                    .arg(classes_->currentText()),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return;
        if (class_retire_requested) class_retire_requested(id);
        else {
            for (auto& value : configured_classes_) {
                if (QString::fromStdString(value.id) != id) continue;
                value.active = false; value.visible = false; value.locked = true;
            }
            apply_class_schema();
        }
    });
    auto move_selected = [this](int direction) {
        const QString id = current_class_id();
        if (id.isEmpty()) return;
        if (class_move_requested) class_move_requested(id, direction);
        else {
            auto found = std::find_if(configured_classes_.begin(), configured_classes_.end(),
                                      [&id](const AnnotationClass& value) {
                                          return QString::fromStdString(value.id) == id;
                                      });
            if (found == configured_classes_.end()) return;
            const auto index = std::distance(configured_classes_.begin(), found);
            const auto target = index + (direction < 0 ? -1 : 1);
            if (target < 0 || target >= static_cast<decltype(index)>(configured_classes_.size())) return;
            std::iter_swap(configured_classes_.begin() + index, configured_classes_.begin() + target);
            apply_class_schema();
        }
    };
    connect(move_up, &QToolButton::clicked, this, [move_selected] { move_selected(-1); });
    connect(move_down, &QToolButton::clicked, this, [move_selected] { move_selected(1); });
    connect(lock_class, &QToolButton::toggled, this, [this](bool locked) {
        if (rebuilding_classes_) return;
        const QString id = current_class_id();
        if (id.isEmpty()) return;
        if (class_lock_changed) class_lock_changed(id, locked);
        else {
            for (auto& value : configured_classes_)
                if (QString::fromStdString(value.id) == id) value.locked = locked;
            update_class_state();
        }
    });
    connect(class_list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (rebuilding_classes_ || !item) return;
        const QString id = item->data(Qt::UserRole).toString();
        const bool visible = item->checkState() == Qt::Checked;
        if (class_visibility_changed) class_visibility_changed(id, visible);
        else {
            for (auto& value : configured_classes_)
                if (QString::fromStdString(value.id) == id) value.visible = visible;
            apply_class_schema();
        }
    });
    connect(class_list_, &QListWidget::currentRowChanged, this, [this, lock_class](int index) {
        if (rebuilding_classes_) return;
        if (index >= 0 && classes_->currentIndex() != index) classes_->setCurrentIndex(index);
        const QString id = current_class_id();
        bool locked = false;
        for (const auto& value : configured_classes_)
            if (QString::fromStdString(value.id) == id) locked = value.locked;
        rebuilding_classes_ = true;
        lock_class->setChecked(locked);
        rebuilding_classes_ = false;
    });
    auto add_shortcut = [this](const QKeySequence& sequence, QAbstractButton* button) {
        auto* shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        shortcut->setAutoRepeat(false);
        connect(shortcut, &QShortcut::activated, button, &QAbstractButton::click);
    };
    add_shortcut(QKeySequence("Alt+S"), pan);
    add_shortcut(QKeySequence("Alt+M"), transform_region);
    add_shortcut(QKeySequence("Alt+B"), brush_);
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
    update_class_state();
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
    const bool show_classes = mode == 0;
    {
        const QSignalBlocker classes_blocker(toggle_classes_);
        toggle_classes_->setChecked(show_classes);
    }
    const int center_width = std::max(
        560, available_width - (show_samples ? 210 : 0) -
                 (show_classes ? 230 : 0));
    content_splitter_->setSizes({
        show_samples ? 210 : 0,
        center_width,
        show_classes ? 230 : 0});
    content_splitter_->setProperty(
        "responsiveMode",
        mode == 0 ? "wide" : mode == 1 ? "narrow" : "compact");
}

void SegmentationAnnotationWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const bool compact = event->size().width() < 900;
    const bool short_layout = event->size().height() < 360;
    if (state_) state_->setVisible(!compact);
    if (class_identity_ && classes_)
        class_identity_->setVisible(
            !short_layout && classes_->currentIndex() >= 0);
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
    if (class_list_)
        class_list_->setIconSize(
            compact ? QSize(12, 12) : QSize(14, 14));
    apply_responsive_layout();
    update_context_label();
}

void SegmentationAnnotationWidget::set_task_dataset(
    const QString& directory,
    const std::vector<TaskSampleBinding>& samples,
    const std::vector<AnnotationClass>& classes)
{
    if (!confirm_transition()) return;
    dataset_directory_ = directory;
    configured_classes_ = classes;
    imported_image_paths_.clear();
    sample_bindings_.clear();
    sample_ids_.clear();
    sample_image_sets_.clear();
    display_names_.clear();
    for (int sample_index = 0;
         sample_index < static_cast<int>(samples.size());
         ++sample_index) {
        const auto& sample = samples[static_cast<size_t>(sample_index)];
        if (!sample.selected || sample.image_paths.empty()) continue;
        QStringList images;
        for (const auto& path : sample.image_paths)
            images.push_back(QFileInfo(QString::fromStdString(path)).absoluteFilePath());
        const QString primary = images.front();
        imported_image_paths_.push_back(primary);
        sample_bindings_.push_back(sample);
        sample_ids_.push_back(QString::fromStdString(sample.sample_id));
        sample_image_sets_.push_back(images);
        display_names_.push_back(image_display_name(sample_index, sample));

    }
    reload_dataset();
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

void SegmentationAnnotationWidget::set_class_schema(
    const std::vector<AnnotationClass>& classes)
{
    configured_classes_ = classes;
    apply_class_schema();
    refresh_sample_status();
    update_context_label();
}

void SegmentationAnnotationWidget::set_inference_result(const InferenceResultSet* result) {
    inference_results_ = result ? result->samples : std::vector<ResultReference>{};
    refresh_inference_layers();
}

void SegmentationAnnotationWidget::refresh_inference_layers() {
    if (!canvas_ || current_index_ < 0 || current_index_ >= image_files_.size()) {
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
    for (const auto& reference : inference_results_) {
        const auto* value = std::get_if<jocelin::result::sg::SgResult>(&reference.result);
        if (!value || current_index_ >= static_cast<int>(sample_bindings_.size()) ||
            reference.source_sample_id != sample_bindings_[static_cast<size_t>(current_index_)].sample_id ||
            QFileInfo(QString::fromStdString(value->image_path)).absoluteFilePath() !=
                QFileInfo(image_files_[current_index_]).absoluteFilePath()) continue;
        canvas_->set_prediction_overlay(prediction_overlay_from_result(*value));
        if (!value->result_name.empty() && !value->save_dir.empty())
            canvas_->set_heatmap_overlay(QImage(QDir(QString::fromStdString(value->save_dir))
                .filePath(QString::fromStdString(value->result_name) + "_heatmap.png")));
        break;
    }
    QImage training_mask;
    if (current_index_ < static_cast<int>(sample_bindings_.size())) {
        const auto& path = sample_bindings_[static_cast<size_t>(current_index_)].training_mask_path;
        if (!path.empty()) training_mask = QImage(QString::fromStdString(path));
    }
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

void SegmentationAnnotationWidget::update_class_state() {
    const bool has_classes = classes_ && classes_->count() > 0;
    const bool has_class = has_classes && classes_->currentIndex() >= 0;
    bool locked = false;
    const QString selected_id = current_class_id();
    for (const auto& value : configured_classes_)
        if (QString::fromStdString(value.id) == selected_id) locked = value.locked;
    if (brush_) {
        brush_->setEnabled(true);
        brush_->setToolTip(locked ? tr("Brush (locked)") : tr("Brush (Alt+B)"));
    }
    if (canvas_) {
        canvas_->set_class_value(has_class ? current_mask_value() : 0);
        canvas_->set_editing_enabled(!locked);
    }
    for (const char* name : {
             "segmentationPolygonButton", "segmentationRectangleButton",
             "segmentationEllipseButton", "segmentationThresholdButton",
             "segmentationFillButton"}) {
        if (auto* button = findChild<QToolButton*>(name)) {
            button->setEnabled(true);
            const QString object_name = QString::fromLatin1(name);
            if (object_name.contains("Polygon"))
                button->setToolTip(locked ? tr("Polygon (locked)") : tr("Polygon (Alt+P)"));
            else if (object_name.contains("Rectangle"))
                button->setToolTip(locked ? tr("Rectangle (locked)") : tr("Rectangle (Alt+R)"));
            else if (object_name.contains("Ellipse"))
                button->setToolTip(locked ? tr("Ellipse (locked)") : tr("Ellipse (Alt+O)"));
            else if (object_name.contains("Threshold"))
                button->setToolTip(locked ? tr("Dual threshold (locked)") : tr("Dual-threshold ROI (Alt+T)"));
            else
                button->setToolTip(locked ? tr("Fill (locked)") : tr("Fill (Alt+F)"));
        }
    }
    if (class_empty_hint_) class_empty_hint_->setVisible(!has_classes);
    if (class_list_) class_list_->setVisible(has_classes);
    if (class_identity_) {
        class_identity_->setVisible(has_class);
        class_identity_->setText(has_class
            ? tr("Mask %1 · %2").arg(current_mask_value()).arg(selected_id)
            : QString{});
        class_identity_->setToolTip(has_class
            ? tr("Stable class ID: %1\nMask value: %2")
                .arg(selected_id)
                .arg(current_mask_value())
            : QString{});
    }
    for (const char* name : {
             "segmentationRenameClassButton",
             "segmentationDeleteClassButton",
             "segmentationLockClassButton",
             "segmentationMoveClassUpButton",
             "segmentationMoveClassDownButton"})
        if (auto* button = findChild<QToolButton*>(name))
            button->setVisible(has_classes);
    if (add_class_) {
        add_class_->setText(has_classes ? tr("Add class") : tr("Add first class..."));
        add_class_->setProperty("primary", !has_classes);
        add_class_->style()->unpolish(add_class_);
        add_class_->style()->polish(add_class_);
    }
}

int SegmentationAnnotationWidget::class_index_for_mask_value(int mask_value) const {
    if (!classes_) return -1;
    for (int index = 0; index < classes_->count(); ++index)
        if (classes_->itemData(index, Qt::UserRole + 1).toInt() == mask_value)
            return index;
    return -1;
}

int SegmentationAnnotationWidget::current_mask_value() const {
    return classes_ && classes_->currentIndex() >= 0
        ? classes_->currentData(Qt::UserRole + 1).toInt()
        : 0;
}

QString SegmentationAnnotationWidget::current_class_id() const {
    return classes_ && classes_->currentIndex() >= 0
        ? classes_->currentData(Qt::UserRole).toString()
        : QString{};
}

void SegmentationAnnotationWidget::apply_class_schema() {
    if (!classes_ || !class_list_ || !canvas_) return;
    const QString selected_id = current_class_id();
    rebuilding_classes_ = true;
    const QSignalBlocker combo_blocker(classes_);
    classes_->clear();
    QHash<int, QColor> colors;
    QSet<int> hidden;
    for (const auto& value : configured_classes_) {
        const QColor color(QString::fromStdString(value.color));
        colors.insert(value.mask_value, color.isValid() ? color : class_color(value.mask_value));
        if (!value.visible) hidden.insert(value.mask_value);
        if (!value.active) continue;
        classes_->addItem(QString::fromStdString(value.name));
        const int index = classes_->count() - 1;
        classes_->setItemData(index, QString::fromStdString(value.id), Qt::UserRole);
        classes_->setItemData(index, value.mask_value, Qt::UserRole + 1);
    }
    int selected = -1;
    for (int index = 0; index < classes_->count(); ++index)
        if (classes_->itemData(index, Qt::UserRole).toString() == selected_id)
            selected = index;
    if (selected < 0 && classes_->count() > 0) selected = 0;
    classes_->setCurrentIndex(selected);
    canvas_->set_class_colors(colors);
    canvas_->set_hidden_class_values(hidden);
    rebuilding_classes_ = false;
    rebuild_class_list();
    update_class_state();
}

int SegmentationAnnotationWidget::add_or_select_class(const QString& requested_name) {
    const QString name = requested_name.trimmed();
    if (name.isEmpty()) return 0;
    int index = classes_->findText(name, Qt::MatchFixedString);
    const bool added = index < 0;
    if (added && class_add_requested) {
        class_add_requested(name);
        index = classes_->findText(name, Qt::MatchFixedString);
    } else if (added) {
        state_->setText(tr("Class creation is unavailable."));
        return 0;
    }
    if (index < 0) return 0;
    classes_->setCurrentIndex(index);
    state_->setText(added ? tr("Added class '%1'").arg(name) : tr("Selected class '%1'").arg(name));
    return current_mask_value();
}

int SegmentationAnnotationWidget::prompt_for_class() {
    QStringList names;
    for (int index = 0; index < classes_->count(); ++index) names.push_back(classes_->itemText(index));
    bool accepted = false;
    const QString selected = QInputDialog::getItem(
        this,
        names.isEmpty() ? tr("Create the first annotation class") : tr("Choose annotation class"),
        names.isEmpty() ? tr("Class name") : tr("Choose an existing class or type a new name"),
        names, std::max(0, classes_->currentIndex()), true, &accepted).trimmed();
    if (!accepted || selected.isEmpty()) {
        state_->setText(tr("Annotation cancelled - no class selected"));
        return 0;
    }
    return add_or_select_class(selected);
}

void SegmentationAnnotationWidget::rebuild_class_list() {
    if (!class_list_ || !classes_) return;
    const int selected = classes_->currentIndex();
    rebuilding_classes_ = true;
    const QSignalBlocker list_blocker(class_list_);
    class_list_->clear();
    for (int index = 0; index < classes_->count(); ++index) {
        const QString id = classes_->itemData(index, Qt::UserRole).toString();
        const int mask_value = classes_->itemData(index, Qt::UserRole + 1).toInt();
        auto schema = std::find_if(configured_classes_.begin(), configured_classes_.end(),
                                   [&id](const AnnotationClass& value) {
                                       return QString::fromStdString(value.id) == id;
                                   });
        QPixmap chip(14, 14);
        QColor color = schema == configured_classes_.end()
            ? class_color(mask_value)
            : QColor(QString::fromStdString(schema->color));
        if (!color.isValid()) color = class_color(mask_value);
        chip.fill(color);
        const bool locked = schema != configured_classes_.end() && schema->locked;
        const QString text =
            classes_->itemText(index) +
            (locked ? tr(" · locked") : QString{});
        auto* item = new QListWidgetItem(
            QIcon(chip), text, class_list_);
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, mask_value);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(schema == configured_classes_.end() || schema->visible
                                ? Qt::Checked : Qt::Unchecked);
        item->setSizeHint(QSize(0, 42));
        item->setToolTip(tr("Stable class ID: %1\nMask value: %2").arg(id).arg(mask_value));
    }
    class_list_->setCurrentRow(selected);
    rebuilding_classes_ = false;
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
        const int row_index = row;
        const int image_count = row_index < sample_image_sets_.size()
            ? sample_image_sets_[row_index].size() : 1;
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
    if (!canvas_ || !canvas_->is_dirty()) return true;
    const auto answer = QMessageBox::warning(this, tr("Unsaved mask"), tr("Save the current mask before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) { save_current(); return !canvas_->is_dirty(); }
    return true;
}

void SegmentationAnnotationWidget::reload_dataset() {
    image_files_.clear(); classes_->clear(); current_index_ = -1;
    if (!imported_image_paths_.isEmpty()) {
        for (const QString& path : imported_image_paths_) {
            QImageReader reader(path);
            reader.setDecideFormatFromContent(true);
            if (QFileInfo(path).isFile() && reader.canRead()) image_files_.push_back(QFileInfo(path).absoluteFilePath());
        }
    } else {
        QDir image_dir(QDir(dataset_directory_).filePath("images"));
        if (!image_dir.exists()) image_dir = QDir(dataset_directory_);
        const QStringList names = image_dir.entryList(
            {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tif", "*.tiff", "*.webp"},
            QDir::Files, QDir::Name);
        for (const QString& name : names) image_files_.push_back(image_dir.absoluteFilePath(name));
    }
    if (sample_ids_.size() != image_files_.size() ||
        display_names_.size() != image_files_.size() ||
        static_cast<int>(sample_bindings_.size()) != image_files_.size()) {
        sample_bindings_.clear();
        sample_ids_.clear();
        sample_image_sets_.clear();
        display_names_.clear();
        for (int row = 0; row < image_files_.size(); ++row) {
            const QString image = image_files_[row];
            TaskSampleBinding sample;
            sample.sample_id = stable_sample_id(
                {QFileInfo(image).absoluteFilePath().toStdString()});
            sample.image_paths = {QFileInfo(image).absoluteFilePath().toStdString()};
            sample_bindings_.push_back(sample);
            sample_ids_.push_back(QString::fromStdString(sample.sample_id));
            sample_image_sets_.push_back({image});
            display_names_.push_back(image_display_name(row, sample));
        }
    }
    apply_class_schema();
    std::vector<SampleBrowserEntry> browser_entries;
    browser_entries.reserve(static_cast<size_t>(image_files_.size()));
    for (int row = 0; row < image_files_.size(); ++row) {
        SampleBrowserEntry entry;
        entry.sample_id = row < static_cast<int>(sample_bindings_.size())
            ? sample_bindings_[static_cast<size_t>(row)].sample_id
            : std::string{};
        entry.image_path = image_files_[row];
        entry.display_name = display_names_.value(row);
        entry.source_index = row;
        browser_entries.push_back(std::move(entry));
    }
    images_->set_entries(browser_entries);
    refresh_sample_status();
    if (image_files_.isEmpty()) {
        context_->setText(tr("No dataset selected"));
        context_->setToolTip({});
        state_->setText(tr("No valid imported images are connected."));
    }
    else if (classes_->count() == 0) state_->setText(tr("%1 image(s) loaded. Add a class before annotating.").arg(image_files_.size()));
    else  state_->setText(tr("Ready"));
    if (!image_files_.isEmpty()) load_index(0);
}

void SegmentationAnnotationWidget::load_index(int index) {
    if (index < 0 || index >= image_files_.size()) return;
    const QString path = image_files_[index];
    QString error;
    if (!canvas_->load(path, {}, &error)) {
        state_->setText(error); if (status) status(error); return;
    }
    if (index < static_cast<int>(sample_bindings_.size()))
        canvas_->apply_mask(segmentation_annotation_image(sample_bindings_[static_cast<size_t>(index)]), false);
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
        current_index_ >= image_files_.size())
        return;
    const QString path = image_files_[current_index_];
    const QSize dimensions = QImageReader(path).size();
    const QString concise =
        tr("%1 / %2  |  %3  |  %4 × %5")
            .arg(current_index_ + 1)
            .arg(image_files_.size())
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
    if (current_index_ < 0) return false;
    if (!canvas_->is_dirty()) return true;
    if (!mask_saved) return false;
    mask_saved(QFileInfo(image_files_[current_index_]).absoluteFilePath(), canvas_->mask_image());
    canvas_->mark_saved();
    refresh_sample_status();
    update_context_label();
    state_->setText(tr("Annotation updated"));
    if (status) status(state_->text());
    return true;
}

} // namespace studio
