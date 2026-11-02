#include "classification_annotation.h"
#include "annotation_region_edit_service.h"
#include "dataset_import_dialog.h"
#include "dataset_import_service.h"
#include "dataset_item_model.h"
#include "image_viewport_transform.h"
#include "model_presentation.h"
#include "sample_presentation.h"
#include "segmentation_annotation.h"
#include "segmentation_annotation_state.h"
#include "task_dataset_binding.h"
#include "task_workspace_service.h"

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

namespace {

void accept_input_dialog(const QString& value, int attempts = 50) {
    if (auto* dialog = qobject_cast<QInputDialog*>(
            QApplication::activeModalWidget())) {
        dialog->setTextValue(value);
        dialog->accept();
        return;
    }
    if (attempts > 0)
        QTimer::singleShot(
            10, [value, attempts] {
                accept_input_dialog(value, attempts - 1);
            });
}

} // namespace

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
    studio::import_segmentation_annotation({{"foreground", 1, "foreground", "#1F8A9E", true, false, true}},
        annotation_sample, directory.filePath("masks/segment.png"));
    annotation_sample.split = "train";
    annotation_sample.annotation_state = "annotated";
    studio::SegmentationAnnotationWidget annotation;
    annotation.resize(1000, 600);
    annotation.set_task_dataset(
        directory.path(),
        {annotation_sample},
        {studio::AnnotationClass{
            "foreground", 1, "foreground", "#1F8A9E",
            true, false, true}});
    studio::InferenceResultSet segmentation_results;
    jocelin::result::sg::SgResult prediction;
    prediction.image_path = annotation_sample.image_paths.front();
    prediction.result_name = "segment";
    prediction.save_dir = directory.filePath("results").toStdString();
    prediction.image_width = first.width();
    prediction.image_height = first.height();
    jocelin::result::sg::SgResult::Component predicted_region;
    predicted_region.label = 1;
    predicted_region.interest = true;
    for (int row = 4; row < 20; ++row) predicted_region.region.vRun.emplace_back(row, 5, 30, 1);
    prediction.components.push_back(predicted_region);
    segmentation_results.samples.push_back({annotation_sample.sample_id, prediction});
    annotation.set_inference_result(&segmentation_results);
    annotation.show();
    QApplication::processEvents();
    auto* canvas = annotation.findChild<studio::MaskCanvas*>("segmentationMaskCanvas");
    auto* annotation_list = annotation.findChild<QListWidget*>("annotationImageList");
    auto* segmentation_classes = annotation.findChild<QListWidget*>("segmentationClassList");
    auto* annotation_quality = annotation.findChild<QLabel*>("annotationQualitySummary");
    if (!canvas || !annotation_list || annotation_list->count() != 1 ||
        annotation_list->item(0)->icon().isNull() || !segmentation_classes)
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
        directory.path(),
        {studio::TaskSampleBinding{
            "invalid-sample",
            {directory.filePath("images/segment.png").toStdString()},
            true,
            {},
            {},
            {},
            "train",
            "annotated",
            {}}},
        {studio::AnnotationClass{
            "foreground", 1, "foreground", "#1F8A9E",
            true, false, true}});
    background_annotation.show();
    QApplication::processEvents();
    auto* background_list = background_annotation.findChild<QListWidget*>("annotationImageList");
    if (!background_list || background_list->count() != 1 ||
        !background_list->item(0)->text().contains(QStringLiteral("✓"))) return 88;

    studio::SegmentationAnnotationWidget no_class_annotation;
    no_class_annotation.resize(900, 600);
    no_class_annotation.set_task_dataset(
        directory.filePath("late-import"),
        {studio::TaskSampleBinding{
            "late-sample",
            {directory.filePath(
                "late-import/connected-after-import.jpg").toStdString()},
            true, {}, {}, {}, "unassigned", "unannotated", {}}},
        {});
    no_class_annotation.show();
    QApplication::processEvents();
    auto* no_class_list = no_class_annotation.findChild<QListWidget*>("annotationImageList");
    auto* class_combo = no_class_annotation.findChild<QComboBox*>("segmentationClassCombo");
    auto* brush_button = no_class_annotation.findChild<QToolButton*>("segmentationBrushButton");
    auto* no_class_canvas = no_class_annotation.findChild<studio::MaskCanvas*>("segmentationMaskCanvas");
    if (!no_class_list || no_class_list->count() != 1 || !class_combo || class_combo->count() != 0 ||
        !brush_button || !brush_button->isEnabled() || !no_class_canvas) return 20;
    std::vector<studio::AnnotationClass> emitted_classes;
    QString saved_image;
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
    no_class_annotation.class_add_requested =
        [&](const QString& name) {
            const int mask_value =
                static_cast<int>(emitted_classes.size()) + 1;
            emitted_classes.push_back({
                "class-" + std::to_string(mask_value),
                mask_value,
                name.toStdString(),
                studio::default_class_color(mask_value),
                true, false, true});
            empty_service.binding().classes = emitted_classes;
            no_class_annotation.set_class_schema(
                emitted_classes);
        };
    no_class_annotation.mask_saved = [&](const QString& image, const QImage& mask) {
        saved_image = image;
        saved_mask = mask.copy();
        empty_service.set_segmentation_mask_for_image(image.toStdString(), mask);
        no_class_annotation.refresh_samples(empty_service.binding().samples, {});
    };
    QTimer::singleShot(0, [] {
        accept_input_dialog("vehicle");
    });
    brush_button->click();
    QApplication::processEvents();
    const QPoint paint_point = no_class_canvas->rect().center();
    QMouseEvent first_annotation_press(QEvent::MouseButtonPress, paint_point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(no_class_canvas, &first_annotation_press);
    QMouseEvent first_annotation_release(QEvent::MouseButtonRelease, paint_point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(no_class_canvas, &first_annotation_release);
    QApplication::processEvents();
    if (emitted_classes.size() != 1 ||
        emitted_classes.front().name != "vehicle" ||
        !no_class_canvas->is_dirty()) return 21;

    class_combo->setEditText("road");
    auto* add_class = no_class_annotation.findChild<QPushButton*>("segmentationAddClassButton");
    if (!add_class) return 22;
    add_class->click();
    QApplication::processEvents();
    auto* class_list = no_class_annotation.findChild<QListWidget*>("segmentationClassList");
    auto* polygon_button = no_class_annotation.findChild<QToolButton*>("segmentationPolygonButton");
    auto* rectangle_button = no_class_annotation.findChild<QToolButton*>("segmentationRectangleButton");
    auto* ellipse_button = no_class_annotation.findChild<QToolButton*>("segmentationEllipseButton");
    auto* threshold_button = no_class_annotation.findChild<QToolButton*>("segmentationThresholdButton");
    auto* fill_button = no_class_annotation.findChild<QToolButton*>("segmentationFillButton");
    if (emitted_classes.size() != 2 ||
        emitted_classes.back().name != "road" ||
        class_combo->count() != 2 || !brush_button->isEnabled() ||
        !class_list || class_list->count() != 2 || class_list->item(0)->icon().isNull() ||
        !polygon_button || !polygon_button->isEnabled() || !rectangle_button || !rectangle_button->isEnabled() ||
        !ellipse_button || !ellipse_button->isEnabled() ||
        !threshold_button || !threshold_button->isEnabled() ||
        !fill_button || !fill_button->isEnabled()) return 23;
    auto* save_mask = no_class_annotation.findChild<QToolButton*>("segmentationSaveMaskButton");
    if (!no_class_canvas->is_dirty() || !save_mask) return 24;
    save_mask->click();
    QApplication::processEvents();
    if (saved_image.isEmpty() || saved_mask.isNull()) return 25;
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
    no_class_annotation.set_task_dataset(directory.filePath("late-import"),
        empty_service.binding().samples, emitted_classes);
    // Inspect the canvas through its existing export API, inside the temporary test fixture.
    const QString canvas_capture = directory.filePath("empty-canvas.png");
    QString canvas_error;
    if (no_class_canvas->is_dirty() || !no_class_canvas->save(canvas_capture, &canvas_error)) return 93;
    const QImage empty_canvas = QImage(canvas_capture).convertToFormat(QImage::Format_Grayscale8);
    if (empty_canvas.isNull()) return 95;
    for (int row = 0; row < empty_canvas.height(); ++row)
        if (std::any_of(empty_canvas.constScanLine(row),
            empty_canvas.constScanLine(row) + empty_canvas.width(), [](uchar value) { return value != 0; })) return 96;

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
             true, {}, {}, "dog", "unassigned", "annotated", {}}},
        {"cat", "dog"},
        {-1, 1});
    studio::InferenceResultSet classification_results;
    jocelin::result::cls::ClsResult class_prediction;
    class_prediction.image_path = directory.filePath("images/segment.png").toStdString();
    class_prediction.predicted_class = 2;
    class_prediction.confidence = 0.91;
    class_prediction.heatmap_path = directory.filePath("results/segment_heatmap.png").toStdString();
    classification_results.samples.push_back({"classification-sample-1", class_prediction});
    classification.set_inference_result(&classification_results, {"dog", "cat"});
    QString assigned_image;
    int assigned_class = -1;
    classification.class_assigned = [&](const QString& image, int class_index) {
        assigned_image = image;
        assigned_class = class_index;
    };
    classification.show();
    QApplication::processEvents();
    auto* classification_images = classification.findChild<QListWidget*>("classificationImageList");
    auto* classification_classes = classification.findChild<QListWidget*>("classificationClassList");
    auto* classification_preview = classification.findChild<QLabel*>("classificationPreview");
    auto* ground_truth_button = classification.findChild<QPushButton*>("classificationGroundTruthButton");
    auto* prediction_button = classification.findChild<QPushButton*>("classificationPredictionButton");
    auto* heatmap_button = classification.findChild<QPushButton*>("classificationHeatmapButton");
    auto* review_state = classification.findChild<QLabel*>("classificationReviewState");
    if (!classification_images || classification_images->count() != 2 || !classification_classes ||
        classification_classes->count() != 2 || !classification_preview || !classification_preview->pixmap() ||
        classification_preview->pixmap()->isNull() || !ground_truth_button ||
        !prediction_button || !heatmap_button || !review_state || !ground_truth_button->isChecked() ||
        !prediction_button->isEnabled() || !heatmap_button->isEnabled()) return 33;
    prediction_button->click();
    QApplication::processEvents();
    if (!prediction_button->isChecked() || !review_state->text().contains("cat") ||
        !review_state->text().contains("91.0%")) return 67;
    heatmap_button->click();
    QApplication::processEvents();
    if (!heatmap_button->isChecked() || !review_state->text().contains("Heatmap") ||
        !classification_preview->pixmap() || classification_preview->pixmap()->isNull()) return 68;
    ground_truth_button->click();
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
    if (assigned_class != 1 || QFileInfo(assigned_image).absoluteFilePath() !=
        QFileInfo(directory.filePath("images/segment.png")).absoluteFilePath()) return 34;
    QTimer::singleShot(0, [] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu || menu->actions().isEmpty() || !menu->actions().front()->menu() ||
            menu->actions().front()->menu()->actions().isEmpty()) return;
        menu->actions().front()->menu()->actions().front()->trigger();
        menu->close();
    });
    QMetaObject::invokeMethod(classification_preview, "customContextMenuRequested", Qt::DirectConnection,
                              Q_ARG(QPoint, QPoint(20, 20)));
    if (assigned_class != 0 || !classification_images->item(0)->text().contains("cat")) return 35;

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
    remembered_annotation.set_task_dataset(directory.path(),
        {first_threshold_sample, second_threshold_sample},
        {{"threshold-class", 1, "foreground", "#00ff00", true, false, true}});
    auto* remembered_canvas = remembered_annotation.findChild<studio::MaskCanvas*>("segmentationMaskCanvas");
    auto* remembered_samples = remembered_annotation.findChild<QListWidget*>("annotationImageList");
    if (!remembered_canvas || !remembered_samples) {
        return 100;
    }
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
