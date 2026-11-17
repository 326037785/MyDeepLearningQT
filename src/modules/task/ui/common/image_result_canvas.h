#pragma once

#include "modules/task/ui/common/image_viewport_transform.h"
#include "modules/task/ui/common/annotation_tool_interaction.h"

#include <QImage>
#include <QWidget>

namespace studio {

// Read-only image and prediction display. It owns no annotation target or edit history.
class ImageResultCanvas final : public QWidget {
public:
    explicit ImageResultCanvas(QWidget* parent = nullptr);
    bool load(const QString& image_path, QString* error);
    void clear();
    void set_prediction_overlay(const QImage& image);
    bool has_prediction_overlay() const { return !prediction_overlay_.isNull(); }
    void set_prediction_visible(bool visible) { prediction_visible_ = visible; update(); }
    void set_overlay_opacity(double opacity) { overlay_opacity_ = opacity; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QImage image_;
    QImage prediction_overlay_;
    ImageViewportTransform viewport_;
    bool prediction_visible_ = true;
    double overlay_opacity_ = 0.42;
    ImagePanInteraction pan_;
};

} // namespace studio
