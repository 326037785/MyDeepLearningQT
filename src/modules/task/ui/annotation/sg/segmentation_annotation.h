#pragma once

#include "modules/task/ui/image/image_viewport_transform.h"
#include "modules/task/ui/annotation/tools/annotation_tool_interaction.h"
#include "modules/task/ui/annotation/sg/annotation_region_edit_service.h"
#include "modules/task/ui/results/result_inspection.h"
#include "modules/workflow/model/workflow.h"
#include <common/geometry/interval.h>

#include <QColor>
#include <QImage>
#include <QHash>
#include <QPoint>
#include <QPointer>
#include <QPolygonF>
#include <QSet>
#include <QStringList>
#include <QTransform>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QCheckBox;
class QKeyEvent;
class QLabel;
class QResizeEvent;
class QSlider;
class QSplitter;
class QTabWidget;
class QToolButton;
class QUndoStack;
class QPainter;

namespace studio {

class SampleBrowser;

class MaskCanvas final : public QWidget {
public:
    explicit MaskCanvas(QWidget* parent = nullptr);

    bool load(const QString& image_path, const QString& mask_path, QString* error);
    const QImage& mask_image() const { return mask_; }
    void mark_saved();
    void clear();
    bool save(const QString& mask_path, QString* error);
    void set_brush_size(int size) { interaction_.set_brush_size(size); }
    void set_class_value(int value) { class_value_ = value; }
    void set_class_display(const QHash<int, QColor>& colors, const QSet<int>& hidden);
    void set_editing_enabled(bool enabled) { editing_enabled_ = enabled; }
    void remove_class_pixels(int mask_value);
    void set_eraser(bool eraser) { set_tool(eraser ? AnnotationTool::Eraser : AnnotationTool::Brush); }
    void set_tool(AnnotationTool tool);
    void set_overlay_visible(bool visible) {
        overlay_visible_ = visible;
        if (!visible) {
            clear_transform_edit();
        } else if (result_popup_) {
            result_popup_->close();
        }
        update();
    }
    void set_mask_visible(bool visible) { mask_visible_ = visible; update(); }
    void set_prediction_visible(bool visible) {
        prediction_visible_ = visible;
        if (!visible && result_popup_) {
            result_popup_->close();
        }
        update();
    }
    void set_heatmap_visible(bool visible) { heatmap_visible_ = visible; update(); }
    void set_overlay_opacity(double opacity) { overlay_opacity_ = opacity; update(); }
    void set_prediction_overlay(const QImage& image,
                                const QVector<ResultPresentationItem>& items = {});
    void set_heatmap_overlay(const QImage& image);
    void set_mask_overlay(const QImage& image);
    bool has_prediction_overlay() const { return !prediction_overlay_.isNull(); }
    bool has_heatmap_overlay() const { return !heatmap_overlay_.isNull(); }
    bool has_mask_overlay() const { return !mask_overlay_.isNull(); }
    void undo();
    void redo();
    bool can_undo() const;
    bool can_redo() const;
    bool is_dirty() const { return mask_ != saved_mask_; }
    void apply_mask(const QImage& mask, bool dirty);
    void apply_ellipse(const QRect& roi);
    void apply_threshold_roi(const QRect& roi,
                             const std::vector<jocelin::geo::Interval>& intervals,
                             bool keep_outside_range);
    int zoom_percent() const;
    void zoom_by(double factor);
    void reset_view();

    std::function<void(bool)> dirty_changed;
    std::function<void(int)> class_picked;
    std::function<int()> choose_class;
    std::function<void(int)> zoom_changed;
    std::function<void(bool, bool)> undo_state_changed;
    std::function<void(bool)> polygon_active_changed;
    std::function<void(const QRect&, const QImage&)> threshold_roi_selected;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    using TransformHandle = SelectionTransformInteraction::Handle;

    QPoint image_point(const QPoint& widget_point) const;
    QPoint annotation_point(const QPoint& widget_point) const;
    QPointF image_position(const QPointF& widget_point) const;
    QPointF widget_position(const QPointF& image_point) const;
    QRectF image_target() const;
    int resolve_class() const;
    void paint_at(const QPoint& point);
    void paint_segment(const QPoint& from, const QPoint& to);
    void fill_at(const QPoint& point);
    bool begin_transform_edit(const QPoint& point);
    void clear_transform_edit();
    void begin_transform_drag(TransformHandle handle, const QPointF& point);
    void update_transform_drag(const QPointF& point);
    void finish_transform_drag();
    TransformHandle transform_handle_at(const QPointF& widget_point) const;
    bool transform_region_contains(const QPointF& image_point) const;
    QPolygonF transform_frame() const;
    void paint_transform_controls(QPainter& painter) const;
    void finish_edit(const QImage& before);
    void finish_polygon();
    void cancel_polygon();
    void rebuild_overlay();

