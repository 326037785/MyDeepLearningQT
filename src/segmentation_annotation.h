#pragma once

#include "image_viewport_transform.h"
#include "annotation_tool_interaction.h"
#include "annotation_region_edit_service.h"
#include "workflow.h"
#include <common/geometry/interval.h>

#include <QColor>
#include <QImage>
#include <QHash>
#include <QPoint>
#include <QPolygonF>
#include <QSet>
#include <QStringList>
#include <QTransform>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QComboBox;
class QCheckBox;
class QKeyEvent;
class QLabel;
class QListWidget;
class QPushButton;
class QResizeEvent;
class QSlider;
class QSplitter;
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
    void set_class_colors(const QHash<int, QColor>& colors);
    void set_hidden_class_values(const QSet<int>& values);
    void set_editing_enabled(bool enabled) { editing_enabled_ = enabled; }
    void set_eraser(bool eraser) { set_tool(eraser ? AnnotationTool::Eraser : AnnotationTool::Brush); }
    void set_tool(AnnotationTool tool);
    void set_overlay_visible(bool visible) { overlay_visible_ = visible; update(); }
    void set_mask_visible(bool visible) { mask_visible_ = visible; update(); }
    void set_prediction_visible(bool visible) { prediction_visible_ = visible; update(); }
    void set_heatmap_visible(bool visible) { heatmap_visible_ = visible; update(); }
    void set_overlay_opacity(double opacity) { overlay_opacity_ = opacity; update(); }
    void set_prediction_overlay(const QImage& image);
    void set_heatmap_overlay(const QImage& image);
    void set_mask_overlay(const QImage& image);
    bool has_prediction_overlay() const { return !prediction_overlay_.isNull(); }
    bool has_heatmap_overlay() const { return !heatmap_overlay_.isNull(); }
    bool has_mask_overlay() const { return !mask_overlay_.isNull(); }
    void undo();
    void redo();
    bool can_undo() const;
    bool can_redo() const;
    bool is_dirty() const { return dirty_; }
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
    std::function<int()> class_required;
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
    QImage overlay_;
    QImage prediction_overlay_;
    QImage heatmap_overlay_;
    QImage mask_overlay_;
    QImage stroke_before_;

    ImageViewportTransform viewport_;
    bool editing_enabled_ = true;
    ImagePanInteraction pan_;
    int class_value_ = 0;
    AnnotationToolInteraction interaction_;
    SelectionTransformInteraction transform_;
    std::optional<LabelRegionSelection> transform_region_;
    QImage transform_base_mask_;
    QImage transform_drag_before_;
    bool dirty_ = false;
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
    explicit SegmentationAnnotationWidget(QWidget* parent = nullptr);

    void set_task_dataset(const QString& directory,
                          const std::vector<TaskSampleBinding>& samples,
                          const std::vector<AnnotationClass>& classes);
    void refresh_samples(const std::vector<TaskSampleBinding>& samples,
                         const std::vector<std::string>& reload_sample_ids);
    void set_class_schema(const std::vector<AnnotationClass>& classes);
    void set_inference_result(const InferenceResultSet* result);
    void refresh_inference_layers();
    std::function<void(const QString&)> status;
    std::function<void(const QString&, const QImage&)> mask_saved;
    std::function<void(const QString&)> class_add_requested;
    std::function<void(const QString&, const QString&)> class_rename_requested;
    std::function<void(const QString&)> class_retire_requested;
    std::function<void(const QString&, int)> class_move_requested;
    std::function<void(const QString&, bool)> class_visibility_changed;
    std::function<void(const QString&, bool)> class_lock_changed;
    std::function<void(const std::vector<std::string>&, const std::string&)>
        sample_split_requested;
    std::function<void(const std::vector<std::string>&)> samples_empty_requested;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    bool confirm_transition();
    void reload_dataset();
    void load_index(int index);
    bool save_current();
    void set_selected_empty(const std::vector<std::string>& ids);
    void update_layer_controls();
    void update_class_state();
    void rebuild_class_list();
    void apply_class_schema();
    void update_context_label();
    int class_index_for_mask_value(int mask_value) const;
    int current_mask_value() const;
    QString current_class_id() const;
    void refresh_sample_status();
    int prompt_for_class();
    int add_or_select_class(const QString& name);
    void apply_responsive_layout(bool force = false);
    void set_side_panel_width(int index, bool shown, int preferred_width);

    QString dataset_directory_;
    QStringList imported_image_paths_;
    std::vector<AnnotationClass> configured_classes_;
    std::vector<TaskSampleBinding> sample_bindings_;
    QStringList sample_ids_;
    QList<QStringList> sample_image_sets_;
    QStringList display_names_;
    std::vector<ResultReference> inference_results_;
    QStringList image_files_;
    int current_index_ = -1;
    int threshold_low_ = 64;
    int threshold_high_ = 192;
    bool threshold_outside_ = false;
    bool remember_thresholds_ = false;
    SampleBrowser* images_ = nullptr;
    QListWidget* class_list_ = nullptr;
    QSplitter* content_splitter_ = nullptr;
    QWidget* samples_panel_ = nullptr;
    QWidget* classes_panel_ = nullptr;
    QToolButton* toggle_samples_ = nullptr;
    QToolButton* toggle_classes_ = nullptr;
    MaskCanvas* canvas_ = nullptr;
    QComboBox* classes_ = nullptr;
    QToolButton* brush_ = nullptr;
    QPushButton* add_class_ = nullptr;
    QLabel* context_ = nullptr;
    QLabel* state_ = nullptr;
    QLabel* samples_title_ = nullptr;
    QLabel* quality_ = nullptr;
    QLabel* class_identity_ = nullptr;
    QLabel* class_empty_hint_ = nullptr;
    QLabel* brush_size_caption_ = nullptr;
    QLabel* layer_caption_ = nullptr;
    QLabel* opacity_caption_ = nullptr;
    QCheckBox* annotation_layer_ = nullptr;
    QCheckBox* prediction_layer_ = nullptr;
    QCheckBox* heatmap_layer_ = nullptr;
    QCheckBox* mask_layer_ = nullptr;
    QSlider* brush_size_slider_ = nullptr;
    QSlider* opacity_slider_ = nullptr;
    bool rebuilding_classes_ = false;
    bool current_image_portrait_ = false;
    int responsive_layout_mode_ = -1;
};

} // namespace studio
