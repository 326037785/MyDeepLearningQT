#pragma once

#include "modules/task/ui/annotation/od/editing/box_edit_command.h"
#include "modules/task/presentation/results/detection_result_presentation.h"
#include "modules/task/ui/annotation/tools/annotation_tool_interaction.h"
#include "modules/task/ui/image/image_viewport_transform.h"

#include <QApplication>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QUndoStack>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace studio {

using Box = jocelin::meta::BboxComp;

class DetectionCanvas final : public QWidget {
public:
    explicit DetectionCanvas(QWidget* parent);

    void load(QImage image, const std::vector<Box>& boxes);

    void cancel_edit();

    void set_tool(AnnotationTool tool);

    void set_layers(bool annotations, bool predictions);

    void set_classes(const std::vector<AnnotationClass>& classes);

    void set_prediction(const jocelin::result::od::OdResult* result,
                        const std::vector<jocelin::model::ModelClass>& classes);

    void fit();

    void zoom_at(const QPointF& anchor, double factor);

    const std::vector<Box>& boxes() const;
    bool has_image() const;
    int zoom_percent() const;

    bool selected_box() const;

    void remove_selected();

    std::function<void()> selection_changed = [] {};
    std::function<void()> zoom_changed = [] {};
    std::function<std::string()> request_class;

protected:
    void paintEvent(QPaintEvent*) override;

    void mousePressEvent(QMouseEvent* event) override;

    void mouseDoubleClickEvent(QMouseEvent* event) override;

    void mouseMoveEvent(QMouseEvent* event) override;

    void mouseReleaseEvent(QMouseEvent* event) override;

    void wheelEvent(QWheelEvent* event) override;

public:
    QString empty_text;

private:
    QRectF image_rect() const;

    QPointF to_source(const QPointF& point) const;

    QRectF to_widget(const QRectF& box) const;

    QPoint clamped_pixel(const QPointF& point) const;

    QRectF moved_rect() const;

    bool can_draw() const;

    void update_cursor();

    const AnnotationClass* find_class(const std::string& id) const;

    bool visible(const std::string& id) const;

    int box_at(const QPointF& point) const;

    void paint_box(QPainter& painter, const Box& box, const QRectF& outline,
                   bool prediction, bool selected) const;

    void finish_box(const QRect& rect);

    QImage image_;
    ImageViewportTransform viewport_;
    ImagePanInteraction pan_;
    AnnotationToolInteraction gesture_;
    SelectionTransformInteraction transform_;
    QRectF draft_;
    std::vector<Box> boxes_;
    std::vector<Box> predictions_;
    std::vector<AnnotationClass> classes_;
    std::vector<jocelin::model::ModelClass> prediction_classes_;
    QPointer<QMenu> result_popup_;
    int selected_ = -1;
    bool move_mode_ = false;
    bool show_annotations_ = true;
    bool show_predictions_ = true;

public:
    QUndoStack history;
};


} // namespace studio
