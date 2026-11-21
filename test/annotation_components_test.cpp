#include "common/ui/editing/confirm_transition.h"
#include "modules/settings/application/annotation_preferences.h"
#include "modules/task/application/task_dataset_binding.h"
#include "modules/task/ui/annotation/sg/segmentation_annotation.h"
#include "modules/task/ui/annotation/od/canvas/detection_canvas.h"
#include "modules/task/ui/samples/sample_browser.h"
#include "modules/task/ui/task_workspace.h"

#include <QApplication>
#include <QAbstractButton>
#include <QImage>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <stdexcept>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool decide(QMessageBox::StandardButton answer, const std::function<bool()>& operation) {
    QTimer dialog;
    QObject::connect(&dialog, &QTimer::timeout, [&] {
        QMessageBox* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (message) {
            message->button(answer)->click();
        }
    });
    dialog.start(1);
    return operation();
}

} // namespace

int main(int argc, char** argv) try {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "cannot create annotation fixture");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QWidget host;
    int saved = 0;
    int discarded = 0;
    const auto save = [&] { ++saved; return true; };
    const auto discard = [&] { ++discarded; };
    require(studio::confirm_transition(&host, false, false, save, discard) && saved == 0,
        "clean components must continue without saving");
    studio::set_annotation_auto_save(true);
    require(studio::confirm_transition(&host, true, studio::annotation_auto_save(), save, discard)
        && saved == 1 && discarded == 0, "the configured default must save without a prompt");
    require(!studio::confirm_transition(&host, true, true, [] { return false; }, discard),
        "a failed automatic save must stop the transition");
    studio::set_annotation_auto_save(false);
    require(!decide(QMessageBox::Cancel, [&] {
        return studio::confirm_transition(&host, true, studio::annotation_auto_save(), save, discard);
    }) && saved == 1 && discarded == 0, "cancel must retain pending edits");
    require(decide(QMessageBox::Save, [&] {
        return studio::confirm_transition(&host, true, false, save, discard);
    }) && saved == 2, "Save must commit before continuing");
    require(decide(QMessageBox::Discard, [&] {
        return studio::confirm_transition(&host, true, false, save, discard);
    }) && discarded == 1, "Discard must actually reset the editor");

    // Browser source identities need not be contiguous after filtering.
    studio::SampleBrowser browser;
    browser.set_entries({{"a", {}, "A", {}, {}, {}, 2}, {"b", {}, "B", {}, {}, {}, 7}});
    browser.current_requested = [](int, const QString&) { return false; };
    browser.setCurrentRow(1);
    require(browser.current_source_index() == 2, "cancelled sample changes lost the accepted source identity");
    browser.current_requested = [](int index, const QString&) { return index == 7; };
    browser.setCurrentRow(1);
    browser.update_entry_state(7, "B", "annotated", "test");
    require(browser.current_source_index() == 7, "filtered navigation used a binding index as a row");

    // A page transition must invoke SG's component, just like OD's component.
    const QString image_path = temporary.filePath("sample.png");
    QImage image(32, 24, QImage::Format_RGB888);
    image.fill(Qt::white);
    require(image.save(image_path), "cannot create image");
    studio::WorkflowGraph graph;
    const int dataset = graph.add_node(studio::NodeKind::DatasetInput);
    const int task = graph.add_task_node({"sg"});
    std::get<studio::DatasetConfig>(graph.node(dataset).config).items = {
        {image_path.toStdString(), {}, true, "sample"}};
    graph.connect(dataset, 0, task, 0);
    studio::TaskWorkspace workspace;
    workspace.bind(&graph, task);
    workspace.activate_step(studio::TaskWorkspace::Step::Annotation);
    studio::SegmentationAnnotationWidget* editor = nullptr;
    for (QWidget* widget : workspace.findChildren<QWidget*>()) {
        if (studio::SegmentationAnnotationWidget* candidate =
                dynamic_cast<studio::SegmentationAnnotationWidget*>(widget)) {
            editor = candidate;
            break;
        }
    }
    require(editor != nullptr, "SG editor is unavailable");
    studio::TaskWorkspaceService service(graph, task);
    const std::string class_id = service.add_class("object");
    const int value = studio::find_annotation_class(service.binding(), class_id)->mask_value;
    QImage mask(image.size(), QImage::Format_Grayscale8);
    mask.fill(value);
    editor->canvas()->apply_mask(mask, true);
    require(!decide(QMessageBox::Cancel, [&] { return workspace.confirm_annotation_transition(); })
        && editor->canvas()->is_dirty(), "SG cancellation bypassed the shared lifecycle");
    require(decide(QMessageBox::Discard, [&] { return workspace.confirm_annotation_transition(); })
        && !editor->canvas()->is_dirty() && service.binding().samples.front().regions.empty(),
        "SG discard retained a draft or committed it");
    editor->canvas()->apply_mask(mask, true);
    studio::set_annotation_auto_save(true);
    workspace.activate_step(studio::TaskWorkspace::Step::Data);
    require(workspace.active_step() == studio::TaskWorkspace::Step::Data
        && !editor->canvas()->is_dirty() && !service.binding().samples.front().regions.empty(),
        "SG page transition failed to apply the configured automatic save");

    const int od_task = graph.add_task_node({"od"});
    graph.connect(dataset, 0, od_task, 0);
    studio::TaskWorkspaceService od_service(graph, od_task);
    od_service.add_class("object");
    workspace.bind(&graph, od_task);
    workspace.activate_step(studio::TaskWorkspace::Step::Annotation);
    workspace.resize(1280, 800);
    workspace.show();
    application.processEvents();
    studio::DetectionCanvas* canvas = nullptr;
    for (QWidget* widget : workspace.findChildren<QWidget*>()) {
        if (studio::DetectionCanvas* candidate = dynamic_cast<studio::DetectionCanvas*>(widget)) {
            canvas = candidate;
            break;
        }
    }
    require(canvas != nullptr, "OD canvas is unavailable");
    workspace.findChild<QListWidget*>("odClassList")->setCurrentRow(0);
    const auto draw = [&] {
        const QRectF frame = studio::ImageViewportTransform{}.target_rect(image.size(), canvas->size());
        const QPointF start = frame.topLeft() + QPointF(frame.width() * 0.2, frame.height() * 0.2);
        const QPointF end = frame.topLeft() + QPointF(frame.width() * 0.7, frame.height() * 0.7);
        QMouseEvent press(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &press);
        QApplication::sendEvent(canvas, &move);
        QApplication::sendEvent(canvas, &release);
        require(!canvas->history.isClean(), "OD fixture did not create a pending edit");
    };
    studio::set_annotation_auto_save(false);
    draw();
    require(!decide(QMessageBox::Cancel, [&] { return workspace.confirm_annotation_transition(); })
        && !canvas->history.isClean(), "OD cancellation bypassed the shared lifecycle");
    require(decide(QMessageBox::Discard, [&] { return workspace.confirm_annotation_transition(); })
        && canvas->history.isClean() && canvas->boxes().empty(), "OD discard retained draft boxes");
    draw();
    studio::set_annotation_auto_save(true);
    workspace.activate_step(studio::TaskWorkspace::Step::Data);
    require(workspace.active_step() == studio::TaskWorkspace::Step::Data && canvas->history.isClean()
        && od_service.binding().samples.front().bbox_comps.size() == 1,
        "OD page transition failed to apply the configured automatic save");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
