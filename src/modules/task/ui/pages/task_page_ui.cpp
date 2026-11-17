#include "modules/task/ui/pages/task_page_ui.h"
#include "common/ui/ui_metrics.h"
#include "modules/workflow/model/workflow.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>

#include <algorithm>

namespace studio {

namespace {

class CollapsibleSplitterHandle final : public QSplitterHandle {
public:
    CollapsibleSplitterHandle(Qt::Orientation orientation, QSplitter* parent)
        : QSplitterHandle(orientation, parent) {}

protected:
    void mousePressEvent(QMouseEvent* event) override {
        press_global_ = event->globalPos();
        QSplitterHandle::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        const bool click = (event->globalPos() - press_global_).manhattanLength() <= 4;
        QSplitterHandle::mouseReleaseEvent(event);
        if (click) toggle_side_panel();
    }

    void paintEvent(QPaintEvent* event) override {
        QSplitterHandle::paintEvent(event);
        auto* owner = splitter();
        if (!owner || orientation() != Qt::Horizontal) return;

        int handle_index = -1;
        for (int index = 1; index < owner->count(); ++index) {
            if (owner->handle(index) == this) {
                handle_index = index;
                break;
            }
        }
        if (handle_index < 0) return;

        int target_index = property("collapsePanelIndex").toInt();
        if (target_index < 0 || target_index >= owner->count())
            target_index = handle_index == 1 ? 0 : owner->count() - 1;
        const bool left_panel = target_index < handle_index;
        const auto sizes = owner->sizes();
        const bool collapsed = target_index < sizes.size() && sizes[target_index] <= 1;
        const bool point_left = left_panel ? !collapsed : collapsed;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::Highlight));
        const QPoint center = rect().center();
        QPolygon triangle;
        if (point_left) {
            triangle << QPoint(center.x() - 3, center.y())
                     << QPoint(center.x() + 2, center.y() - 5)
                     << QPoint(center.x() + 2, center.y() + 5);
        } else {
            triangle << QPoint(center.x() + 3, center.y())
                     << QPoint(center.x() - 2, center.y() - 5)
                     << QPoint(center.x() - 2, center.y() + 5);
        }
        painter.drawPolygon(triangle);
    }

private:
    void toggle_side_panel() {
        auto* owner = splitter();
        if (!owner || owner->count() < 2) return;

        int handle_index = -1;
        for (int index = 1; index < owner->count(); ++index) {
            if (owner->handle(index) == this) {
                handle_index = index;
                break;
            }
        }
        if (handle_index < 0) return;

        int target_index = property("collapsePanelIndex").toInt();
        if (target_index < 0 || target_index >= owner->count())
            target_index = handle_index == 1 ? 0 : owner->count() - 1;
        auto sizes = owner->sizes();
        if (target_index >= sizes.size()) return;

        const int default_width = std::max(1, property("restoreWidth").toInt());
        if (sizes[target_index] > 1) {
            setProperty("restoreWidth", sizes[target_index]);
            sizes[target_index] = 0;
        } else {
            sizes[target_index] = default_width;
        }
        owner->setSizes(sizes);
        update();
    }

    QPoint press_global_;
};

} // namespace

CollapsibleSplitter::CollapsibleSplitter(Qt::Orientation orientation, QWidget* parent)
    : QSplitter(orientation, parent) {}

QSplitterHandle* CollapsibleSplitter::createHandle() {
    return new CollapsibleSplitterHandle(orientation(), this);
}

namespace task_page {

QLabel* helper_label(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setProperty("meta", true);
    label->setWordWrap(true);
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return label;
}

QWidget* metric_card(const QString& caption, QLabel*& value, QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setProperty("metricCard", true);
    auto* layout = new QHBoxLayout(card);
    layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                               ThemeTokens::Space2, ThemeTokens::Space1);
    layout->setSpacing(ThemeTokens::Space1);
    auto* name = new QLabel(caption, card);
    name->setProperty("eyebrow", true);
    value = new QLabel("0", card);
    value->setProperty("metricValue", true);
    layout->addWidget(name);
    layout->addStretch();
    layout->addWidget(value);
    return card;
}

QString split_label(const std::string& value) {
    if (value == "train") return QObject::tr("Train");
    if (value == "validation") return QObject::tr("Validation");
    if (value == "test") return QObject::tr("Test");
    return QObject::tr("Unassigned");
}

QString annotation_label(const TaskSampleBinding& sample) {
    if (sample.annotation_state == "annotated") return QObject::tr("Annotated");
    return QObject::tr("Not annotated");
}

QString weight_type_label(jocelin::config::WeightType type) {
    switch (type) {
    case jocelin::config::WeightType::LibTorch: return QStringLiteral("LibTorch");
    case jocelin::config::WeightType::Onnx: return QStringLiteral("ONNX");
    case jocelin::config::WeightType::OpenVino: return QStringLiteral("OpenVINO");
    case jocelin::config::WeightType::TensorRt: return QStringLiteral("TensorRT");
    }
    return QStringLiteral("—");
}

void set_runtime_state(QLabel* label, const QString& value) {
    if (!label) return;
    QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty()) normalized = QStringLiteral("idle");
    QString display = normalized;
    display[0] = display[0].toUpper();
    label->setText(display);
    label->setProperty("runtimeState", normalized);
    label->style()->unpolish(label);
    label->style()->polish(label);
}

} // namespace task_page

} // namespace studio
