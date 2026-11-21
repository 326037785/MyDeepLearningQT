#include "modules/task/ui/results/image_result_canvas.h"

#include <QImageReader>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>

namespace studio {

void paint_labeled_box(QPainter& painter, const QRectF& frame, const QRectF& clip,
                       const QString& name, const QString& score, const QColor& color,
                       bool prediction, bool selected) {
    if (!frame.intersects(clip)) {
        return;
    }
    painter.save();
    painter.setBrush(Qt::NoBrush);
    if (selected || prediction) {
        painter.setPen(QPen(Qt::white, selected ? 5 : 2));
        painter.drawRect(frame);
    }
    painter.setPen(QPen(color, selected ? 3 : 2, prediction ? Qt::DashLine : Qt::SolidLine));
    painter.drawRect(frame);
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const int text_width = std::max(0, static_cast<int>(clip.width()) - 12 - metrics.horizontalAdvance(score));
    const QString caption = metrics.elidedText(name, Qt::ElideRight, text_width) + score;
    const int label_height = metrics.height() + 6;
    const int label_width = metrics.horizontalAdvance(caption) + 12;
    const QRectF label(std::clamp(frame.left(), clip.left(), std::max(clip.left(), clip.right() - label_width)),
                       frame.top() - label_height >= clip.top() ? frame.top() - label_height
                                                               : std::max(frame.top(), clip.top()),
                       label_width, label_height);
    painter.fillRect(label, color);
    painter.setPen(qGray(color.rgb()) < 135 ? Qt::white : Qt::black);
    painter.drawText(label.adjusted(6, 0, -6, 0), Qt::AlignVCenter, caption);
    painter.restore();
}

ImageResultCanvas::ImageResultCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(280, 160);
    setCursor(Qt::CrossCursor);
}

bool ImageResultCanvas::load(const QString& image_path, QString* error) {
    QImageReader reader(image_path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        if (error) {
            *error = tr("Cannot read image: %1").arg(image_path);
        }
        return false;
    }
    image_ = image.convertToFormat(QImage::Format_RGB888);
    prediction_overlay_ = {};
    set_result_items({}, {});
    viewport_.reset();
    pan_.end();
    update();
    return true;
}

void ImageResultCanvas::clear() {
    image_ = {};
    prediction_overlay_ = {};
    set_result_items({}, {});
    viewport_.reset();
    pan_.end();
    update();
}

void ImageResultCanvas::set_prediction_overlay(const QImage& image) {
    prediction_overlay_ = image;
    update();
}

void ImageResultCanvas::set_result_items(QVector<ResultPresentationItem> items, const QSize& source_size) {
    if (result_popup_) {
        result_popup_->close();
    }
    result_items_ = image_.size() == source_size ? std::move(items) : QVector<ResultPresentationItem>{};
    update();
}

void ImageResultCanvas::set_prediction_visible(bool visible) {
    prediction_visible_ = visible;
    if (!visible && result_popup_) {
        result_popup_->close();
    }
    update();
}

QRectF ImageResultCanvas::image_rect() const {
    const QRect viewport = ImageViewportTransform::fit_viewport(size());
    return viewport_.target_rect(image_.size(), viewport.size()).translated(viewport.topLeft());
}

void ImageResultCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#18292D"));
    if (image_.isNull()) {
        painter.setPen(QColor("#B8C8C6"));
        painter.drawText(rect(), Qt::AlignCenter, tr("Select an image"));
        return;
    }
    const QRectF target = image_rect();
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, image_);
    if (prediction_visible_ && !prediction_overlay_.isNull()) {
        painter.setOpacity(overlay_opacity_);
        painter.drawImage(target, prediction_overlay_);
    }
    if (prediction_visible_ && !result_items_.isEmpty()) {
        painter.setOpacity(1.0);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF clip = target.intersected(rect());
        painter.setClipRect(clip);
        QTransform to_viewport;
        to_viewport.translate(target.left(), target.top());
        to_viewport.scale(target.width() / image_.width(), target.height() / image_.height());
        for (const ResultPresentationItem& item : result_items_) {
            paint_labeled_box(painter, to_viewport.mapRect(item.outline), clip, item.text, {}, item.color, true);
        }
    }
}

void ImageResultCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    const QRectF target = image_rect();
    if (event->button() == Qt::LeftButton && prediction_visible_ && !result_items_.isEmpty() &&
        !target.isEmpty() && target.contains(event->localPos())) {
        const QPointF point = event->localPos() - target.topLeft();
        const QPointF source(point.x() * image_.width() / target.width(), point.y() * image_.height() / target.height());
        const int index = result_item_at(result_items_, source);
        if (index >= 0) {
            if (result_popup_) {
                result_popup_->close();
            }
            result_popup_ = show_result_properties(this, event->globalPos(), result_items_[index]);
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ImageResultCanvas::mousePressEvent(QMouseEvent* event) {
    if (!image_.isNull() && event->button() == Qt::MiddleButton) {
        pan_.begin(event->pos());
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ImageResultCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (pan_.active()) {
        viewport_.pan_by(pan_.move(event->pos()));
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ImageResultCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton && pan_.active()) {
        pan_.end();
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ImageResultCanvas::wheelEvent(QWheelEvent* event) {
    if (image_.isNull()) {
        return;
    }
    const QRect viewport = ImageViewportTransform::fit_viewport(size());
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    if (viewport_.zoom_at(event->position() - viewport.topLeft(), factor,
                          image_.size(), viewport.size())) {
        update();
    }
    event->accept();
}

} // namespace studio
