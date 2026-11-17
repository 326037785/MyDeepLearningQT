#include "modules/task/ui/annotation/cls/classification_annotation.h"
#include "modules/task/ui/annotation/sg/annotation_region_edit_service.h"
#include "modules/workflow/ui/dataset/dataset_import_dialog.h"
#include "modules/workflow/application/dataset_import_service.h"
#include "modules/workflow/ui/dataset/dataset_item_model.h"
#include "modules/task/ui/common/image_viewport_transform.h"
#include "modules/task/ui/common/model_presentation.h"
#include "modules/task/presentation/result_presentation.h"
#include "modules/task/presentation/sample_presentation.h"
#include "modules/task/ui/annotation/sg/segmentation_annotation.h"
#include "modules/task/application/annotation/segmentation_annotation_state.h"
#include "modules/task/application/task_dataset_binding.h"
#include "modules/task/application/task_workspace_service.h"

#include <QApplication>
#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QStyle>
#include <QListView>
#include <QListWidget>
#include <QMouseEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <fstream>
#include <cmath>

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;

    QImage first(80, 60, QImage::Format_RGB888);
    first.fill(QColor("#C65B32"));
    QImage second(80, 60, QImage::Format_RGB888);
    second.fill(QColor("#2C7A78"));
    QImage third(80, 60, QImage::Format_RGB888);
    third.fill(QColor("#D5A638"));
    if (!first.save(directory.filePath("train.png")) ||
        !second.save(directory.filePath("val.png")) ||
        !third.save(directory.filePath("unlabeled.png"))) return 2;
    QDir().mkpath(directory.filePath("late-import"));
    if (!first.save(directory.filePath("late-import/connected-after-import.jpg"))) return 2;
    {
        std::ofstream manifest(directory.filePath("samples.csv").toStdString());
        manifest << "train.png,1,train\nval.png,2,val\nunlabeled.png,,unlabeled\n";
    }

    QFile damaged(directory.filePath("damaged.png"));
    if (!damaged.open(QIODevice::WriteOnly)) return 3;
    damaged.write("not an image");
    damaged.close();

    studio::DatasetConfig config;
    const auto imported = studio::DatasetImportService::import_paths(
        config, {directory.filePath("train.png"), directory.filePath("val.png"),
                 directory.filePath("unlabeled.png"), damaged.fileName()}, true);
    if (!imported.error.isEmpty() || imported.added != 4 || imported.config.items.size() != 4) return 3;
    if (studio::DatasetImportService::valid_paths(imported.config).size() != 3) return 4;
    const auto folder_import = studio::DatasetImportService::import_paths(config, {directory.path()}, false);
    if (QDir::cleanPath(QString::fromStdString(folder_import.config.directory)) != QDir::cleanPath(directory.path())) return 14;
    const auto duplicate = studio::DatasetImportService::import_paths(
        imported.config, {directory.filePath("train.png")}, true);
    if (duplicate.duplicate != 1 || duplicate.config.items.size() != 4) return 5;

    studio::DatasetImportDialog dialog(imported.config);
    dialog.show();
    QApplication::processEvents();
    auto* list = dialog.findChild<QListView*>("datasetImportImageList");
    auto* preview = dialog.findChild<QLabel*>("datasetImportPreview");
    if (!list || list->model()->rowCount() != 4 || !preview) return 6;
    int model_valid = 0;
    for (int row = 0; row < list->model()->rowCount(); ++row)
        model_valid += list->model()->index(row, 0).data(studio::DatasetItemModel::ValidRole).toBool() ? 1 : 0;
    if (model_valid != 3) return 7;
    if (studio::model_scale_label("n") != "Nano (n)" ||
        studio::model_scale_label("x") != "Extra Large (x)") return 8;

    QDir().mkpath(directory.filePath("images"));
    QDir().mkpath(directory.filePath("masks"));
    if (!first.save(directory.filePath("images/segment.png"))) return 9;
    const auto standard_root_import = studio::DatasetImportService::import_paths(
        config, {directory.path()}, false);
    if (!standard_root_import.error.isEmpty() || standard_root_import.config.items.size() != 1 ||
        QDir::cleanPath(QString::fromStdString(standard_root_import.config.directory)) != QDir::cleanPath(directory.path()) ||
        !QDir::fromNativeSeparators(QString::fromStdString(standard_root_import.config.items.front().path))
            .contains("/images/", Qt::CaseInsensitive)) return 18;
    const auto images_folder_import = studio::DatasetImportService::import_paths(
        config, {directory.filePath("images")}, false);
    if (QDir::cleanPath(QString::fromStdString(images_folder_import.config.directory)) !=
        QDir::cleanPath(directory.path())) return 19;
    QImage mask(first.size(), QImage::Format_Grayscale8);
    mask.fill(1);
    if (!mask.save(directory.filePath("masks/segment.png"))) return 10;
    const auto paired_import = studio::DatasetImportService::import_paths(config, {directory.path()}, false);
    if (paired_import.config.items.size() != 1 ||
        paired_import.config.items.front().sample_id.empty()) return 26;
    QDir().mkpath(directory.filePath("results"));
    QImage prediction_mask(first.size(), QImage::Format_Grayscale8);
    prediction_mask.fill(0);
    for (int y = 4; y < 20; ++y) {
        uchar* row = prediction_mask.scanLine(y);
        for (int x = 5; x < 30; ++x) row[x] = 1;
    }
    QImage prediction_color(first.size(), QImage::Format_RGB888);
    prediction_color.fill(Qt::black);
    QPainter prediction_painter(&prediction_color);
    prediction_painter.fillRect(QRect(5, 4, 25, 16), QColor("#1F8A9E"));
    prediction_painter.end();
    if (!prediction_mask.save(directory.filePath("results/segment_mask.png")) ||
        !prediction_color.save(directory.filePath("results/segment_color_mask.png")) ||
        !second.save(directory.filePath("results/segment_heatmap.png"))) return 10;
    const studio::TaskSampleBinding presentation_sample{
        "presentation-sample",
        {directory.filePath("images/segment.png").toStdString(),
         directory.filePath("results/segment_heatmap.png").toStdString()},
        true, {}, {}, {}, "unassigned", "unannotated", {}};
    const QString presentation_tooltip = studio::sample_image_tooltip(
        studio::image_display_name(0, presentation_sample),
        presentation_sample,
        "Not annotated");
    if (studio::image_display_name(0, presentation_sample) != "img_00001.png" ||
        !presentation_tooltip.contains("Images in sample: 2") ||
        !presentation_tooltip.contains("segment.png") ||
        !presentation_tooltip.contains("segment_heatmap.png")) return 38;
    {
        std::ofstream classes(directory.filePath("classes.txt").toStdString());
        classes << "foreground\n";
    }
    studio::TaskSampleBinding annotation_sample;
    annotation_sample.sample_id = "segment-sample";
    annotation_sample.image_paths = {
        directory.filePath("images/segment.png").toStdString()};
    annotation_sample.selected = true;
    studio::import_segmentation_annotation({{"foreground", 1, "foreground", "#1F8A9E", true, false}},
        annotation_sample, directory.filePath("masks/segment.png"));
    annotation_sample.split = "train";
    annotation_sample.annotation_state = "annotated";
    studio::SegmentationAnnotationWidget annotation;
    annotation.resize(1000, 600);
    annotation.set_task_dataset({annotation_sample});
    studio::InferenceResultSet segmentation_results;
    jocelin::result::sg::SgResult prediction;
    prediction.image_path = annotation_sample.image_paths.front();
    prediction.result_name = "segment";
    prediction.save_dir = directory.filePath("results").toStdString();
    prediction.image_width = first.width();
    prediction.image_height = first.height();
    jocelin::result::sg::SgResult::Component predicted_region;
    predicted_region.label = 1;
    predicted_region.area = 370;
    predicted_region.confidence = 0.83;
    predicted_region.interest = true;
    for (int row = 4; row < 20; ++row) {
        if (row >= 10 && row < 13) {
            predicted_region.region.vRun.emplace_back(row, 5, 10, 1);
            predicted_region.region.vRun.emplace_back(row, 20, 30, 1);
        } else {
            predicted_region.region.vRun.emplace_back(row, 5, 30, 1);
        }
    }
    prediction.components.push_back(predicted_region);
    jocelin::result::sg::SgResult::Component rejected_region;
    rejected_region.label = 2;
    rejected_region.area = 25;
    rejected_region.confidence = 0.41;
    rejected_region.interest = false;
    for (int row = 20; row < 25; ++row) {
        rejected_region.region.vRun.emplace_back(row, 50, 55, 2);
    }
    prediction.components.push_back(rejected_region);
    jocelin::result::sg::SgResult::Component other_region;
    other_region.label = 2;
    other_region.area = 25;
    other_region.confidence = 0.41;
    other_region.interest = true;
    for (int row = 35; row < 40; ++row) {
        other_region.region.vRun.emplace_back(row, 45, 50, 2);
    }
    prediction.components.push_back(other_region);
    const std::vector<jocelin::model::ModelClass> color_model_classes{{"road-id", "road", 1}};
    std::vector<studio::AnnotationClass> color_task_classes{{"road-id", 1, "renamed", "#123456", true, false}};
    const QImage custom_prediction = studio::prediction_overlay_from_result(
        prediction, color_model_classes, color_task_classes);
    if (custom_prediction.pixelColor(6, 5) != QColor("#123456") ||
        custom_prediction.pixelColor(15, 11).alpha() != 0 ||
        custom_prediction.pixelColor(51, 21).alpha() != 0) {
        return 113;
    }
    color_task_classes.front().color = "#000000";
    const QImage black_prediction = studio::prediction_overlay_from_result(
        prediction, color_model_classes, color_task_classes);
    if (black_prediction.pixelColor(6, 5) != QColor(Qt::black)) {
        return 114;
    }
    color_task_classes.front().id = "different-class";
    if (studio::prediction_overlay_from_result(prediction, color_model_classes, color_task_classes) !=
        studio::prediction_overlay_from_result(prediction)) {
        return 115;
    }
    segmentation_results.samples.push_back({annotation_sample.sample_id, prediction});
    annotation.set_inference_result(&segmentation_results, {{1, "road"}, {2, "tree"}});
    annotation.show();
    QApplication::processEvents();
    auto* canvas = annotation.findChild<studio::MaskCanvas*>("segmentationMaskCanvas");
    auto* annotation_list = annotation.findChild<QListWidget*>("annotationImageList");
    auto* annotation_quality = annotation.findChild<QLabel*>("annotationQualitySummary");
    if (!canvas || !annotation_list || annotation_list->count() != 1 ||
        annotation_list->item(0)->icon().isNull())
        return 11;
    if (!annotation_quality || annotation_quality->isVisible()) return 86;
    const auto* annotation_item = annotation_list->item(0);
    if (!annotation_item->text().startsWith("img_00001.png") ||
        annotation_item->data(Qt::UserRole).toString() != "segment-sample") return 36;
    auto* prediction_layer = annotation.findChild<QCheckBox*>("predictionLayerCheck");
    auto* heatmap_layer = annotation.findChild<QCheckBox*>("heatmapLayerCheck");
    auto* mask_layer = annotation.findChild<QCheckBox*>("maskLayerCheck");
    if (!prediction_layer || !prediction_layer->isEnabled() || !heatmap_layer ||
        !heatmap_layer->isEnabled() || !mask_layer || mask_layer->isEnabled())
        return 87;
    prediction_layer->setChecked(false);
    QApplication::processEvents();
    QImage without_prediction(canvas->size(), QImage::Format_ARGB32_Premultiplied);
    without_prediction.fill(Qt::transparent);
    canvas->render(&without_prediction);
    const QColor background_without_prediction = without_prediction.pixelColor(
        without_prediction.width() / 2, without_prediction.height() / 2);
    prediction_layer->setChecked(true);
    QApplication::processEvents();
    QImage rendered(canvas->size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    canvas->render(&rendered);
    if (rendered.isNull() || rendered.pixelColor(rendered.width() / 2, rendered.height() / 2) == QColor("#18292D")) return 12;
    if (rendered.pixelColor(rendered.width() / 2, rendered.height() / 2) !=
        background_without_prediction) return 69;

    // A future task can provide arbitrary properties and its own hit rule.
    studio::ResultPresentationItem arbitrary_item;
    arbitrary_item.text = "instance";
    arbitrary_item.attributes = {{"Instance ID", "42"}, {"Width", "18"}, {"Custom", "value"}};
    arbitrary_item.hit_test = [](const QPointF& point) { return QRectF(2, 3, 18, 9).contains(point); };
    QVector<studio::ResultPresentationItem> arbitrary_items{arbitrary_item};
    if (studio::result_item_at(arbitrary_items, QPointF(3, 4)) != 0 ||
        studio::result_item_at(arbitrary_items, QPointF(30, 4)) != -1) {
        return 104;
    }
    QMenu* arbitrary_popup = studio::show_result_properties(canvas, canvas->mapToGlobal(QPoint(10, 10)), arbitrary_item);
    if (!arbitrary_popup || arbitrary_popup->actions().size() != 3 ||
        arbitrary_popup->actions()[0]->text() != "Instance ID: 42" ||
        arbitrary_popup->actions()[2]->text() != "Custom: value") {
        return 105;
    }
    arbitrary_popup->close();

    QCheckBox* annotation_layer = annotation.findChild<QCheckBox*>("annotationLayerCheck");
    QToolButton* select_tool = annotation.findChild<QToolButton*>("segmentationTransformRegionButton");
    if (!annotation_layer || !select_tool) {
        return 106;
    }
    annotation_layer->setChecked(false);
    select_tool->click();
    const QRectF result_target = studio::ImageViewportTransform{}.target_rect(
        first.size(), QSize(std::max(1, canvas->width() - 48), std::max(1, canvas->height() - 48)))
                                     .translated(24, 24);
    const auto result_point = [&result_target, &first](const QPointF& image_point) {
        return QPointF(result_target.left() + image_point.x() * result_target.width() / first.width(),
                       result_target.top() + image_point.y() * result_target.height() / first.height());
    };
    const QImage annotation_before_result = canvas->mask_image();
    QMouseEvent hit_result(QEvent::MouseButtonDblClick, result_point({6.5, 5.5}),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &hit_result);
    QMenu* result_popup = nullptr;
    for (QMenu* menu : canvas->findChildren<QMenu*>("resultPropertiesPopup", Qt::FindDirectChildrenOnly)) {
        if (menu->isVisible()) {
            result_popup = menu;
        }
    }
    if (!result_popup || result_popup->actions().size() != 3 ||
        result_popup->actions()[0]->text() != "Class: road" ||
        result_popup->actions()[1]->text() != "Area: 370 px" ||
        result_popup->actions()[2]->text() != "Confidence: 83.0%" ||
        canvas->mask_image() != annotation_before_result || canvas->is_dirty()) {
        return 107;
    }
    // The hole is inside the bounding rectangle but outside the component.
    QMouseEvent miss_result(QEvent::MouseButtonDblClick, result_point({15.5, 11.5}),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &miss_result);
    for (QMenu* menu : canvas->findChildren<QMenu*>("resultPropertiesPopup", Qt::FindDirectChildrenOnly)) {
        if (menu->isVisible()) {
            return 108;
        }
    }
    QMouseEvent hit_other(QEvent::MouseButtonDblClick, result_point({46.5, 36.5}),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &hit_other);
    result_popup = nullptr;
    for (QMenu* menu : canvas->findChildren<QMenu*>("resultPropertiesPopup", Qt::FindDirectChildrenOnly)) {
        if (menu->isVisible()) {
            result_popup = menu;
        }
    }
    if (!result_popup || result_popup->actions().size() != 3 ||
        result_popup->actions()[0]->text() != "Class: tree" ||
        result_popup->actions()[1]->text() != "Area: 25 px" ||
        result_popup->actions()[2]->text() != "Confidence: 41.0%") {
        return 116;
    }
    QMouseEvent hit_rejected(QEvent::MouseButtonDblClick, result_point({51.5, 21.5}),
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &hit_rejected);
    for (QMenu* menu : canvas->findChildren<QMenu*>("resultPropertiesPopup", Qt::FindDirectChildrenOnly)) {
        if (menu->isVisible()) {
            return 109;
        }
    }
    QToolButton* pan_tool = annotation.findChild<QToolButton*>("segmentationPanButton");
    if (!pan_tool) {
        return 117;
    }
    pan_tool->click();
    QApplication::sendEvent(canvas, &hit_result);
    bool pan_hit = false;
    for (QMenu* menu : canvas->findChildren<QMenu*>("resultPropertiesPopup", Qt::FindDirectChildrenOnly)) {
        pan_hit = pan_hit || menu->isVisible();
    }
    if (!pan_hit) {
        return 118;
    }
    prediction_layer->setChecked(false);
    for (QMenu* menu : canvas->findChildren<QMenu*>("resultPropertiesPopup", Qt::FindDirectChildrenOnly)) {
        if (menu->isVisible()) {
            return 110;
        }
    }
    prediction_layer->setChecked(true);
    annotation_layer->setChecked(true);

    QImage edge_mask(first.size(), QImage::Format_Grayscale8);
    edge_mask.fill(0);
    const QString edge_mask_path = directory.filePath("masks/edge-test.png");
    if (!edge_mask.save(edge_mask_path)) return 75;
    studio::MaskCanvas edge_canvas;
    edge_canvas.resize(320, 240);
    QString edge_error;
    if (!edge_canvas.load(directory.filePath("images/segment.png"), edge_mask_path, &edge_error))
        return 75;
    edge_canvas.set_class_value(1);
    edge_canvas.set_tool(studio::AnnotationTool::Rectangle);
    edge_canvas.show();
    QApplication::processEvents();
    const QPoint edge_start(160, 120);
    const QPoint edge_outside(310, 230);
    QMouseEvent edge_press(QEvent::MouseButtonPress, edge_start,
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&edge_canvas, &edge_press);
    QMouseEvent edge_move(QEvent::MouseMove, edge_outside,
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&edge_canvas, &edge_move);
    QMouseEvent edge_release(QEvent::MouseButtonRelease, edge_outside,
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&edge_canvas, &edge_release);
    if (!edge_canvas.save(edge_mask_path, &edge_error)) return 75;
    const QImage clipped_edge_mask(edge_mask_path);
    if (clipped_edge_mask.isNull() || clipped_edge_mask.pixelColor(79, 59).red() != 1)
        return 75;

    QImage empty_mask(first.size(), QImage::Format_Grayscale8);
    empty_mask.fill(0);
    const QString empty_mask_path = directory.filePath("masks/empty.png");
    if (!empty_mask.save(empty_mask_path)) return 49;
    studio::SegmentationAnnotationWidget background_annotation;
    background_annotation.resize(900, 600);
    background_annotation.set_task_dataset(
        {studio::TaskSampleBinding{
            "invalid-sample",
            {directory.filePath("images/segment.png").toStdString()},
            true,
            {},
            {},
            {},
            "train",
            "annotated",
            {}}});
    background_annotation.show();
    QApplication::processEvents();
    auto* background_list = background_annotation.findChild<QListWidget*>("annotationImageList");
    if (!background_list || background_list->count() != 1 ||
        !background_list->item(0)->text().contains(QStringLiteral("✓"))) return 88;

    studio::SegmentationAnnotationWidget no_class_annotation;
    no_class_annotation.resize(900, 600);
    no_class_annotation.set_task_dataset(
        {studio::TaskSampleBinding{
            "late-sample",
            {directory.filePath(
                "late-import/connected-after-import.jpg").toStdString()},
            true, {}, {}, {}, "unassigned", "unannotated", {}}});
    no_class_annotation.show();
    QApplication::processEvents();
    auto* no_class_list = no_class_annotation.findChild<QListWidget*>("annotationImageList");
    auto* brush_button = no_class_annotation.findChild<QToolButton*>("segmentationBrushButton");
    auto* no_class_canvas = no_class_annotation.findChild<studio::MaskCanvas*>("segmentationMaskCanvas");
    if (!no_class_list || no_class_list->count() != 1 ||
        !brush_button || !brush_button->isEnabled() || !no_class_canvas) return 20;
    std::string saved_sample_id;
    QImage saved_mask;
    bool saved_empty = false;
    studio::WorkflowGraph empty_graph;
    const int source_node_id = empty_graph.add_node(studio::NodeKind::DatasetInput);
    const int node_id = empty_graph.add_task_node({"sg"});
    auto& empty_dataset = std::get<studio::DatasetConfig>(empty_graph.node(source_node_id).config);
    empty_dataset.items = {{directory.filePath("late-import/connected-after-import.jpg").toStdString(), {}, true, "late-sample"}};
    empty_graph.connect(source_node_id, 0, node_id, 0);
    studio::TaskWorkspaceService empty_service(empty_graph, node_id);
    empty_service.synchronize();
    empty_service.add_class("vehicle");
    no_class_canvas->set_class_value(1);
    no_class_annotation.save_mask = [&](const std::string& sample_id, const QImage& mask) {
        saved_sample_id = sample_id;
        saved_mask = mask.copy();
        empty_service.set_segmentation_mask(sample_id, mask);
        no_class_annotation.refresh_samples(empty_service.binding().samples, {});
        return true;
    };
    brush_button->click();
    QApplication::processEvents();
    const QPoint paint_point = no_class_canvas->rect().center();
    QMouseEvent first_annotation_press(QEvent::MouseButtonPress, paint_point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(no_class_canvas, &first_annotation_press);
    QMouseEvent first_annotation_release(QEvent::MouseButtonRelease, paint_point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(no_class_canvas, &first_annotation_release);
    QApplication::processEvents();
    auto* save_mask = no_class_annotation.findChild<QToolButton*>("segmentationSaveMaskButton");
    if (!no_class_canvas->is_dirty() || !save_mask) return 24;
    save_mask->click();
    QApplication::processEvents();
    if (saved_sample_id != "late-sample" || saved_mask.isNull()) return 25;
    auto* saved_samples =
        no_class_annotation.findChild<QListWidget*>("annotationImageList");
    if (!saved_samples || saved_samples->count() != 1 ||
        !saved_samples->item(0)->text().contains("Annotated"))
        return 85;

    int empty_requests = 0;
    bool view_unchanged_on_request = false;
    no_class_annotation.samples_empty_requested = [&](const std::vector<std::string>& ids) {
        ++empty_requests;
        view_unchanged_on_request = no_class_canvas->is_dirty();
        saved_empty = ids == std::vector<std::string>{"late-sample"};
        for (const auto& id : ids) empty_service.set_segmentation_sample_empty(id);
        no_class_annotation.refresh_samples(empty_service.binding().samples, ids);
    };
    const QImage foreground_before_empty = saved_mask.copy();
    empty_service.add_class("road");
    no_class_canvas->set_class_value(2);
    const QDir mask_directory(directory.filePath("late-import"));
    const auto files_before_empty = mask_directory.entryList(QDir::Files);
    bool empty_action_found = false;
    auto invoke_empty = [&](QMessageBox::StandardButton answer) {
        QTimer::singleShot(0, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            auto* action = menu ? menu->findChild<QAction*>("annotationSetAsEmptyAction") : nullptr;
            if (!action) { if (menu) menu->close(); return; }
            empty_action_found = action->isEnabled();
            QTimer::singleShot(0, [answer] {
                if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    if (auto* button = dialog->button(answer)) button->click();
            });
            action->trigger();
            menu->close();
        });
        QMetaObject::invokeMethod(saved_samples, "customContextMenuRequested", Qt::DirectConnection,
            Q_ARG(QPoint, saved_samples->visualItemRect(saved_samples->item(0)).center()));
    };
    invoke_empty(QMessageBox::No);
    if (!empty_action_found || empty_requests != 0 || saved_empty || saved_mask != foreground_before_empty) return 90;
    QMouseEvent pending_press(QEvent::MouseButtonPress, paint_point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(no_class_canvas, &pending_press);
    QMouseEvent pending_release(QEvent::MouseButtonRelease, paint_point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(no_class_canvas, &pending_release);
    if (!no_class_canvas->is_dirty()) return 97;
    invoke_empty(QMessageBox::Yes);
    // Confirmation clears the domain target and canvas, preserving the former resource on disk.
    if (!saved_empty || empty_requests != 1 || !view_unchanged_on_request ||
        !empty_service.binding().samples.front().regions.empty() ||
        empty_service.binding().samples.front().annotation_state != "annotated" ||
        saved_mask != foreground_before_empty ||
        mask_directory.entryList(QDir::Files) != files_before_empty ||
        !saved_samples->item(0)->text().contains("Annotated") || no_class_canvas->is_dirty()) return 91;
    save_mask->click();
    if (saved_mask != foreground_before_empty ||
        !saved_samples->item(0)->text().contains("Annotated")) return 92;
    // Revisiting the sample must not rediscover its old file as the current target.
    no_class_annotation.set_task_dataset(empty_service.binding().samples);
    // Inspect the canvas through its existing export API, inside the temporary test fixture.
    const QString canvas_capture = directory.filePath("empty-canvas.png");
    QString canvas_error;
    if (no_class_canvas->is_dirty() || !no_class_canvas->save(canvas_capture, &canvas_error)) return 93;
    const QImage empty_canvas = QImage(canvas_capture).convertToFormat(QImage::Format_Grayscale8);
    if (empty_canvas.isNull()) return 95;
    for (int row = 0; row < empty_canvas.height(); ++row)
        if (std::any_of(empty_canvas.constScanLine(row),
            empty_canvas.constScanLine(row) + empty_canvas.width(), [](uchar value) { return value != 0; })) return 96;

    studio::TaskSampleBinding second_empty_sample = empty_service.binding().samples.front();
    second_empty_sample.sample_id = "second-sample";
    no_class_annotation.set_task_dataset({empty_service.binding().samples.front(), second_empty_sample});
    if (saved_samples->count() != 2) {
        return 117;
    }
    saved_samples->setCurrentRow(0);
    saved_samples->item(1)->setSelected(true);
    std::vector<std::string> empty_sample_ids;
    no_class_annotation.samples_empty_requested = [&](const std::vector<std::string>& ids) {
        empty_sample_ids = ids;
        std::sort(empty_sample_ids.begin(), empty_sample_ids.end());
    };
    invoke_empty(QMessageBox::Yes);
    if (empty_sample_ids != std::vector<std::string>{"late-sample", "second-sample"}) {
        return 118;
    }

    studio::ClassificationAnnotationWidget classification;
    classification.resize(1000, 620);
    classification.set_dataset(
        {studio::TaskSampleBinding{
             "classification-sample-1",
             {directory.filePath("images/segment.png").toStdString()},
             true, {}, {}, {}, "unassigned", "unannotated", {}},
         studio::TaskSampleBinding{
             "classification-sample-2",
             {directory.filePath("results/segment_heatmap.png").toStdString()},
             true, {}, {}, "dog", "unassigned", "annotated", {}},
         studio::TaskSampleBinding{
             "classification-sample-3",
             {directory.filePath("images/segment.png").toStdString()},
             true, {}, {}, {}, "unassigned", "unannotated", {}}},
        {"cat", "dog"},
        {-1, 1, -1});
    studio::InferenceResultSet classification_results;
    jocelin::result::cls::ClsResult class_prediction;
    class_prediction.image_path = directory.filePath("images/segment.png").toStdString();
    class_prediction.predicted_class = 2;
    class_prediction.confidence = 0.91;
    class_prediction.heatmap_path = directory.filePath("results/segment_heatmap.png").toStdString();
    classification_results.samples.push_back({"classification-sample-1", class_prediction});
    class_prediction.image_path = class_prediction.heatmap_path;
    class_prediction.predicted_class = -1;
    class_prediction.confidence = 0.4;
    class_prediction.heatmap_path.clear();
    classification_results.samples.push_back({"classification-sample-2", class_prediction});
    classification.set_inference_result(&classification_results, {"dog", "cat"});
    std::vector<std::string> assigned_samples;
    int assigned_class = -1;
    classification.class_assigned = [&](const std::vector<std::string>& sample_ids, int class_index) {
        assigned_samples = sample_ids;
        std::sort(assigned_samples.begin(), assigned_samples.end());
        assigned_class = class_index;
    };
    classification.show();
    QApplication::processEvents();
    auto* classification_images = classification.findChild<QListWidget*>("classificationImageList");
    auto* classification_classes = classification.findChild<QListWidget*>("classificationClassList");
    auto* classification_preview = classification.findChild<QLabel*>("classificationPreview");
    QCheckBox* classification_heatmap = classification.findChild<QCheckBox*>("classificationHeatmapLayerCheck");
    QLabel* review_state = classification.findChild<QLabel*>("classificationReviewState");
    QLabel* image_labels = classification.findChild<QLabel*>("classificationImageLabels");
    if (!classification_images || classification_images->count() != 3 || !classification_classes ||
        classification_classes->count() != 2 || !classification_preview ||
        classification_preview->pixmap(Qt::ReturnByValue).isNull() ||
        !classification_heatmap || !classification_heatmap->isEnabled() ||
        classification_heatmap->isChecked() || !review_state || !image_labels || !image_labels->isVisible()) {
        return 33;
    }
    if (review_state->text() != "Confidence: 91.0%" || !image_labels->text().contains("cat") ||
        !image_labels->text().contains("Ground truth: Unassigned")) {
        return 67;
    }
    const auto inside_image = [classification_preview, image_labels] {
        const QRect image_rect = QStyle::alignedRect(classification_preview->layoutDirection(),
            classification_preview->alignment(), classification_preview->pixmap(Qt::ReturnByValue).size(),
            classification_preview->contentsRect());
        return image_labels->parentWidget() == classification_preview &&
            image_rect.contains(image_labels->geometry()) &&
            image_labels->geometry().topRight() == image_rect.topRight() + QPoint(-8, 8);
    };
    if (!inside_image()) {
        return 123;
    }
    classification.resize(1050, 665);
    QApplication::processEvents();
    if (!inside_image()) {
        return 124;
    }
    if (!image_labels->text().contains("href=\"prediction\"") ||
        !QMetaObject::invokeMethod(image_labels, "linkActivated", Qt::DirectConnection,
                                   Q_ARG(QString, QStringLiteral("prediction")))) {
        return 111;
    }
    QMenu* classification_popup = image_labels->findChild<QMenu*>("resultPropertiesPopup");
    if (!classification_popup || !classification_popup->isVisible() ||
        classification_popup->actions().size() != 1 ||
        classification_popup->actions().front()->text() != "Confidence: 91.0%") {
        return 112;
    }
    classification_popup->close();
    const QImage original_preview = classification_preview->pixmap(Qt::ReturnByValue).toImage();
    classification_heatmap->click();
    QApplication::processEvents();
    const QImage overlay_preview = classification_preview->pixmap(Qt::ReturnByValue).toImage();
    if (overlay_preview.size() != original_preview.size() ||
        !image_labels->text().contains("cat") || review_state->text() != "Confidence: 91.0%") {
        return 68;
    }
    const QPoint center = overlay_preview.rect().center();
    const QColor overlay_pixel = overlay_preview.pixelColor(center);
    if (original_preview.pixelColor(center) != first.pixelColor(0, 0) ||
        std::abs(overlay_pixel.red() - 133) > 1 ||
        std::abs(overlay_pixel.green() - 104) > 1 ||
        std::abs(overlay_pixel.blue() - 79) > 1) {
        return 119;
    }
    classification_heatmap->click();
    if (classification_preview->pixmap(Qt::ReturnByValue).toImage() != original_preview) {
        return 120;
    }
    classification_images->setCurrentRow(1);
    if (!image_labels->text().contains("Unknown") || review_state->text() != "Confidence: 40.0%" ||
        classification_heatmap->isEnabled()) {
        return 121;
    }
    classification_images->setCurrentRow(2);
    if (image_labels->text().contains("href=\"prediction\"") || review_state->text() != "Confidence: —") {
        return 122;
    }
    classification_images->setCurrentRow(0);
    const auto* classification_item = classification_images->item(0);
    if (!classification_item->text().startsWith("img_00001.png") ||
        QFileInfo(classification_item->data(Qt::UserRole).toString()).absoluteFilePath() !=
            QFileInfo(directory.filePath("images/segment.png")).absoluteFilePath()) return 37;
    bool classification_empty_disabled = false;
    QTimer::singleShot(0, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        auto* empty = menu ? menu->findChild<QAction*>("annotationSetAsEmptyAction") : nullptr;
        classification_empty_disabled = empty && !empty->isEnabled();
        if (menu) menu->close();
    });
    QMetaObject::invokeMethod(classification_images, "customContextMenuRequested", Qt::DirectConnection,
        Q_ARG(QPoint, classification_images->visualItemRect(classification_images->item(0)).center()));
    if (!classification_empty_disabled) return 94;
    classification_images->setCurrentRow(0);
    QTimer::singleShot(0, [] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu || menu->actions().isEmpty() || !menu->actions().front()->menu() ||
            menu->actions().front()->menu()->actions().size() < 2) return;
        menu->actions().front()->menu()->actions().at(1)->trigger();
        menu->close();
    });
    QMetaObject::invokeMethod(classification_images, "customContextMenuRequested", Qt::DirectConnection,
                              Q_ARG(QPoint, QPoint(10, 10)));
    if (assigned_class != 1 || assigned_samples != std::vector<std::string>{"classification-sample-1"}) {
        return 34;
    }
    QTimer::singleShot(0, [] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu || menu->actions().isEmpty() || !menu->actions().front()->menu() ||
            menu->actions().front()->menu()->actions().isEmpty()) return;
        menu->actions().front()->menu()->actions().front()->trigger();
        menu->close();
    });
    QMetaObject::invokeMethod(image_labels, "customContextMenuRequested", Qt::DirectConnection,
                              Q_ARG(QPoint, QPoint(20, 20)));
    if (assigned_class != 0 || !classification_images->item(0)->text().contains("cat")) return 35;

    const auto click_thumbnail = [classification_images](int row, Qt::MouseButton button,
                                                          Qt::KeyboardModifiers modifiers) {
        const QPoint point = classification_images->visualItemRect(classification_images->item(row)).center();
        QMouseEvent press(QEvent::MouseButtonPress, point, button, button, modifiers);
        QApplication::sendEvent(classification_images->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, point, button, Qt::NoButton, modifiers);
        QApplication::sendEvent(classification_images->viewport(), &release);
    };
    click_thumbnail(0, Qt::LeftButton, Qt::NoModifier);
    click_thumbnail(2, Qt::LeftButton, Qt::ControlModifier);
    click_thumbnail(0, Qt::RightButton, Qt::NoModifier);
    QTimer::singleShot(0, [] {
        QMenu* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (menu) {
            menu->actions().front()->menu()->actions().front()->trigger();
            menu->close();
        }
    });
    QMetaObject::invokeMethod(classification_images, "customContextMenuRequested", Qt::DirectConnection,
        Q_ARG(QPoint, classification_images->visualItemRect(classification_images->item(0)).center()));
    if (assigned_class != 0 || assigned_samples != std::vector<std::string>{"classification-sample-1", "classification-sample-3"} ||
        classification_images->selectedItems().size() != 2 ||
        classification_images->item(1)->data(Qt::UserRole + 1).toInt() != 1) {
        return 113;
    }

    click_thumbnail(0, Qt::LeftButton, Qt::NoModifier);
    click_thumbnail(1, Qt::LeftButton, Qt::ShiftModifier);
    classification_classes->setCurrentRow(1);
    QPushButton* assign_class = classification.findChild<QPushButton*>("classificationAssignClassButton");
    if (!assign_class) {
        return 114;
    }
    assign_class->click();
    if (assigned_class != 1 || assigned_samples != std::vector<std::string>{"classification-sample-1", "classification-sample-2"} ||
        classification_images->selectedItems().size() != 2 ||
        classification_images->item(2)->data(Qt::UserRole + 1).toInt() != 0) {
        return 115;
    }

    classification.class_add_requested = [](const QString& name) {
        return std::vector<studio::AnnotationClass>{
            {"cat", 0, "cat", "#123456"}, {"dog", 0, "dog", "#456789"},
            {"new-class", 0, name.toStdString(), "#789123"}};
    };
    classification_classes->setCurrentRow(-1);
    QTimer::singleShot(0, [] {
        QInputDialog* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        if (dialog) {
            dialog->setTextValue(QStringLiteral("bird"));
            dialog->accept();
        }
    });
    assign_class->click();
    if (assigned_class != 2 || assigned_samples != std::vector<std::string>{"classification-sample-1", "classification-sample-2"} ||
        classification_images->selectedItems().size() != 2 ||
        classification_images->item(1)->data(Qt::UserRole + 1).toInt() != 2) {
        return 116;
    }

    studio::MaskCanvas stroke_canvas;
    stroke_canvas.set_class_value(1);
    stroke_canvas.resize(400, 300);
    QString stroke_error;
    const QString stroke_mask = directory.filePath("masks/stroke.png");
    if (!stroke_canvas.load(directory.filePath("images/segment.png"), stroke_mask, &stroke_error)) return 15;
    stroke_canvas.show();
    QApplication::processEvents();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(80, 150), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&stroke_canvas, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(320, 150), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&stroke_canvas, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(320, 150), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&stroke_canvas, &release);
    if (!stroke_canvas.save(stroke_mask, &stroke_error)) return 16;
    const QImage continuous(stroke_mask);
    if (continuous.isNull() || qGray(continuous.pixel(40, 30)) != 1) return 17;

    studio::MaskCanvas tool_canvas;
    tool_canvas.set_class_value(1);
    tool_canvas.resize(400, 300);
    const QString tool_mask = directory.filePath("masks/tools.png");
    if (!tool_canvas.load(directory.filePath("images/segment.png"), tool_mask, &stroke_error)) return 27;
    tool_canvas.show();
    QApplication::processEvents();
    tool_canvas.set_tool(studio::AnnotationTool::Rectangle);
    QMouseEvent rectangle_press(QEvent::MouseButtonPress, QPointF(100, 75), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&tool_canvas, &rectangle_press);
    QMouseEvent rectangle_move(QEvent::MouseMove, QPointF(300, 225), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&tool_canvas, &rectangle_move);
    QMouseEvent rectangle_release(QEvent::MouseButtonRelease, QPointF(300, 225), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&tool_canvas, &rectangle_release);
    if (!tool_canvas.save(tool_mask, &stroke_error) || qGray(QImage(tool_mask).pixel(40, 30)) != 1) return 28;
    tool_canvas.undo();
    tool_canvas.set_tool(studio::AnnotationTool::Fill);
    QMouseEvent fill_press(QEvent::MouseButtonPress, QPointF(200, 150), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&tool_canvas, &fill_press);
    if (!tool_canvas.save(tool_mask, &stroke_error) || qGray(QImage(tool_mask).pixel(0, 0)) != 1) return 29;
    tool_canvas.undo();
    auto add_polygon_points = [](studio::MaskCanvas& canvas) {
        for (const QPointF point : {QPointF(100, 225), QPointF(200, 75), QPointF(300, 225)}) {
            QMouseEvent polygon_press(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &polygon_press);
        }
    };
    tool_canvas.set_tool(studio::AnnotationTool::Polygon);
    add_polygon_points(tool_canvas);
    QMouseEvent polygon_finish_right(QEvent::MouseButtonPress, QPointF(300, 225), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(&tool_canvas, &polygon_finish_right);
    if (!tool_canvas.can_undo() || !tool_canvas.save(tool_mask, &stroke_error) ||
        qGray(QImage(tool_mask).pixel(40, 30)) != 1) return 30;
    tool_canvas.undo();
    if (!tool_canvas.save(tool_mask, &stroke_error) || qGray(QImage(tool_mask).pixel(40, 30)) != 0) return 41;
    tool_canvas.redo();
    if (!tool_canvas.save(tool_mask, &stroke_error) || qGray(QImage(tool_mask).pixel(40, 30)) != 1) return 42;

    const QString enter_mask = directory.filePath("masks/polygon-enter.png");
    studio::MaskCanvas enter_canvas;
    enter_canvas.set_class_value(1);
    enter_canvas.resize(400, 300);
    if (!enter_canvas.load(directory.filePath("images/segment.png"), enter_mask, &stroke_error)) return 43;
    enter_canvas.show();
    QApplication::processEvents();
    enter_canvas.set_tool(studio::AnnotationTool::Polygon);
    for (const QPointF point : {QPointF(100, 225), QPointF(200, 75)}) {
        QMouseEvent polygon_press(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&enter_canvas, &polygon_press);
    }
    QMouseEvent insufficient_right(QEvent::MouseButtonPress, QPointF(200, 75), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(&enter_canvas, &insufficient_right);
    QMouseEvent third_point(QEvent::MouseButtonPress, QPointF(300, 225), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&enter_canvas, &third_point);
    QKeyEvent enter_finish(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&enter_canvas, &enter_finish);
    if (!enter_canvas.save(enter_mask, &stroke_error) || qGray(QImage(enter_mask).pixel(40, 30)) != 1) return 44;

    const QString escape_mask = directory.filePath("masks/polygon-escape.png");
    studio::MaskCanvas escape_canvas;
    escape_canvas.set_class_value(1);
    escape_canvas.resize(400, 300);
    if (!escape_canvas.load(directory.filePath("images/segment.png"), escape_mask, &stroke_error)) return 45;
    escape_canvas.show();
    QApplication::processEvents();
    escape_canvas.set_tool(studio::AnnotationTool::Polygon);
    add_polygon_points(escape_canvas);
    QKeyEvent polygon_cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&escape_canvas, &polygon_cancel);
    QKeyEvent enter_after_cancel(QEvent::KeyPress, Qt::Key_Enter, Qt::NoModifier);
    QApplication::sendEvent(&escape_canvas, &enter_after_cancel);
    if (escape_canvas.is_dirty() || !escape_canvas.save(escape_mask, &stroke_error) ||
        qGray(QImage(escape_mask).pixel(40, 30)) != 0) return 46;

    const QString double_click_mask = directory.filePath("masks/polygon-double-click.png");
    studio::MaskCanvas double_click_canvas;
    double_click_canvas.set_class_value(1);
    double_click_canvas.resize(400, 300);
    if (!double_click_canvas.load(directory.filePath("images/segment.png"), double_click_mask, &stroke_error)) return 47;
    double_click_canvas.show();
    QApplication::processEvents();
    double_click_canvas.set_tool(studio::AnnotationTool::Polygon);
    add_polygon_points(double_click_canvas);
    QMouseEvent polygon_finish_double(QEvent::MouseButtonDblClick, QPointF(300, 225), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&double_click_canvas, &polygon_finish_double);
    if (!double_click_canvas.save(double_click_mask, &stroke_error) ||
        qGray(QImage(double_click_mask).pixel(40, 30)) != 1) return 48;

    int picked_class = -1;
    tool_canvas.class_picked = [&picked_class](int value) { picked_class = value; };
    tool_canvas.set_tool(studio::AnnotationTool::Pick);
    QMouseEvent pick_press(QEvent::MouseButtonPress, QPointF(200, 150), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&tool_canvas, &pick_press);
    const int fit_zoom = tool_canvas.zoom_percent();
    tool_canvas.zoom_by(1.2);
    if (picked_class != 1 || tool_canvas.zoom_percent() <= fit_zoom) return 31;
    tool_canvas.reset_view();
    if (tool_canvas.zoom_percent() != 100) return 32;

    QImage threshold_source(16, 8, QImage::Format_RGB888);
    for (int y = 0; y < threshold_source.height(); ++y)
        for (int x = 0; x < threshold_source.width(); ++x)
            threshold_source.setPixelColor(
                x, y, QColor(x < 8 ? 50 : 200,
                             x < 8 ? 50 : 200,
                             x < 8 ? 50 : 200));
    const QString threshold_image =
        directory.filePath("images/threshold.png");
    const QString threshold_mask =
        directory.filePath("masks/threshold.png");
    if (!threshold_source.save(threshold_image)) return 49;
    bool interval_dialog_checked = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto* low = dialog->findChild<QSpinBox*>("thresholdLow");
        auto* high = dialog->findChild<QSpinBox*>("thresholdHigh");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        auto* preview = dialog->findChild<QLabel*>("thresholdPreview");
        if (low && high && buttons && preview) {
            auto* apply = buttons->button(QDialogButtonBox::Ok);
            low->setValue(220);
            const bool invalid_low = high->value() == 192 && !apply->isEnabled() &&
                                     preview->text().contains("must not exceed");
            high->setValue(240);
            const bool recovered = low->value() == 220 && apply->isEnabled() &&
                                   !preview->pixmap(Qt::ReturnByValue).isNull();
            high->setValue(30);
            const bool invalid_high = low->value() == 220 && !apply->isEnabled();
            low->setValue(30);
            interval_dialog_checked = invalid_low && recovered && invalid_high &&
                                      high->value() == 30 && apply->isEnabled();
        }
        dialog->reject();
    });
    no_class_canvas->threshold_roi_selected(QRect(0, 0, 16, 8), threshold_source);
    if (!interval_dialog_checked) return 93;
    studio::SegmentationAnnotationWidget remembered_annotation;
    studio::TaskSampleBinding first_threshold_sample;
    first_threshold_sample.sample_id = "threshold-first";
    first_threshold_sample.image_paths = {directory.filePath("train.png").toStdString()};
    studio::TaskSampleBinding second_threshold_sample;
    second_threshold_sample.sample_id = "threshold-second";
    second_threshold_sample.image_paths = {directory.filePath("val.png").toStdString()};
    remembered_annotation.set_task_dataset({first_threshold_sample, second_threshold_sample});
    auto* remembered_canvas = remembered_annotation.findChild<studio::MaskCanvas*>("segmentationMaskCanvas");
    auto* remembered_samples = remembered_annotation.findChild<QListWidget*>("annotationImageList");
    if (!remembered_canvas || !remembered_samples) {
        return 100;
    }
    remembered_canvas->set_class_value(1);
    for (int step = 0; step < 4; ++step) {
        remembered_canvas->mark_saved();
        remembered_samples->setCurrentRow(step % 2);
        bool settings_checked = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) {
                return;
            }
            auto* low = dialog->findChild<QSpinBox*>("thresholdLow");
            auto* high = dialog->findChild<QSpinBox*>("thresholdHigh");
            auto* mode = dialog->findChild<QComboBox*>("thresholdMode");
            auto* remember = dialog->findChild<QCheckBox*>("thresholdRememberParameters");
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (!low || !high || !mode || !remember || !buttons) {
                dialog->reject();
                return;
            }
            const bool defaults = step == 0 || step == 3;
            settings_checked = low->value() == (defaults ? 64 : 30) &&
                high->value() == (defaults ? 192 : 160) &&
                mode->currentData().toBool() == !defaults && remember->isChecked() == !defaults;
            if (step == 0) {
                low->setValue(30);
                high->setValue(160);
                mode->setCurrentIndex(1);
                remember->setChecked(true);
            } else {
                remember->setChecked(false);
            }
            if (step == 0 || step == 2) {
                buttons->button(QDialogButtonBox::Ok)->click();
            } else {
                low->setValue(70);
                high->setValue(190);
                mode->setCurrentIndex(0);
                dialog->reject();
            }
        });
        remembered_canvas->threshold_roi_selected(QRect(0, 0, 16, 8), threshold_source);
        if (!settings_checked) {
            return 101;
        }
    }
    studio::MaskCanvas threshold_canvas;
    threshold_canvas.set_class_value(3);
    if (!threshold_canvas.load(
            threshold_image, threshold_mask, &stroke_error))
        return 89;
    threshold_canvas.apply_threshold_roi(
        QRect(0, 0, 16, 8), {{40, 80}}, false);
    if (!threshold_canvas.save(threshold_mask, &stroke_error))
        return 51;
    QImage threshold_result(threshold_mask);
    if (qGray(threshold_result.pixel(2, 2)) != 3 ||
        qGray(threshold_result.pixel(12, 2)) != 0)
        return 52;
    threshold_canvas.undo();
    threshold_canvas.apply_threshold_roi(
        QRect(0, 0, 16, 8), {{40, 80}}, true);
    threshold_canvas.save(threshold_mask, &stroke_error);
    threshold_result = QImage(threshold_mask);
    if (qGray(threshold_result.pixel(2, 2)) != 0 ||
        qGray(threshold_result.pixel(12, 2)) != 3)
        return 53;
    threshold_canvas.undo();
    threshold_canvas.apply_threshold_roi(
        QRect(0, 0, 16, 8), {{50, 50}, {200, 200}}, false);
    if (!threshold_canvas.save(threshold_mask, &stroke_error)) return 94;
    threshold_result = QImage(threshold_mask);
    if (qGray(threshold_result.pixel(2, 2)) != 3 ||
        qGray(threshold_result.pixel(12, 2)) != 3) return 95;
    threshold_canvas.undo();
    threshold_canvas.apply_threshold_roi(
        QRect(0, 0, 16, 8), {{80, 40}}, true);
    threshold_canvas.apply_threshold_roi(
        QRect(0, 0, 16, 8), {}, true);
    if (!threshold_canvas.save(threshold_mask, &stroke_error)) return 96;
    threshold_result = QImage(threshold_mask);
    if (qGray(threshold_result.pixel(2, 2)) != 0 ||
        qGray(threshold_result.pixel(12, 2)) != 0) return 97;
    threshold_canvas.apply_threshold_roi(
        QRect(0, 0, 16, 8), {{50, 50}}, false);
    if (!threshold_canvas.save(threshold_mask, &stroke_error)) return 98;
    threshold_result = QImage(threshold_mask);
    if (qGray(threshold_result.pixel(2, 2)) != 3 ||
        qGray(threshold_result.pixel(12, 2)) != 0) return 99;
    threshold_canvas.undo();
    threshold_canvas.apply_ellipse(QRect(2, 1, 12, 6));
    threshold_canvas.save(threshold_mask, &stroke_error);
    threshold_result = QImage(threshold_mask);
    if (qGray(threshold_result.pixel(8, 4)) != 3 ||
        qGray(threshold_result.pixel(0, 0)) != 0)
        return 54;
    threshold_canvas.undo();
    threshold_canvas.save(threshold_mask, &stroke_error);
    if (qGray(QImage(threshold_mask).pixel(8, 4)) != 0)
        return 55;

    QImage move_source(16, 8, QImage::Format_RGB888);
    move_source.fill(QColor("#708080"));
    QImage move_seed(16, 8, QImage::Format_Grayscale8);
    move_seed.fill(0);
    for (int y = 2; y <= 4; ++y)
        for (int x = 2; x <= 5; ++x)
            move_seed.scanLine(y)[x] = 3;
    for (int y = 5; y <= 6; ++y)
        for (int x = 12; x <= 13; ++x)
            move_seed.scanLine(y)[x] = 3;
    const auto selected_region =
        studio::AnnotationRegionEditService::select_connected_region(
            move_seed, QPoint(3, 3), 3);
    if (!selected_region) return 68;
    const QRectF selected_bounds =
        studio::AnnotationRegionEditService::region_bounds(*selected_region);
    if (selected_bounds != QRectF(2, 2, 4, 3)) return 69;
    const auto nearly_equal = [](qreal left, qreal right) {
        return std::abs(left - right) < 0.001;
    };
    const auto same_point = [&nearly_equal](const QPointF& left, const QPointF& right) {
        return nearly_equal(left.x(), right.x()) && nearly_equal(left.y(), right.y());
    };

    studio::AnnotationRegionTransform corner_scale;
    corner_scale.anchor = selected_bounds.bottomRight();
    corner_scale.scale_x = 1.5;
    corner_scale.scale_y = 1.5;
    const QTransform corner_matrix =
        studio::AnnotationRegionEditService::transform_matrix(corner_scale);
    if (!same_point(corner_matrix.map(corner_scale.anchor), corner_scale.anchor) ||
        !same_point(corner_matrix.map(selected_bounds.topLeft()), QPointF(0, 0.5)))
        return 70;

    studio::AnnotationRegionTransform horizontal_scale;
    horizontal_scale.anchor = QPointF(selected_bounds.left(), selected_bounds.center().y());
    horizontal_scale.scale_x = 2.0;
    const QTransform horizontal_matrix =
        studio::AnnotationRegionEditService::transform_matrix(horizontal_scale);
    if (!same_point(horizontal_matrix.map(horizontal_scale.anchor), horizontal_scale.anchor) ||
        !same_point(horizontal_matrix.map(selected_bounds.topRight()), QPointF(10, 2)))
        return 71;

    studio::AnnotationRegionTransform vertical_scale;
    vertical_scale.anchor = QPointF(selected_bounds.center().x(), selected_bounds.top());
    vertical_scale.scale_y = 2.0;
    const QTransform vertical_matrix =
        studio::AnnotationRegionEditService::transform_matrix(vertical_scale);
    if (!same_point(vertical_matrix.map(vertical_scale.anchor), vertical_scale.anchor) ||
        !same_point(vertical_matrix.map(selected_bounds.bottomLeft()), QPointF(2, 8)))
        return 72;

    studio::AnnotationRegionTransform rotation;
    rotation.anchor = selected_bounds.center();
    rotation.rotation_degrees = 90.0;
    const QTransform rotation_matrix =
        studio::AnnotationRegionEditService::transform_matrix(rotation);
    const QPointF rotated_right = rotation_matrix.map(
        QPointF(selected_bounds.right(), selected_bounds.center().y()));
    if (!same_point(rotation_matrix.map(rotation.anchor), rotation.anchor) ||
        !nearly_equal(rotated_right.x(), rotation.anchor.x()) ||
        !nearly_equal(std::abs(rotated_right.y() - rotation.anchor.y()),
                      selected_bounds.width() / 2.0))
        return 73;

    const QImage horizontally_scaled =
        studio::AnnotationRegionEditService::transformed_mask(
            move_seed, *selected_region, horizontal_matrix);
    const auto scaled_region =
        studio::AnnotationRegionEditService::select_connected_region(
            horizontally_scaled, QPoint(3, 3), 3);
    if (!scaled_region ||
        studio::AnnotationRegionEditService::region_bounds(*scaled_region).width() <=
            selected_bounds.width() ||
        studio::AnnotationRegionEditService::region_bounds(*scaled_region).height() !=
            selected_bounds.height())
        return 74;
    const QString move_image = directory.filePath("images/move.png");
    const QString move_mask = directory.filePath("masks/move.png");
    if (!move_source.save(move_image) || !move_seed.save(move_mask)) return 62;
    studio::MaskCanvas move_canvas;
    move_canvas.resize(320, 160);
    move_canvas.set_class_value(3);
    if (!move_canvas.load(move_image, move_mask, &stroke_error)) return 63;
    move_canvas.show();
    QApplication::processEvents();
    const auto canvas_point = [&move_source](const QPointF& image_point) {
        const QRectF target = studio::ImageViewportTransform{}.target_rect(
            move_source.size(), QSize(320 - 48, 160 - 48))
                                  .translated(24, 24);
        return QPointF(
            target.left() + image_point.x() * target.width() / move_source.width(),
            target.top() + image_point.y() * target.height() / move_source.height());
    };
    move_canvas.set_tool(studio::AnnotationTool::TransformSelection);
    QMouseEvent select_move_region(QEvent::MouseButtonDblClick, canvas_point({3.5, 3.5}),
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&move_canvas, &select_move_region);
    QMouseEvent move_label_press(QEvent::MouseButtonPress, canvas_point({3.5, 3.5}),
                                 Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&move_canvas, &move_label_press);
    QMouseEvent move_label_drag(QEvent::MouseMove, canvas_point({7.5, 3.5}),
                                Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&move_canvas, &move_label_drag);
    QMouseEvent move_label_release(QEvent::MouseButtonRelease, canvas_point({7.5, 3.5}),
                                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&move_canvas, &move_label_release);
    if (!move_canvas.can_undo() || !move_canvas.save(move_mask, &stroke_error)) return 64;
    QImage moved_mask(move_mask);
    if (qGray(moved_mask.pixel(3, 3)) != 0 ||
        qGray(moved_mask.pixel(7, 3)) != 3 ||
        qGray(moved_mask.pixel(12, 5)) != 3) return 65;
    move_canvas.undo();
    if (!move_canvas.save(move_mask, &stroke_error)) return 66;
    QImage restored_mask(move_mask);
    if (qGray(restored_mask.pixel(3, 3)) != 3 || qGray(restored_mask.pixel(7, 3)) != 0) return 67;

    // Hiding annotation clears an existing selection and prevents selecting
    // hidden pixels, even when an inference overlay is present.
    QApplication::sendEvent(&move_canvas, &select_move_region);
    move_canvas.set_prediction_overlay(prediction_color);
    move_canvas.set_overlay_visible(false);
    QApplication::sendEvent(&move_canvas, &select_move_region);
    QApplication::sendEvent(&move_canvas, &move_label_press);
    QApplication::sendEvent(&move_canvas, &move_label_drag);
    QApplication::sendEvent(&move_canvas, &move_label_release);
    if (move_canvas.mask_image() != move_seed || move_canvas.is_dirty()) {
        return 102;
    }
    move_canvas.set_overlay_visible(true);
    QApplication::sendEvent(&move_canvas, &select_move_region);
    QApplication::sendEvent(&move_canvas, &move_label_press);
    QApplication::sendEvent(&move_canvas, &move_label_drag);
    move_canvas.set_overlay_visible(false);
    QApplication::sendEvent(&move_canvas, &move_label_release);
    if (move_canvas.mask_image() != move_seed || move_canvas.is_dirty()) {
        return 103;
    }
    move_canvas.set_overlay_visible(true);

    const QString transform_mask = directory.filePath("masks/transform.png");
    if (!move_seed.save(transform_mask)) return 75;
    studio::MaskCanvas transform_canvas;
    transform_canvas.resize(320, 160);
    transform_canvas.set_class_value(3);
    if (!transform_canvas.load(move_image, transform_mask, &stroke_error)) return 76;
    transform_canvas.show();
    QApplication::processEvents();
    transform_canvas.set_tool(studio::AnnotationTool::TransformSelection);
    const auto select_transform_region = [&transform_canvas, &canvas_point] {
        QMouseEvent select(QEvent::MouseButtonDblClick, canvas_point({3.5, 3.5}),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&transform_canvas, &select);
    };

    select_transform_region();
    QMouseEvent right_edge_press(QEvent::MouseButtonPress, canvas_point({6.0, 3.5}),
                                 Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &right_edge_press);
    QMouseEvent right_edge_drag(QEvent::MouseMove, canvas_point({9.0, 3.5}),
                                Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &right_edge_drag);
    QMouseEvent right_edge_release(QEvent::MouseButtonRelease, canvas_point({9.0, 3.5}),
                                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &right_edge_release);
    if (!transform_canvas.can_undo() || !transform_canvas.save(transform_mask, &stroke_error))
        return 77;
    if (qGray(QImage(transform_mask).pixel(7, 3)) != 3) return 78;
    transform_canvas.undo();
    if (!transform_canvas.save(transform_mask, &stroke_error) || QImage(transform_mask) != move_seed)
        return 79;

    select_transform_region();
    QMouseEvent corner_press(QEvent::MouseButtonPress, canvas_point({2.0, 2.0}),
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &corner_press);
    QMouseEvent corner_drag(QEvent::MouseMove, canvas_point({1.0, 1.0}),
                            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &corner_drag);
    QMouseEvent corner_release(QEvent::MouseButtonRelease, canvas_point({1.0, 1.0}),
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &corner_release);
    if (!transform_canvas.save(transform_mask, &stroke_error) ||
        QImage(transform_mask) == move_seed)
        return 80;
    transform_canvas.undo();
    if (!transform_canvas.save(transform_mask, &stroke_error) || QImage(transform_mask) != move_seed)
        return 81;

    select_transform_region();
    const QPointF rotation_start = canvas_point({6.0, 2.0}) + QPointF(0.0, -24.0);
    const QPointF rotation_end = canvas_point({4.0, 3.5}) + QPointF(45.0, 0.0);
    QMouseEvent rotation_press(QEvent::MouseButtonPress, rotation_start,
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &rotation_press);
    QMouseEvent rotation_drag(QEvent::MouseMove, rotation_end,
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &rotation_drag);
    QMouseEvent rotation_release(QEvent::MouseButtonRelease, rotation_end,
                                 Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &rotation_release);
    if (!transform_canvas.save(transform_mask, &stroke_error) ||
        QImage(transform_mask) == move_seed)
        return 82;
    transform_canvas.undo();
    if (!transform_canvas.save(transform_mask, &stroke_error) || QImage(transform_mask) != move_seed)
        return 83;

    select_transform_region();
    QMouseEvent top_edge_press(QEvent::MouseButtonPress, canvas_point({4.0, 2.0}),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &top_edge_press);
    QMouseEvent top_edge_drag(QEvent::MouseMove, canvas_point({4.0, 1.0}),
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &top_edge_drag);
    QMouseEvent top_edge_release(QEvent::MouseButtonRelease, canvas_point({4.0, 1.0}),
                                 Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &top_edge_release);
    if (!transform_canvas.save(transform_mask, &stroke_error)) return 90;
    const QImage top_scaled(transform_mask);
    if (qGray(top_scaled.pixel(3, 1)) != 3 ||
        qGray(top_scaled.pixel(3, 3)) != 3 ||
        qGray(top_scaled.pixel(1, 3)) != 0 ||
        qGray(top_scaled.pixel(6, 3)) != 0)
        return 91;
    transform_canvas.undo();
    if (!transform_canvas.save(transform_mask, &stroke_error) || QImage(transform_mask) != move_seed)
        return 92;

    select_transform_region();
    QMouseEvent cancel_press(QEvent::MouseButtonPress, canvas_point({6.0, 3.5}),
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &cancel_press);
    QMouseEvent cancel_drag(QEvent::MouseMove, canvas_point({9.0, 3.5}),
                            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &cancel_drag);
    QKeyEvent cancel_transform(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &cancel_transform);
    QMouseEvent cancel_release(QEvent::MouseButtonRelease, canvas_point({9.0, 3.5}),
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&transform_canvas, &cancel_release);
    if (!transform_canvas.save(transform_mask, &stroke_error) || QImage(transform_mask) != move_seed)
        return 84;
    return 0;
}