    QImage image_;
    QImage mask_;
    QImage saved_mask_;
    QImage overlay_;
    QImage prediction_overlay_;
    QVector<ResultPresentationItem> result_items_;
    QPointer<QMenu> result_popup_;
    QImage heatmap_overlay_;
    QImage mask_overlay_;
    QImage stroke_before_;
    QImage stroke_preview_;

    ImageViewportTransform viewport_;
    bool editing_enabled_ = true;
    ImagePanInteraction pan_;
    int class_value_ = 0;
    AnnotationToolInteraction interaction_;
    SelectionTransformInteraction transform_;
    std::optional<LabelRegionSelection> transform_region_;
    QImage transform_base_mask_;
    QImage transform_drag_before_;
    bool overlay_visible_ = true;
    bool mask_visible_ = true;
    bool prediction_visible_ = true;
    bool heatmap_visible_ = false;
    double overlay_opacity_;
    QUndoStack* undo_stack_ = nullptr;
    QHash<int, QColor> class_colors_;
    QSet<int> hidden_class_values_;
};

class SegmentationAnnotationWidget final : public QWidget {
public:
    explicit SegmentationAnnotationWidget(QWidget* parent = nullptr, QTabWidget* inspector = nullptr);

    void set_task_dataset(const std::vector<TaskSampleBinding>& samples);
    void refresh_samples(const std::vector<TaskSampleBinding>& samples,
                         const std::vector<std::string>& reload_sample_ids);
    MaskCanvas* canvas() const { return canvas_; }
    void set_inference_result(const InferenceResultSet* result,
                              const QHash<int, QString>& model_classes = {},
                              const std::vector<jocelin::model::ModelClass>& model_schema = {},
                              const std::vector<AnnotationClass>& task_classes = {});
    void refresh_inference_layers();
    bool confirm_transition();
    std::function<void(const QString&)> status;
    std::function<bool(const std::string&, const QImage&)> save_mask;
    std::function<void(const std::vector<std::string>&, const std::string&)>
        sample_split_requested;
    std::function<void(const std::vector<std::string>&)> samples_empty_requested;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void load_index(int index);
    bool save_current();
    void set_selected_empty(const std::vector<std::string>& ids);
    void update_layer_controls();
    void update_context_label();
    void refresh_sample_status();
    void apply_responsive_layout(bool force = false);
    void set_side_panel_width(int index, bool shown, int preferred_width);

    std::vector<TaskSampleBinding> sample_bindings_;
    QStringList display_names_;
    std::vector<ResultReference> inference_results_;
    QHash<int, QString> prediction_class_names_;
    std::vector<jocelin::model::ModelClass> prediction_model_schema_;
    std::vector<AnnotationClass> prediction_task_classes_;
    int current_index_ = -1;
    int threshold_low_ = 64;
    int threshold_high_ = 192;
    bool threshold_outside_ = false;
    bool remember_thresholds_ = false;
    SampleBrowser* images_ = nullptr;
    QSplitter* content_splitter_ = nullptr;
    QToolButton* toggle_samples_ = nullptr;
    QToolButton* toggle_classes_ = nullptr;
    MaskCanvas* canvas_ = nullptr;
    QLabel* context_ = nullptr;
    QLabel* state_ = nullptr;
    QLabel* samples_title_ = nullptr;
    QLabel* quality_ = nullptr;
    QLabel* result_details_ = nullptr;
    QLabel* brush_size_caption_ = nullptr;
    QLabel* layer_caption_ = nullptr;
    QLabel* opacity_caption_ = nullptr;
    QCheckBox* annotation_layer_ = nullptr;
    QCheckBox* prediction_layer_ = nullptr;
    QCheckBox* heatmap_layer_ = nullptr;
    QCheckBox* mask_layer_ = nullptr;
    QSlider* brush_size_slider_ = nullptr;
    QSlider* opacity_slider_ = nullptr;
    bool current_image_portrait_ = false;
    int responsive_layout_mode_ = -1;
};

} // namespace studio
