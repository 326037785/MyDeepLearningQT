#include "image_result_canvas.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>

namespace studio {
namespace {

constexpr int kViewportMargin = 24;

QSize viewport_size(const QSize& size) {
    return {std::max(1, size.width() - 2 * kViewportMargin),
            std::max(1, size.height() - 2 * kViewportMargin)};
}

} // namespace

ImageResultCanvas::ImageResultCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(280, 160);
    setCursor(Qt::CrossCursor);
}

bool ImageResultCanvas::load(const QString& image_path, QString* error) {
    QImage image(image_path);
    if (image.isNull()) {
        if (error) {
            *error = tr("Cannot read image: %1").arg(image_path);
        }
        return false;
    }
    image_ = image.convertToFormat(QImage::Format_RGB888);
    prediction_overlay_ = {};
    viewport_.reset();
    pan_.end();
    update();
    return true;
}

void ImageResultCanvas::clear() {
    image_ = {};
    prediction_overlay_ = {};
    viewport_.reset();
    pan_.end();
    update();
}

void ImageResultCanvas::set_prediction_overlay(const QImage& image) {
    prediction_overlay_ = image;
    update();
}

void ImageResultCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#18292D"));
    if (image_.isNull()) {
        painter.setPen(QColor("#B8C8C6"));
        painter.drawText(rect(), Qt::AlignCenter, tr("Select an image"));
        return;
    }
    const QRectF target = viewport_.target_rect(image_.size(), viewport_size(size()))
        .translated(kViewportMargin, kViewportMargin);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, image_);
    if (prediction_visible_ && !prediction_overlay_.isNull()) {
        painter.setOpacity(overlay_opacity_);
        painter.drawImage(target, prediction_overlay_);
    }
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
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    if (viewport_.zoom_at(event->position() - QPointF(kViewportMargin, kViewportMargin),
                          factor, image_.size(), viewport_size(size()))) {
        update();
    }
    event->accept();
}

} // namespace studio
