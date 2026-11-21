#include "modules/task/ui/annotation/od/canvas/detection_canvas.h"

namespace studio {

DetectionCanvas::DetectionCanvas(QWidget* parent) : QWidget(parent)
{
    setObjectName("odAnnotationCanvas");
    setMinimumSize(280, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    set_tool(AnnotationTool::Rectangle);
}

void DetectionCanvas::load(QImage image, const std::vector<Box>& boxes)
{
    history.clear();
    boxes_ = boxes;
    image_ = std::move(image);
    cancel_edit();
    fit();
}

void DetectionCanvas::cancel_edit()
{
    gesture_.reset();
    transform_.reset();
    pan_.end();
    selected_ = -1;
    move_mode_ = false;
    update_cursor();
    update();
    selection_changed();
}

void DetectionCanvas::set_tool(AnnotationTool tool)
{
    gesture_.set_tool(tool);
    cancel_edit();
}

void DetectionCanvas::set_layers(bool annotations, bool predictions)
{
    show_annotations_ = annotations;
    show_predictions_ = predictions;
    if (!annotations) {
        cancel_edit();
    }
    if (!predictions && result_popup_) {
        result_popup_->close();
    }
    update();
}

void DetectionCanvas::set_classes(const std::vector<AnnotationClass>& classes)
{
    if (result_popup_) {
        result_popup_->close();
    }
    classes_ = classes;
    if (!selected_box()) {
        cancel_edit();
    }
    update_cursor();
    update();
}

void DetectionCanvas::set_prediction(const jocelin::result::od::OdResult* result,
                        const std::vector<jocelin::model::ModelClass>& classes)
{
    if (result_popup_) {
        result_popup_->close();
    }
    prediction_classes_ = classes;
    predictions_.clear();
    if (result && image_.size() == QSize(result->source_size.width, result->source_size.height)) {
        predictions_ = result->detections;
    }
    update();
}

void DetectionCanvas::fit()
{
    viewport_.reset();
    update();
    zoom_changed();
}

void DetectionCanvas::zoom_at(const QPointF& anchor, double factor)
{
    if (viewport_.zoom_at(anchor, factor, image_.size(), size())) {
        zoom_changed();
        update();
    }
}

const std::vector<Box>& DetectionCanvas::boxes() const
{ return boxes_; }

bool DetectionCanvas::has_image() const
{ return !image_.isNull(); }

int DetectionCanvas::zoom_percent() const
{ return qRound(viewport_.zoom() * 100); }

bool DetectionCanvas::selected_box() const
{
    return show_annotations_ && selected_ >= 0 && selected_ < static_cast<int>(boxes_.size()) &&
        visible(boxes_[static_cast<size_t>(selected_)].class_id);
}

void DetectionCanvas::remove_selected()
{
    if (selected_box()) {
        const int index = selected_;
        const Box before = boxes_[static_cast<size_t>(index)];
        cancel_edit();
        history.push(new DetectionBoxEdit(boxes_, index, before, std::nullopt));
    }
}

void DetectionCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    if (image_.isNull()) {
        painter.setPen(QColor("#B8C8C6"));
        painter.drawText(rect(), Qt::AlignCenter, empty_text);
        return;
    }
    const QRectF target = image_rect();
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, image_);
    painter.setClipRect(target.intersected(rect()));
    painter.setRenderHint(QPainter::Antialiasing);
    if (show_predictions_) {
        for (const Box& box : predictions_) {
            paint_box(painter, box, detection_box_rect(box), true, false);
        }
    }
    if (show_annotations_) {
        for (int index = 0; index < static_cast<int>(boxes_.size()); ++index) {
            const Box& box = boxes_[static_cast<size_t>(index)];
            const bool selected = index == selected_;
            const QRectF outline = selected && transform_.dragging()
                ? moved_rect() : detection_box_rect(box);
            paint_box(painter, box, outline, false, selected);
        }
        if (gesture_.painting() || !draft_.isEmpty()) {
            const QRectF preview = gesture_.painting()
                ? QRectF(QRect(gesture_.shape_start(), gesture_.hover_point()).normalized()) : draft_;
            painter.setPen(QPen(Qt::white, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(to_widget(preview));
        }
    }
}

void DetectionCanvas::mousePressEvent(QMouseEvent* event)
{
    if (image_.isNull()) {
        return;
    }
    setFocus(Qt::MouseFocusReason);
    const QPointF point = event->localPos();
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        pan_.begin(point);
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const QPoint image_point = viewport_.map_to_image(point, image_.size(), size());
    const int hit = box_at(point);
    if (move_mode_ && hit == selected_ && selected_box()) {
        transform_.begin_drag(SelectionTransformInteraction::Handle::Body, to_source(point));
        update_cursor();
        return;
    }
    move_mode_ = false;
    selected_ = gesture_.tool() == AnnotationTool::Rectangle ? -1 : hit;
    if (can_draw()) {
        gesture_.press(image_point);
    }
    selection_changed();
    update_cursor();
    update();
}

void DetectionCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const int hit = box_at(event->localPos());
    if (hit < 0) {
        if (show_predictions_ && image_rect().contains(event->localPos())) {
            const QPointF source = to_source(event->localPos());
            const auto prediction = std::find_if(predictions_.rbegin(), predictions_.rend(),
                [this, &source](const Box& box) {
                    return visible(box.class_id) && detection_box_rect(box).contains(source);
                });
            if (prediction != predictions_.rend()) {
                gesture_.reset();
                if (result_popup_) {
                    result_popup_->close();
                }
                result_popup_ = show_result_properties(this, event->globalPos(),
                    present_detection(*prediction, prediction_classes_, classes_));
            }
        }
        return;
    }
    gesture_.reset();
    transform_.reset();
    selected_ = hit;
    move_mode_ = true;
    selection_changed();
    update_cursor();
    update();
}

void DetectionCanvas::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF point = event->localPos();
    if (pan_.active()) {
        viewport_.pan_by(pan_.move(point));
    } else if (transform_.dragging() && selected_box()) {
        transform_.update_drag(detection_box_rect(boxes_[static_cast<size_t>(selected_)]), to_source(point));
    } else if (gesture_.painting()) {
        gesture_.move(clamped_pixel(point));
    }
    update();
}

void DetectionCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (pan_.active() && (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)) {
        pan_.end();
        update_cursor();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (transform_.dragging() && selected_box()) {
        const int index = selected_;
        const Box before = boxes_[static_cast<size_t>(index)];
        transform_.update_drag(detection_box_rect(before), to_source(event->localPos()));
        const QPointF center = moved_rect().center();
        transform_.reset();
        if (center != detection_box_rect(before).center()) {
            Box after = before;
            after.box.x_center = center.x();
            after.box.y_center = center.y();
            history.push(new DetectionBoxEdit(boxes_, index, before, after));
        }
        update_cursor();
    } else if (gesture_.painting()) {
        gesture_.move(clamped_pixel(event->localPos()));
        const AnnotationGesture completed = gesture_.release();
        if (completed.kind == AnnotationGesture::Kind::Rectangle &&
            completed.rect.width() > 1 && completed.rect.height() > 1 &&
            QLineF(to_widget(completed.rect).topLeft(), to_widget(completed.rect).bottomRight()).length()
                >= QApplication::startDragDistance()) {
            finish_box(completed.rect);
        }
    }
    update();
}

void DetectionCanvas::wheelEvent(QWheelEvent* event)
{
    zoom_at(event->position(), std::pow(1.2, event->angleDelta().y() / 120.0));
    event->accept();
}

QRectF DetectionCanvas::image_rect() const
{ return viewport_.target_rect(image_.size(), size()); }

QPointF DetectionCanvas::to_source(const QPointF& point) const
{
    const QRectF target = image_rect();
    return {(point.x() - target.x()) * image_.width() / target.width(),
            (point.y() - target.y()) * image_.height() / target.height()};
}

QRectF DetectionCanvas::to_widget(const QRectF& box) const
{
    const QRectF target = image_rect();
    const double sx = target.width() / image_.width();
    const double sy = target.height() / image_.height();
    return {target.x() + box.x() * sx, target.y() + box.y() * sy,
            box.width() * sx, box.height() * sy};
}

QPoint DetectionCanvas::clamped_pixel(const QPointF& point) const
{
    const QPointF source = to_source(point);
    return {static_cast<int>(std::clamp(source.x(), 0.0, image_.width() - 1.0)),
            static_cast<int>(std::clamp(source.y(), 0.0, image_.height() - 1.0))};
}

QRectF DetectionCanvas::moved_rect() const
{
    const QRectF before = detection_box_rect(boxes_[static_cast<size_t>(selected_)]);
    const QRectF moved = transform_.matrix().mapRect(before);
    return {std::clamp(moved.x(), 0.0, std::max(0.0, image_.width() - before.width())),
            std::clamp(moved.y(), 0.0, std::max(0.0, image_.height() - before.height())),
            before.width(), before.height()};
}

bool DetectionCanvas::can_draw() const
{
    return show_annotations_ && gesture_.tool() == AnnotationTool::Rectangle;
}

void DetectionCanvas::update_cursor()
{
    setCursor(transform_.dragging() ? Qt::ClosedHandCursor :
              move_mode_ ? Qt::SizeAllCursor : can_draw() ? Qt::CrossCursor : Qt::ArrowCursor);
}

const AnnotationClass* DetectionCanvas::find_class(const std::string& id) const
{
    const auto found = std::find_if(classes_.begin(), classes_.end(),
        [&id](const AnnotationClass& value) { return value.id == id; });
    return found == classes_.end() ? nullptr : &*found;
}

bool DetectionCanvas::visible(const std::string& id) const
{
    const AnnotationClass* value = find_class(id);
    return !value || value->visible;
}

int DetectionCanvas::box_at(const QPointF& point) const
{
    if (!show_annotations_ || !image_rect().contains(point)) {
        return -1;
    }
    const QPointF source = to_source(point);
    for (int index = static_cast<int>(boxes_.size()) - 1; index >= 0; --index) {
        const Box& box = boxes_[static_cast<size_t>(index)];
        if (visible(box.class_id) && detection_box_rect(box).contains(source)) {
            return index;
        }
    }
    return -1;
}

void DetectionCanvas::paint_box(QPainter& painter, const Box& box, const QRectF& outline,
                   bool prediction, bool selected) const
{
    if (!visible(box.class_id)) {
        return;
    }
    const AnnotationClass* value = find_class(box.class_id);
    QString name;
    QColor color("#458BF0");
    if (value) {
        name = QString::fromStdString(value->name);
        const QColor configured(QString::fromStdString(value->color));
        color = configured.isValid() ? configured : QColor("#0EA67A");
    }
    if (prediction || !value) {
        const auto found = std::find_if(prediction_classes_.begin(), prediction_classes_.end(),
            [&box](const jocelin::model::ModelClass& cls) { return cls.id == box.class_id; });
        name = QString::fromStdString(found == prediction_classes_.end() ? box.class_id : found->name);
    }
    const QString score = prediction && box.score
        ? QStringLiteral(" %1").arg(QString::number(*box.score, 'f', 2)) : QString{};
    paint_labeled_box(painter, to_widget(outline), image_rect().intersected(rect()),
                      name, score, color, prediction, selected);
}

void DetectionCanvas::finish_box(const QRect& rect)
{
    draft_ = rect;
    update();
    const std::string class_id = request_class ? request_class() : std::string{};
    draft_ = {};
    if (!class_id.empty()) {
        const QPointF center = QRectF(rect).center();
        Box box{class_id,
                {0.0, center.x(), center.y(), double(rect.width()), double(rect.height())}, std::nullopt};
        history.push(new DetectionBoxEdit(boxes_, static_cast<int>(boxes_.size()), std::nullopt, box));
    }
}

} // namespace studio
