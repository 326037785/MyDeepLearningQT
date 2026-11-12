#include "task_workspace_pages.h"
#include "result_presentation.h"
#include "classification_annotation_state.h"

#include "augmentation_dialog.h"

#include "annotation_exchange.h"
#include "classification_annotation.h"
#include "icon_provider.h"
#include "image_result_canvas.h"
#include "inference_filter_panel.h"
#include "model_presentation.h"
#include "model_spec_catalog.h"
#include "sample_presentation.h"
#include "sample_browser.h"
#include "project_model_browser.h"
#include "resource_monitor.h"
#include "task_dataset_binding.h"
#include "task_inference_filter.h"
#include "ui_metrics.h"
#include "weight_conversion_popup.h"
#include "model_weight_assets.h"

#include <checkpoint/checkpoint_catalog.h>
#include <common/config/ai_config.h>
#include <common/geometry/interval.h>
#include <runtime/inference_route.h>

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImageReader>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>
#include <type_traits>

namespace studio {

class LossChart final : public QWidget {
public:
    explicit LossChart(QWidget* parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void reset() {
        loss_values_.clear();
        metric_values_.clear();
        metric_title_.clear();
        update();
    }

    void add_point(double loss, double metric, const QString& metric_title) {
        loss_values_.push_back(loss);
        metric_values_.push_back(metric);
        metric_title_ = metric_title;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#F8FAFA"));
        const QRectF plot = rect().adjusted(52, 34, -52, -28);
        painter.setPen(QColor("#173F46"));
        QFont title_font = painter.font();
        title_font.setBold(true);
        painter.setFont(title_font);
        painter.drawText(QRectF(12, 5, width() - 24, 18), Qt::AlignLeft,
                         tr("Training trend"));
        painter.setFont(font());
        painter.setPen(QPen(QColor("#D7E0E0"), 1));
        painter.drawRect(plot);
        for (int line = 1; line < 4; ++line) {
            const qreal y = plot.top() + plot.height() * line / 4.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        if (loss_values_.empty()) return;

        const auto value_range = [](const std::vector<double>& values) {
            const auto range = std::minmax_element(values.begin(), values.end());
            double minimum = *range.first;
            double maximum = *range.second;
            if (std::abs(maximum - minimum) < 1e-12) {
                const double pad = std::max(1e-6, std::abs(maximum) * 0.1);
                minimum -= pad;
                maximum += pad;
            }
            return jocelin::geo::Interval{minimum, maximum};
        };
        const auto loss_range = value_range(loss_values_);
        const auto metric_range = value_range(metric_values_);
        const auto draw_series = [&](const std::vector<double>& values,
                                     const jocelin::geo::Interval& range,
                                     const QPen& pen) {
            const double span = std::max(1e-12, range.high - range.low);
            QPainterPath path;
            for (size_t index = 0; index < values.size(); ++index) {
                const qreal x = values.size() == 1
                    ? plot.left()
                    : plot.left() + plot.width() * static_cast<qreal>(index) /
                        static_cast<qreal>(values.size() - 1);
                const qreal normalized = std::clamp(
                    (values[index] - range.low) / span, 0.0, 1.0);
                const qreal y = plot.bottom() - plot.height() * normalized;
                if (index == 0) path.moveTo(x, y);
                else path.lineTo(x, y);
            }
            painter.setPen(pen);
            painter.drawPath(path);
        };

        draw_series(loss_values_, loss_range, QPen(QColor("#27879B"), 2));
        QPen metric_pen(QColor("#A74720"), 2);
        metric_pen.setStyle(Qt::DashLine);
        draw_series(metric_values_, metric_range, metric_pen);

        painter.setPen(QColor("#718386"));
        painter.drawText(QRectF(4, plot.top() - 7, 44, 16), Qt::AlignRight,
                         QString::number(loss_range.high, 'g', 4));
        painter.drawText(QRectF(4, plot.bottom() - 8, 44, 16), Qt::AlignRight,
                         QString::number(loss_range.low, 'g', 4));
        painter.drawText(QRectF(plot.right() + 5, plot.top() - 7, 44, 16), Qt::AlignLeft,
                         QString::number(metric_range.high, 'g', 4));
        painter.drawText(QRectF(plot.right() + 5, plot.bottom() - 8, 44, 16), Qt::AlignLeft,
                         QString::number(metric_range.low, 'g', 4));
        painter.setPen(QColor("#27879B"));
        painter.drawText(QRectF(plot.left(), 5, 80, 18), Qt::AlignRight, tr("Loss"));
        painter.setPen(QColor("#A74720"));
        painter.drawText(QRectF(plot.left() + 88, 5, plot.width() - 88, 18), Qt::AlignLeft,
                         metric_title_.isEmpty() ? tr("Metric") : metric_title_);
    }

private:
    std::vector<double> loss_values_;
    std::vector<double> metric_values_;
    QString metric_title_;
};

class CompactDoubleSpinBox final : public QDoubleSpinBox {
public:
    explicit CompactDoubleSpinBox(QWidget* parent = nullptr)
        : QDoubleSpinBox(parent) {}

protected:
    QString textFromValue(double value) const override {
        QString text = QString::number(value, 'f', decimals());
        while (text.contains('.') && text.endsWith('0')) text.chop(1);
        if (text.endsWith('.')) text.chop(1);
        return text;
    }
};

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

class CollapsibleSplitter final : public QSplitter {
public:
    explicit CollapsibleSplitter(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QSplitter(orientation, parent) {}

protected:
    QSplitterHandle* createHandle() override {
        return new CollapsibleSplitterHandle(orientation(), this);
    }
};

namespace {

QLabel* title_label(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setProperty("sectionTitle", true);
    return label;
}

QLabel* helper_label(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setProperty("meta", true);
    label->setWordWrap(true);
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return label;
}

QFrame* divider(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setProperty("sectionDivider", true);
    return line;
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

QWidget* path_editor(QWidget* parent,
                     QLineEdit*& edit,
                     const QString& object_name,
                     const QString& button_name,
                     std::function<QString()> choose,
                     std::function<void(const QString&)> commit) {
    auto* row = new QWidget(parent);
    row->setMinimumWidth(0);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeTokens::Space1);
    edit = new QLineEdit(row);
    edit->setObjectName(object_name);
    edit->setMinimumWidth(0);
    edit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto* browse = new QToolButton(row);
    browse->setObjectName(button_name);
    IconProvider::configure(browse, "folder", QObject::tr("Browse"));
    layout->addWidget(edit, 1);
    layout->addWidget(browse);
    QObject::connect(edit, &QLineEdit::editingFinished, row,
                     [edit, commit] { commit(edit->text().trimmed()); });
    QObject::connect(browse, &QToolButton::clicked, row, [edit, choose, commit] {
        const QString selected = choose();
        if (selected.isEmpty()) return;
        edit->setText(selected);
        commit(selected);
    });
    return row;
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

QString device_type_label(jocelin::config::DeviceType type) {
    switch (type) {
    case jocelin::config::DeviceType::Cpu: return QObject::tr("CPU");
    case jocelin::config::DeviceType::Gpu: return QObject::tr("GPU");
    case jocelin::config::DeviceType::Other: return QObject::tr("Other");
    }
    return QObject::tr("Other");
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

QString unique_destination(const QDir& directory, const QString& file_name) {
    const QFileInfo info(file_name);
    QString destination = directory.filePath(info.fileName());
    for (int suffix = 2; QFileInfo::exists(destination); ++suffix)
        destination = directory.filePath(
            info.completeBaseName() + "-" + QString::number(suffix) +
            (info.suffix().isEmpty() ? QString{} : "." + info.suffix()));
    return destination;
}

} // namespace

DatasetSelectionView::DatasetSelectionView(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                             ThemeTokens::Space2, ThemeTokens::Space2);
    root->setSpacing(ThemeTokens::Space1);

    auto* exchange = new QFrame(this);
    exchange->setObjectName("taskAnnotationExchange");
    exchange->setProperty("sectionPanel", true);
    auto* exchange_layout = new QHBoxLayout(exchange);
    exchange_layout->setContentsMargins(
        ThemeTokens::Space2, ThemeTokens::Space1,
        ThemeTokens::Space2, ThemeTokens::Space1);
    exchange_layout->setSpacing(ThemeTokens::Space1);

    auto* open_dataset = new QPushButton(tr("Dataset…"), exchange);
    open_dataset->setObjectName("taskOpenDatasetButton");
    open_dataset->setProperty("compact", true);
    open_dataset->setToolTip(tr("Open the connected source Dataset"));
    auto* exchange_label = new QLabel(tr("Annotations"), exchange);
    exchange_label->setProperty("eyebrow", true);
    auto* import_annotations = new QPushButton(tr("Import…"), exchange);
    import_annotations->setObjectName("taskAnnotationImportButton");
    import_annotations->setProperty("compact", true);
    import_annotations->setToolTip(
        tr("Import a Studio bundle or an images/masks directory."));
    auto* export_format = new QComboBox(exchange);
    export_format->setObjectName("taskAnnotationExportFormat");
    export_format->setToolTip(tr("Annotation export format"));
    export_format->addItem(
        tr("Studio bundle"),
        static_cast<int>(AnnotationExchangeFormat::StudioNative));
    auto* export_annotations = new QPushButton(tr("Export…"), exchange);
    export_annotations->setObjectName("taskAnnotationExportButton");
    export_annotations->setProperty("compact", true);
    exchange_layout->addWidget(open_dataset);
    exchange_layout->addSpacing(ThemeTokens::Space2);
    exchange_layout->addWidget(exchange_label);
    exchange_layout->addWidget(import_annotations);
    exchange_layout->addWidget(export_format);
    exchange_layout->addWidget(export_annotations);
    exchange_layout->addStretch();
    root->addWidget(exchange);

    auto* metrics = new QGridLayout;
    metrics->setSpacing(ThemeTokens::Space2);
    metrics->addWidget(metric_card(tr("SAMPLES"), sample_value_, this), 0, 0);
    metrics->addWidget(metric_card(tr("IMAGES"), image_value_, this), 0, 1);
    metrics->addWidget(metric_card(tr("SELECTED"), selected_value_, this), 0, 2);
    metrics->addWidget(metric_card(tr("NEEDS REVIEW"), quality_value_, this), 0, 3);
    for (int column = 0; column < 4; ++column) metrics->setColumnStretch(column, 1);
    root->addLayout(metrics);

    issue_banner_ = helper_label({}, this);
    issue_banner_->setObjectName("taskDataIssueBanner");
    issue_banner_->setProperty("inlineWarning", true);
    issue_banner_->hide();
    root->addWidget(issue_banner_);

    auto* filters = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setObjectName("taskDataSearch");
    search_->setPlaceholderText(tr("Search sample id, file, split, or issue"));
    filter_ = new QComboBox(this);
    filter_->setObjectName("taskDataFilter");
    filter_->addItem(tr("All samples"), "all");
    filter_->addItem(tr("Selected"), "selected");
    filter_->addItem(tr("Annotated"), "annotated");
    filter_->addItem(tr("Needs review"), "issues");
    filters->addWidget(search_, 1);
    filters->addWidget(filter_);
    root->addLayout(filters);

    table_ = new QTableWidget(this);
    table_->setObjectName("taskDataTable");
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels(
        {tr("Use"), tr("Sample"), tr("Images"), tr("Split"), tr("Annotation"), tr("Issues")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    root->addWidget(table_, 1);

    connect(search_, &QLineEdit::textChanged, this, [this] { apply_filter(); });
    connect(filter_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { apply_filter(); });
    connect(open_dataset, &QPushButton::clicked, this, [this] {
        if (service_ && open_dataset_requested)
            open_dataset_requested(service_->source_node_id());
    });
    connect(
        import_annotations, &QPushButton::clicked,
        this, [this] {
            if (!service_) return;
            const QString source = QFileDialog::getExistingDirectory(
                this, tr("Select annotation package"));
            if (source.isEmpty()) return;
            try {
                auto imported =
                    AnnotationExchange::import_auto(source);
                const auto stats =
                    AnnotationExchange::import_stats(imported);
                if (!dataset_replace_requested)
                    throw std::runtime_error("Dataset replacement handler unavailable");
                if (!dataset_replace_requested(service_->source_node_id(), std::move(imported.dataset),
                                               service_->node_id(), &imported.binding)) return;
                refresh();
                if (changed) changed();
                QString summary =
                    tr("%1 images · %2 masks · %3 unannotated · %4 classes")
                        .arg(stats.image_count)
                        .arg(stats.mask_count)
                        .arg(stats.unannotated_count)
                        .arg(stats.class_count);
                if (stats.review_count > 0)
                    summary += tr("\n%1 item(s) need review.")
                                   .arg(stats.review_count);
                QMessageBox::information(
                    this,
                    tr("Annotations imported"),
                    summary);
            } catch (const std::exception& error) {
                QMessageBox::critical(
                    this,
                    tr("Cannot import annotations"),
                    QString::fromUtf8(error.what()));
            }
        });
    connect(
        export_annotations, &QPushButton::clicked,
        this, [this, export_format] {
            if (!service_) return;
            const QString destination =
                QFileDialog::getExistingDirectory(
                    this,
                    tr("Select annotation export destination"));
            if (destination.isEmpty()) return;
            try {
                const auto format =
                    static_cast<AnnotationExchangeFormat>(
                        export_format->currentData().toInt());
                const QString manifest =
                    AnnotationExchange::export_task(
                        service_->binding(),
                        destination,
                        format);
                QMessageBox::information(
                    this,
                    tr("Annotations exported"),
                    tr("%1 written to %2")
                        .arg(
                            AnnotationExchange::format_name(format),
                            manifest));
            } catch (const std::exception& error) {
                QMessageBox::critical(
                    this,
                    tr("Cannot export annotations"),
                    QString::fromUtf8(error.what()));
            }
        });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (rebuilding_ || !service_ || !item || item->column() != 0) return;
        const QString sample_id = table_->item(item->row(), 1)->data(Qt::UserRole).toString();
        try {
            service_->set_sample_selected(
                sample_id.toStdString(), item->checkState() == Qt::Checked);
            if (changed) changed();
            refresh();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Selection failed"), QString::fromUtf8(error.what()));
        }
    });
    UiMetrics::apply_semantic_metrics(this);
}

void DatasetSelectionView::set_service(TaskWorkspaceService* service) {
    service_ = service;
    refresh();
}

void DatasetSelectionView::refresh() {
    rebuilding_ = true;
    table_->setRowCount(0);
    if (!service_) {
        sample_value_->setText("0");
        image_value_->setText("0");
        selected_value_->setText("0");
        quality_value_->setText("0");
        rebuilding_ = false;
        return;
    }
    try {
        service_->refresh_quality();
        const auto stats = service_->dataset_stats();
        sample_value_->setText(QString::number(stats.samples));
        image_value_->setText(QString::number(stats.images));
        selected_value_->setText(tr("%1 / %2").arg(stats.selected).arg(stats.samples));
        quality_value_->setText(QString::number(stats.quality_issues));
        issue_banner_->setVisible(stats.invalid > 0 || stats.quality_issues > 0);
        issue_banner_->setText(tr("%1 invalid sample(s), %2 sample(s) need annotation review. "
                                  "Bucket capacity is %3 image(s) per sample.")
                                   .arg(stats.invalid).arg(stats.quality_issues)
                                   .arg(stats.bucket_capacity));
        const auto& samples = service_->binding().samples;
        table_->setRowCount(static_cast<int>(samples.size()));
        for (int row = 0; row < static_cast<int>(samples.size()); ++row) {
            const auto& sample = samples[static_cast<size_t>(row)];
            const QString display_name = image_display_name(row, sample);
            const QString annotation_text = annotation_label(sample);
            const QString tooltip = sample_image_tooltip(
                display_name, sample, annotation_text);
            auto* use = new QTableWidgetItem;
            use->setFlags((use->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
            use->setCheckState(sample.selected ? Qt::Checked : Qt::Unchecked);
            auto* id = new QTableWidgetItem(display_name);
            id->setData(Qt::UserRole, QString::fromStdString(sample.sample_id));
            id->setToolTip(tooltip);
            auto* images = new QTableWidgetItem(QString::number(sample.image_paths.size()));
            images->setToolTip(tooltip);
            auto* split = new QTableWidgetItem(split_label(normalized_split(sample.split)));
            auto* annotation = new QTableWidgetItem(annotation_text);
            QStringList issues;
            for (const auto& issue : sample.quality_issues)
                issues.push_back(QString::fromStdString(issue));
            auto* issue = new QTableWidgetItem(issues.join("; "));
            issue->setToolTip(issues.join("\n"));
            for (auto* value : {id, images, split, annotation, issue})
                value->setFlags(value->flags() & ~Qt::ItemIsEditable);
            table_->setItem(row, 0, use);
            table_->setItem(row, 1, id);
            table_->setItem(row, 2, images);
            table_->setItem(row, 3, split);
            table_->setItem(row, 4, annotation);
            table_->setItem(row, 5, issue);
        }
    } catch (const std::exception& error) {
        issue_banner_->setText(QString::fromUtf8(error.what()));
        issue_banner_->show();
    }
    rebuilding_ = false;
    apply_filter();
}

void DatasetSelectionView::apply_filter() {
    if (!table_) return;
    const QString needle = search_->text().trimmed();
    const QString mode = filter_->currentData().toString();
    for (int row = 0; row < table_->rowCount(); ++row) {
        QString haystack;
        for (int column = 1; column < table_->columnCount(); ++column)
            if (table_->item(row, column)) haystack += " " + table_->item(row, column)->text();
        if (table_->item(row, 1))
            haystack += " " + table_->item(row, 1)->data(Qt::UserRole).toString();
        const bool text_match = needle.isEmpty() ||
            haystack.contains(needle, Qt::CaseInsensitive);
        const bool selected = table_->item(row, 0)->checkState() == Qt::Checked;
        const QString annotation = table_->item(row, 4)->text();
        const bool has_issue = !table_->item(row, 5)->text().isEmpty();
        const bool mode_match = mode == "all" ||
            (mode == "selected" && selected) ||
            (mode == "annotated" &&
             annotation == tr("Annotated")) ||
            (mode == "issues" && has_issue);
        table_->setRowHidden(row, !text_match || !mode_match);
    }
}

DatasetSplitView::DatasetSplitView(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                             ThemeTokens::Space2, ThemeTokens::Space2);
    root->setSpacing(ThemeTokens::Space1);

    auto* metrics = new QGridLayout;
    metrics->setSpacing(ThemeTokens::Space2);
    metrics->addWidget(metric_card(tr("TRAIN"), train_value_, this), 0, 0);
    metrics->addWidget(metric_card(tr("VALIDATION"), validation_value_, this), 0, 1);
    metrics->addWidget(metric_card(tr("TEST"), test_value_, this), 0, 2);
    metrics->addWidget(metric_card(tr("UNASSIGNED"), unassigned_value_, this), 0, 3);
    for (int column = 0; column < 4; ++column) metrics->setColumnStretch(column, 1);
    root->addLayout(metrics);

    auto* generator = new QFrame(this);
    generator->setProperty("sectionPanel", true);
    auto* generator_layout = new QHBoxLayout(generator);
    generator_layout->setContentsMargins(ThemeTokens::Space3, ThemeTokens::Space2,
                                         ThemeTokens::Space3, ThemeTokens::Space2);
    train_percent_ = new QSpinBox(generator);
    train_percent_->setObjectName("splitTrainPercent");
    validation_percent_ = new QSpinBox(generator);
    validation_percent_->setObjectName("splitValidationPercent");
    test_percent_ = new QSpinBox(generator);
    test_percent_->setObjectName("splitTestPercent");
    seed_ = new QSpinBox(generator);
    seed_->setObjectName("splitRandomSeed");
    for (auto* value : {train_percent_, validation_percent_, test_percent_}) {
        value->setRange(0, 100);
        value->setSuffix("%");
    }
    train_percent_->setValue(80);
    validation_percent_->setValue(20);
    test_percent_->setValue(0);
    seed_->setRange(0, 999999999);
    seed_->setValue(42);
    auto* apply = new QPushButton(tr("Apply smart split"), generator);
    apply->setObjectName("applySmartSplitButton");
    apply->setProperty("primary", true);
    auto* reset = new QPushButton(tr("Reset manual overrides"), generator);
    reset->setObjectName("resetSplitOverridesButton");
    reset->setProperty("compact", true);
    generator_layout->addWidget(new QLabel(tr("Train"), generator));
    generator_layout->addWidget(train_percent_);
    generator_layout->addWidget(new QLabel(tr("Validation"), generator));
    generator_layout->addWidget(validation_percent_);
    generator_layout->addWidget(new QLabel(tr("Test"), generator));
    generator_layout->addWidget(test_percent_);
    generator_layout->addWidget(new QLabel(tr("Seed"), generator));
    generator_layout->addWidget(seed_);
    generator_layout->addStretch();
    generator_layout->addWidget(reset);
    generator_layout->addWidget(apply);
    root->addWidget(generator);

    warning_ = new QLabel(this);
    warning_->setObjectName("splitAnnotationWarning");
    warning_->setProperty("inlineWarning", true);
    warning_->setWordWrap(true);
    root->addWidget(warning_);

    auto* content = new QSplitter(Qt::Horizontal, this);
    content->setObjectName("splitContentSplitter");
    auto* thumbnail_panel = new QFrame(content);
    thumbnail_panel->setProperty("sectionPanel", true);
    auto* thumbnail_layout = new QVBoxLayout(thumbnail_panel);
    thumbnail_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                                         ThemeTokens::Space2, ThemeTokens::Space2);
    auto* thumbnail_title = new QLabel(tr("SAMPLES"), thumbnail_panel);
    thumbnail_title->setProperty("eyebrow", true);
    thumbnail_layout->addWidget(thumbnail_title);
    thumbnails_ = new SampleBrowser(thumbnail_panel);
    thumbnails_->setObjectName("splitThumbnailList");
    thumbnails_->setIconSize(QSize(72, 54));
    thumbnail_layout->addWidget(thumbnails_, 1);

    table_ = new QTableWidget(content);
    table_->setObjectName("taskSplitTable");
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        {tr("Sample"), tr("Images"), tr("Annotation"), tr("Assignment"), tr("Source")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    content->addWidget(thumbnail_panel);
    content->addWidget(table_);
    content->setCollapsible(0, false);
    content->setStretchFactor(0, 0);
    content->setStretchFactor(1, 1);
    content->setSizes({240, 900});
    root->addWidget(content, 1);
    connect(apply, &QPushButton::clicked, this, [this] { apply_random_split(); });
    connect(reset, &QPushButton::clicked, this, [this] {
        if (!service_) return;
        const auto settings = service_->binding().split_settings;
        service_->assign_reproducible_split(
            settings.train_percent, settings.validation_percent,
            settings.test_percent, settings.seed, true);
        refresh();
        if (changed) changed();
        if (status) status(tr("Manual split overrides were reset and the smart split was recalculated."));
    });
    thumbnails_->current_requested = [this](int row, const QString&) {
        if (row >= 0 && row < table_->rowCount()) table_->selectRow(row);
        return true;
    };
    connect(table_, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
        if (row >= 0 && row < thumbnails_->count())
            thumbnails_->set_current_source_index(row);
    });
    thumbnails_->split_requested = [this](const std::vector<std::string>& ids,
                                           const std::string& value) {
        if (!service_) return;
        for (const auto& id : ids) service_->set_sample_split(id, value);
        refresh();
        if (changed) changed();
    };
    UiMetrics::apply_semantic_metrics(this);
}

void DatasetSplitView::set_service(TaskWorkspaceService* service) {
    service_ = service;
    refresh();
}

void DatasetSplitView::refresh() {
    rebuilding_ = true;
    table_->setRowCount(0);
    std::vector<SampleBrowserEntry> browser_entries;
    if (!service_) {
        thumbnails_->set_entries(browser_entries);
        rebuilding_ = false;
        refresh_stats();
        return;
    }
    service_->refresh_quality();
    const auto split_settings = service_->binding().split_settings;
    train_percent_->setValue(split_settings.train_percent);
    validation_percent_->setValue(split_settings.validation_percent);
    test_percent_->setValue(split_settings.test_percent);
    seed_->setValue(static_cast<int>(split_settings.seed));
    const auto& samples = service_->binding().samples;
    int selected_rows = 0;
    int malformed = 0;
    for (const auto& sample : samples) {
        if (!sample.selected) continue;
        ++selected_rows;
        malformed += sample.quality_issues.empty() ? 0 : 1;
    }
    table_->setRowCount(selected_rows);
    int row = 0;
    for (int sample_index = 0;
         sample_index < static_cast<int>(samples.size());
         ++sample_index) {
        const auto& sample = samples[static_cast<size_t>(sample_index)];
        if (!sample.selected) continue;
        const QString display_name = image_display_name(sample_index, sample);
        const QString annotation_text = annotation_label(sample);
        const QString tooltip = sample_image_tooltip(
            display_name, sample, annotation_text);
        SampleBrowserEntry browser_entry;
        browser_entry.sample_id = sample.sample_id;
        browser_entry.image_path = sample.image_paths.empty()
            ? QString{}
            : QString::fromStdString(sample.image_paths.front());
        browser_entry.display_name = display_name;
        browser_entry.annotation_state = annotation_text;
        browser_entry.split_state = split_label(normalized_split(sample.split));
        browser_entry.tooltip = tooltip;
        browser_entry.source_index = row;
        browser_entries.push_back(std::move(browser_entry));
        auto* id = new QTableWidgetItem(display_name);
        id->setData(Qt::UserRole, QString::fromStdString(sample.sample_id));
        id->setToolTip(tooltip);
        auto* count = new QTableWidgetItem(QString::number(sample.image_paths.size()));
        count->setToolTip(tooltip);
        auto* annotation = new QTableWidgetItem(annotation_text);
        for (auto* item : {id, count, annotation})
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 0, id);
        table_->setItem(row, 1, count);
        table_->setItem(row, 2, annotation);
        auto* assignment = new QComboBox(table_);
        assignment->setProperty("compact", true);
        assignment->addItem(
            tr("Automatic · %1").arg(split_label(normalized_split(sample.split))),
            "auto");
        assignment->addItem(tr("Train"), "train");
        assignment->addItem(tr("Validation"), "validation");
        assignment->addItem(tr("Test"), "test");
        assignment->addItem(tr("Unassigned"), "unassigned");
        const int current = sample.split_manual
            ? assignment->findData(
                  QString::fromStdString(normalized_split(sample.split)))
            : 0;
        assignment->setCurrentIndex(std::max(0, current));
        const std::string sample_id = sample.sample_id;
        connect(assignment, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this, assignment, sample_id](int) {
            if (rebuilding_ || !service_) return;
            const auto selected = assignment->currentData().toString();
            if (selected == "auto")
                service_->restore_automatic_split(sample_id);
            else
                service_->set_sample_split(
                    sample_id, selected.toStdString());
            refresh_stats();
            if (changed) changed();
            QTimer::singleShot(0, this, [this] { refresh(); });
        });
        table_->setCellWidget(row, 3, assignment);
        auto* source = new QTableWidgetItem(
            sample.split_manual ? tr("Manual") : tr("Automatic"));
        source->setFlags(source->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 4, source);
        ++row;
    }
    thumbnails_->set_entries(browser_entries);
    warning_->setVisible(malformed > 0);
    warning_->setText(
        tr("%1 sample(s) have quality issues that must be fixed or deselected before training.")
            .arg(malformed));
    rebuilding_ = false;
    refresh_stats();
}

void DatasetSplitView::refresh_stats() {
    const auto stats = service_ ? service_->split_stats() : TaskSplitStats{};
    train_value_->setText(QString::number(stats.train));
    validation_value_->setText(
        stats.derived_validation > 0
            ? tr("%1 (+%2 derived/shared)")
                  .arg(stats.validation)
                  .arg(stats.derived_validation)
            : QString::number(stats.validation));
    validation_value_->setToolTip(
        stats.derived_validation > 0
            ? tr("No validation split was explicitly assigned. %1 training sample(s) will also be evaluated as a deterministic derived validation set.")
                  .arg(stats.shared_validation)
            : tr("Explicit validation assignments"));
    test_value_->setText(QString::number(stats.test));
    unassigned_value_->setText(QString::number(stats.unassigned));
}

void DatasetSplitView::apply_random_split() {
    if (!service_) return;
    try {
        service_->assign_reproducible_split(
            train_percent_->value(), validation_percent_->value(),
            test_percent_->value(), static_cast<std::uint32_t>(seed_->value()));
        refresh();
        if (changed) changed();
        if (status) status(
            tr("Stratified split applied with seed %1; manual overrides were preserved.")
                .arg(seed_->value()));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Cannot apply split"), QString::fromUtf8(error.what()));
    }
}

TrainingWorkspace::TrainingWorkspace(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                             ThemeTokens::Space2, ThemeTokens::Space2);
    root->setSpacing(ThemeTokens::Space1);
    auto* split = new CollapsibleSplitter(Qt::Horizontal, this);
    workspace_splitter_ = split;
    split->setObjectName("trainingWorkspaceSplitter");
    workspace_scroll_ = new QScrollArea(split);
    workspace_scroll_->setObjectName("trainingSettingsScrollArea");
    workspace_scroll_->setWidgetResizable(true);
    workspace_scroll_->setFrameShape(QFrame::NoFrame);
    workspace_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    workspace_scroll_->setMinimumWidth(220);
    auto* settings = new QWidget;
    settings_panel_ = settings;
    settings->setObjectName("trainingSettingsPanel");
    settings->setMinimumWidth(220);
    workspace_scroll_->setWidget(settings);
    auto* settings_layout = new QVBoxLayout(settings);
    settings_layout->setContentsMargins(0, 0, ThemeTokens::Space2, 0);
    settings_layout->setSpacing(ThemeTokens::Space1);

    const auto field_label = [](const QString& text, QWidget* parent) {
        auto* label = new QLabel(text, parent);
        label->setProperty("configFieldLabel", true);
        return label;
    };
    const int inspector_control_height = UiMetrics::for_widget(this).control_height;
    const auto compact_control = [inspector_control_height](QWidget* control) {
        control->setMinimumHeight(inspector_control_height);
        control->setMaximumHeight(inspector_control_height);
    };

    auto* configuration_heading = new QLabel(tr("CONFIGURATION"), settings);
    configuration_heading->setProperty("eyebrow", true);
    settings_layout->addWidget(configuration_heading);

    auto* model_section = new QFrame(settings);
    model_section->setObjectName("trainingModelConfigSection");
    model_section->setFrameShape(QFrame::NoFrame);
    auto* model_grid = new QGridLayout(model_section);
    model_grid->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                                   ThemeTokens::Space2, ThemeTokens::Space1);
    model_grid->setHorizontalSpacing(ThemeTokens::Space2);
    model_grid->setVerticalSpacing(ThemeTokens::Space1);
    model_scale_ = new QComboBox(settings);
    model_scale_->setObjectName("modelScaleCombo");
    model_index_ = new QComboBox(settings);
    model_index_->setObjectName("modelIndexCombo");
    compact_control(model_scale_);
    compact_control(model_index_);
    model_grid->addWidget(field_label(tr("Capacity"), model_section), 0, 0);
    model_grid->addWidget(field_label(tr("Network"), model_section), 0, 1);
    model_grid->addWidget(model_scale_, 1, 0);
    model_grid->addWidget(model_index_, 1, 1);
    model_grid->setColumnStretch(0, 1);
    model_grid->setColumnStretch(1, 1);
    settings_layout->addWidget(model_section);
    settings_layout->addWidget(divider(settings));

    auto* run_section = new QFrame(settings);
    run_section->setObjectName("trainingRunConfigSection");
    run_section->setFrameShape(QFrame::NoFrame);
    auto* run_grid = new QGridLayout(run_section);
    run_grid->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                                 ThemeTokens::Space2, ThemeTokens::Space1);
    run_grid->setHorizontalSpacing(ThemeTokens::Space2);
    run_grid->setVerticalSpacing(ThemeTokens::Space1);
    auto* image_size = new QWidget(run_section);
    auto* image_size_layout = new QHBoxLayout(image_size);
    image_size_layout->setContentsMargins(0, 0, 0, 0);
    image_size_layout->setSpacing(ThemeTokens::Space1);
    image_width_ = new QSpinBox(image_size);
    image_height_ = new QSpinBox(image_size);
    image_width_->setObjectName("trainingImageWidth");
    image_height_->setObjectName("trainingImageHeight");
    for (auto* value : {image_width_, image_height_}) {
        value->setRange(64, 8192);
        value->setSingleStep(64);
        value->setButtonSymbols(QAbstractSpinBox::NoButtons);
        compact_control(value);
    }
    auto* size_separator = new QLabel("×", image_size);
    size_separator->setProperty("configFieldLabel", true);
    size_separator->setAlignment(Qt::AlignCenter);
    image_size_layout->addWidget(image_width_);
    image_size_layout->addWidget(size_separator);
    image_size_layout->addWidget(image_height_);
    batch_size_ = new QSpinBox(run_section);
    batch_size_->setObjectName("trainingBatchSize");
    batch_size_->setRange(1, 1024);
    batch_size_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    compact_control(batch_size_);
    epochs_ = new QSpinBox(run_section);
    epochs_->setObjectName("trainingEpochs");
    epochs_->setRange(1, 100000);
    epochs_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    compact_control(epochs_);
    learning_rate_ = new CompactDoubleSpinBox(run_section);
    learning_rate_->setObjectName("trainingLearningRate");
    learning_rate_->setDecimals(8);
    learning_rate_->setRange(0.00000001, 10.0);
    learning_rate_->setSingleStep(0.0001);
    learning_rate_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    compact_control(learning_rate_);
    use_gpu_ = new QCheckBox(tr("GPU"), run_section);
    use_gpu_->setObjectName("trainingUseGpu");
    use_gpu_->setToolTip(tr("Prefer GPU when an available training device supports it."));
    compact_control(use_gpu_);
    run_grid->addWidget(field_label(tr("Input size"), run_section), 0, 0);
    run_grid->addWidget(field_label(tr("Device"), run_section), 0, 1);
    run_grid->addWidget(image_size, 1, 0);
    run_grid->addWidget(use_gpu_, 1, 1);
    run_grid->addWidget(field_label(tr("Batch"), run_section), 2, 0);
    run_grid->addWidget(field_label(tr("Epochs"), run_section), 2, 1);
    run_grid->addWidget(batch_size_, 3, 0);
    run_grid->addWidget(epochs_, 3, 1);
    run_grid->addWidget(field_label(tr("Learning rate"), run_section), 4, 0);
    run_grid->addWidget(learning_rate_, 5, 0);
    run_grid->setColumnStretch(0, 1);
    run_grid->setColumnStretch(1, 1);
    settings_layout->addWidget(run_section);

    advanced_toggle_ = new QPushButton(settings);
    advanced_toggle_->setObjectName("trainingAdvancedToggle");
    advanced_toggle_->setCheckable(true);
    advanced_toggle_->setProperty("disclosureRow", true);
    compact_control(advanced_toggle_);
    advanced_toggle_->setToolTip(
        tr("Show optimizer, scheduler, worker, and regularization settings."));
    settings_layout->addWidget(advanced_toggle_);
    advanced_panel_ = new QFrame(settings);
    advanced_panel_->setObjectName("trainingAdvancedPanel");
    advanced_panel_->setFrameShape(QFrame::NoFrame);
    advanced_panel_->hide();
    auto* advanced_form = new QFormLayout(advanced_panel_);
    advanced_form->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                                      ThemeTokens::Space2, ThemeTokens::Space1);
    advanced_form->setHorizontalSpacing(ThemeTokens::Space2);
    advanced_form->setVerticalSpacing(ThemeTokens::Space1);
    advanced_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    advanced_form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    workers_ = new QSpinBox(advanced_panel_);
    workers_->setRange(0, 64);
    workers_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    compact_control(workers_);
    weight_decay_ = new CompactDoubleSpinBox(advanced_panel_);
    weight_decay_->setDecimals(8);
    weight_decay_->setRange(0, 1);
    weight_decay_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    compact_control(weight_decay_);
    optimizer_ = new QComboBox(advanced_panel_);
    optimizer_->addItems({"adam", "sgd"});
    compact_control(optimizer_);
    scheduler_ = new QComboBox(advanced_panel_);
    scheduler_->addItems({"cosine", "step"});
    compact_control(scheduler_);
    advanced_form->addRow(tr("Workers"), workers_);
    advanced_form->addRow(tr("Weight decay"), weight_decay_);
    advanced_form->addRow(tr("Optimizer"), optimizer_);
    advanced_form->addRow(tr("Scheduler"), scheduler_);
    augmentation_button_ = new QPushButton(tr("Configure augmentation…"), advanced_panel_);
    augmentation_button_->setObjectName("trainingAugmentationButton");
    compact_control(augmentation_button_);
    advanced_form->addRow(tr("Data augmentation"), augmentation_button_);
    QLineEdit* output = nullptr;
    auto* output_row = path_editor(
        advanced_panel_, output, "trainingOutputDirectory", "trainingOutputBrowseButton",
        [this] {
            return QFileDialog::getExistingDirectory(
                this, tr("Training output directory"), output_directory_->text());
        },
        [this](const QString& value) {
            if (!service_ || rebuilding_) return;
            service_->task().training.output_directory = value.toStdString();
            if (changed) changed();
        });
    output_directory_ = output;
    compact_control(output_directory_);
    advanced_form->addRow(tr("Output directory"), output_row);
    settings_layout->addWidget(advanced_panel_);

    settings_layout->addWidget(divider(settings));
    start_ = new QPushButton(tr("Start training"), settings);
    start_->setObjectName("trainingStartButton");
    start_->setProperty("primary", true);
    settings_layout->addWidget(start_);
    settings_layout->addStretch();

    auto* monitor = new QFrame(split);
    monitor->setObjectName("embeddedTrainingMonitor");
    monitor->setProperty("sectionPanel", true);
    auto* monitor_layout = new QVBoxLayout(monitor);
    monitor_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                                       ThemeTokens::Space2, ThemeTokens::Space2);
    auto* monitor_header = new QHBoxLayout;
    monitor_header->addWidget(title_label(tr("Training monitor"), monitor));
    state_ = new QLabel(tr("Idle"), monitor);
    state_->setObjectName("trainingRuntimeState");
    state_->setProperty("statusBadge", true);
    monitor_header->addStretch();
    monitor_header->addWidget(state_);
    stop_ = new QPushButton(tr("Stop"), monitor);
    stop_->setObjectName("trainingStopButton");
    stop_->setProperty("destructive", true);
    stop_->setProperty("compact", true);
    stop_->setToolTip(tr("End this training at the next batch boundary."));
    stop_->setEnabled(false);
    stop_->hide();
    monitor_header->addWidget(stop_);
    pause_ = new QPushButton(tr("Pause"), monitor);
    pause_->setObjectName("trainingPauseButton");
    pause_->setProperty("compact", true);
    pause_->setToolTip(tr("Pause after the current epoch and save a resumable checkpoint."));
    pause_->setEnabled(false);
    pause_->hide();
    monitor_header->addWidget(pause_);
    resume_ = new QPushButton(tr("Resume"), monitor);
    resume_->setObjectName("trainingResumeButton");
    resume_->setProperty("compact", true);
    resume_->setToolTip(tr("Continue this training from its paused checkpoint."));
    resume_->setEnabled(false);
    resume_->hide();
    monitor_header->addWidget(resume_);
    monitor_layout->addLayout(monitor_header);
    auto* runtime_metrics = new QHBoxLayout;
    epoch_ = new QLabel(tr("Epoch —"), monitor);
    loss_ = new QLabel(tr("Loss —"), monitor);
    metric_ = new QLabel(tr("Metric —"), monitor);
    for (auto* value : {epoch_, loss_, metric_}) value->setProperty("runtimeMetric", true);
    runtime_metrics->addWidget(epoch_);
    runtime_metrics->addWidget(loss_);
    runtime_metrics->addWidget(metric_);
    runtime_metrics->addStretch();
    monitor_layout->addLayout(runtime_metrics);
    progress_ = new QProgressBar(monitor);
    progress_->setObjectName("trainingProgress");
    progress_->setRange(0, 100);
    monitor_layout->addWidget(progress_);
    chart_ = new LossChart(monitor);
    chart_->setObjectName("embeddedLossChart");
    chart_->setMinimumHeight(220);
    monitor_layout->addWidget(chart_, 1);
    auto* log_bar = new QHBoxLayout;
    log_bar->addStretch();
    run_details_ = new QPushButton(tr("View training log"), monitor);
    run_details_->setObjectName("trainingRunDetailsButton");
    run_details_->setProperty("compact", true);
    run_details_->setToolTip(
        tr("Open the training log in a separate window."));
    log_bar->addWidget(run_details_);
    monitor_layout->addLayout(log_bar);

    auto* models_panel = new QFrame(split);
    models_panel_ = models_panel;
    models_panel->setObjectName("trainingModelsPanel");
    models_panel->setProperty("sectionPanel", true);
    models_panel->setMinimumWidth(260);
    auto* models_layout = new QVBoxLayout(models_panel);
    models_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                                      ThemeTokens::Space2, ThemeTokens::Space2);
    models_layout->setSpacing(ThemeTokens::Space1);
    model_browser_ = new ProjectModelBrowser(models_panel);
    model_browser_->setObjectName("trainingProjectModelBrowser");
    model_browser_->set_model_check_selection(true);
    model_browser_->set_management_multi_selection(true);
    model_browser_->set_detail_visible(false);
    model_browser_->import_requested = [this] {
        if (import_model_requested) import_model_requested();
    };
    model_browser_->export_requested = [this](const QString& id) {
        if (export_model_requested) export_model_requested(id);
    };
    model_browser_->delete_models_requested = [this](const QStringList& ids) {
        if (delete_models_requested) delete_models_requested(ids);
    };
    model_browser_->delete_model_weights_requested = [this](const ModelWeightSelection& weights) {
        if (delete_model_weights_requested) {
            delete_model_weights_requested(weights);
        }
    };
    model_browser_->delete_model_weight_requested = [this](const QString& model_id, const QString& weight_id) {
        if (delete_model_weight_requested) {
            delete_model_weight_requested(model_id, weight_id);
        }
    };
    model_browser_->clear_models_requested = [this] {
        if (clear_models_requested) clear_models_requested();
    };
    model_browser_->selection_changed = [this](const QString& id) {
        if (!rebuilding_ && service_) service_->select_inference_model(id);
    };
    model_browser_->checked_model_changed = [this](const QString& id) {
        if (!rebuilding_ && service_) service_->select_inference_model(id);
        update_model_compatibility();
    };
    models_layout->addWidget(model_browser_, 3);
    source_status_ = helper_label({}, models_panel);
    source_status_->setObjectName("trainingModelCompatibility");
    models_layout->addWidget(source_status_);
    ResourceMonitorWidget* resources = new ResourceMonitorWidget(models_panel);
    resources->setObjectName("trainingResourceMonitor");
    models_layout->addWidget(resources, 1);

    split->addWidget(workspace_scroll_);
    split->addWidget(monitor);
    split->addWidget(models_panel);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setCollapsible(0, true);
    split->setCollapsible(2, true);
    split->setHandleWidth(11);
    split->setSizes({290, 900, 380});
    split->handle(1)->setObjectName("trainingConfigurationHandle");
    split->handle(1)->setProperty("collapsePanelIndex", 0);
    split->handle(1)->setProperty("restoreWidth", 290);
    split->handle(1)->setToolTip(tr("Drag to resize configuration; click to collapse or expand."));
    split->handle(2)->setObjectName("trainingModelsHandle");
    split->handle(2)->setProperty("collapsePanelIndex", 2);
    split->handle(2)->setProperty("restoreWidth", 380);
    split->handle(2)->setToolTip(tr("Drag to resize Project Models; click to collapse or expand."));
    root->addWidget(split, 1);

    connect(augmentation_button_, &QPushButton::clicked, this, [this] {
        if (!service_) return;
        AugmentationDialog dialog(
            jocelin::config::tool_type_from_string(service_->task().task),
            service_->task().training.augment, this);
        if (dialog.exec() != QDialog::Accepted) return;
        service_->task().training.augment = dialog.value();
        update_advanced_presentation();
        if (changed) changed();
    });
    connect(advanced_toggle_, &QPushButton::toggled, this, [this](bool shown) {
        advanced_panel_->setVisible(shown);
        update_advanced_presentation();
        QTimer::singleShot(0, this, [this, shown] {
            update_training_content_height();
            if (shown && workspace_scroll_ && advanced_panel_)
                workspace_scroll_->ensureWidgetVisible(
                    advanced_panel_, 0, ThemeTokens::Space3);
        });
    });

    connect(model_scale_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (rebuilding_) return;
        populate_model_options();
        commit_model_spec();
    });
    connect(model_index_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { commit_model_spec(); });
    auto commit_settings = [this] {
        update_advanced_presentation();
        if (rebuilding_ || !service_) return;
        auto& value = service_->task().training;
        value.image_width = image_width_->value();
        value.image_height = image_height_->value();
        value.batch_size = batch_size_->value();
        value.epochs = epochs_->value();
        value.learning_rate = static_cast<float>(learning_rate_->value());
        value.workers = workers_->value();
        value.weight_decay = static_cast<float>(weight_decay_->value());
        value.optimizer = optimizer_->currentText().toStdString();
        value.scheduler = scheduler_->currentText().toStdString();
        service_->task().device = use_gpu_->isChecked()
            ? jocelin::config::Device{jocelin::config::DeviceType::Gpu, 0, "cuda:0"}
            : jocelin::config::cpu_device();
        update_model_compatibility();
        if (changed) changed();
    };
    connect(image_width_, qOverload<int>(&QSpinBox::valueChanged), this, [commit_settings](int) { commit_settings(); });
    connect(image_height_, qOverload<int>(&QSpinBox::valueChanged), this, [commit_settings](int) { commit_settings(); });
    connect(batch_size_, qOverload<int>(&QSpinBox::valueChanged), this, [commit_settings](int) { commit_settings(); });
    connect(epochs_, qOverload<int>(&QSpinBox::valueChanged), this, [commit_settings](int) { commit_settings(); });
    connect(learning_rate_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [commit_settings](double) { commit_settings(); });
    connect(workers_, qOverload<int>(&QSpinBox::valueChanged), this, [commit_settings](int) { commit_settings(); });
    connect(weight_decay_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [commit_settings](double) { commit_settings(); });
    connect(optimizer_, qOverload<int>(&QComboBox::currentIndexChanged), this, [commit_settings](int) { commit_settings(); });
    connect(scheduler_, qOverload<int>(&QComboBox::currentIndexChanged), this, [commit_settings](int) { commit_settings(); });
    connect(use_gpu_, &QCheckBox::toggled, this, [commit_settings](bool) { commit_settings(); });
    connect(start_, &QPushButton::clicked, this, [this] { request_start(); });
    connect(stop_, &QPushButton::clicked, this, [this] { if (stop_requested) stop_requested(); });
    connect(pause_, &QPushButton::clicked, this, [this] {
        pause_->setEnabled(false);
        set_runtime_state(state_, QStringLiteral("pausing"));
        if (pause_requested) pause_requested();
    });
    connect(resume_, &QPushButton::clicked, this, [this] {
        if (!service_ || !start_requested) return;
        const auto* model = controlled_model();
        if (!model ||
            model->training_status != jocelin::model::ModelTrainingStatus::Paused)
            return;
        TrainingLaunchRequest request;
        request.mode = "resume";
        request.model_id = QString::fromStdString(model->id);
        start_requested(request);
    });
    connect(run_details_, &QPushButton::clicked, this, [this] {
        QString run_id;
        const QString model_id = model_browser_ ? model_browser_->current_model_id() : QString{};
        if (!model_id.isEmpty()) {
            const auto found = std::find_if(
                models_.begin(), models_.end(),
                [&model_id](const ProjectModel& value) {
                    return QString::fromStdString(value.id) == model_id;
                });
            if (found != models_.end())
                run_id = QString::fromStdString(found->training_run_id);
        }
        if (run_details_requested) run_details_requested(run_id);
    });
    update_advanced_presentation();
    update_training_content_height();
    UiMetrics::apply_semantic_metrics(this);
}

void TrainingWorkspace::update_advanced_presentation() {
    if (!advanced_toggle_ || !optimizer_ || !scheduler_ || !workers_) return;
    auto display_name = [](QString value) {
        if (value.compare("sgd", Qt::CaseInsensitive) == 0)
            return QStringLiteral("SGD");
        if (!value.isEmpty()) value[0] = value[0].toUpper();
        return value;
    };
    const QString optimizer = display_name(optimizer_->currentText());
    const QString scheduler = display_name(scheduler_->currentText());
    advanced_toggle_->setText(
        tr("Advanced    %1 · %2 · %3 workers    %4")
            .arg(optimizer)
            .arg(scheduler)
            .arg(workers_->value())
            .arg(advanced_toggle_->isChecked()
                     ? QStringLiteral("⌄")
                     : QStringLiteral("›")));
    advanced_toggle_->setAccessibleName(
        tr("Advanced training settings: %1, %2, %3 workers")
            .arg(optimizer)
            .arg(scheduler)
            .arg(workers_->value()));
}

void TrainingWorkspace::update_training_content_height() {
    if (!settings_panel_ || !settings_panel_->layout()) return;
    settings_panel_->layout()->activate();
}

void TrainingWorkspace::set_service(TaskWorkspaceService* service) {
    service_ = service;
    refresh();
}

void TrainingWorkspace::set_models(
    const std::vector<ProjectModel>& models)
{
    models_ = models;
    populate_models();
}

void TrainingWorkspace::populate_model_options() {
    if (!service_) return;
    const int current = service_->task().model_index;
    const QString capacity = model_scale_->currentData().toString();
    const QSignalBlocker block(model_index_);
    model_index_->clear();
    for (const auto& option : model_spec_options) {
        if (service_->task().task != option.task || capacity != option.scale ||
            model_index_->findData(option.index) >= 0) continue;
        model_index_->addItem(tr("Model %1").arg(option.index), option.index);
    }
    int index = model_index_->findData(current);
    if (index < 0 && model_index_->count() > 0) index = 0;
    model_index_->setCurrentIndex(index);
    model_index_->setEnabled(model_index_->count() > 1);
}

void TrainingWorkspace::populate_models() {
    if (!model_browser_) return;
    std::vector<ProjectModel> task_models;
    if (service_) {
        const auto& task = service_->task();
        const auto task_type = jocelin::config::tool_type_from_string(task.task);
        for (const auto& model : models_)
            if (model.task == task_type)
                task_models.push_back(model);
    }
    model_browser_->set_models(task_models);
    update_model_compatibility();
}

const ProjectModel* TrainingWorkspace::controlled_model() const {
    if (!service_) return nullptr;
    const std::string& id = service_->task().runtime.training_model_id;
    const auto found = std::find_if(
        models_.begin(), models_.end(),
        [&id](const ProjectModel& model) { return model.id == id; });
    return found == models_.end() ? nullptr : &*found;
}

QString TrainingWorkspace::training_source_mode() const {
    if (!service_ || !model_browser_) return "new";
    const QString id = model_browser_->checked_model_id();
    if (id.isEmpty()) return "new";
    const auto found = std::find_if(
        models_.begin(), models_.end(),
        [&id](const ProjectModel& value) {
            return QString::fromStdString(value.id) == id;
        });
    if (found == models_.end()) return "new";

    std::vector<ProjectModelClass> current_classes;
    const bool classification = service_->task().task == "cls";
    if (classification) {
        for (const auto* value : classification_classes(service_->binding().classes))
            current_classes.push_back({value->id, value->name, 0});
    } else {
        for (const auto& value : service_->binding().classes) {
            if (!value.active) continue;
            current_classes.push_back({value.id, value.name, value.mask_value});
        }
        std::sort(current_classes.begin(), current_classes.end(),
            [](const auto& left, const auto& right) {
                return left.mask_value < right.mask_value;
            });
    }
    const bool same_classes =
        found->classes.size() == current_classes.size() &&
        std::equal(
            found->classes.begin(), found->classes.end(),
            current_classes.begin(), current_classes.end(),
            [classification](const auto& left, const auto& right) {
                return left.id == right.id &&
                    left.name == right.name &&
                    (classification || left.mask_value == right.mask_value);
            });
    const auto& task = service_->task();
    const bool same_model_spec =
        found->task == jocelin::config::tool_type_from_string(task.task) &&
        found->model_index == task.model_index &&
        found->model_scale == jocelin::config::model_scale_from_string(task.model_scale);
    return same_classes && same_model_spec ? "inherit" : "transfer";
}

void TrainingWorkspace::update_model_compatibility() {
    if (!model_browser_ || !source_status_) return;
    model_browser_->setEnabled(true);

    if (service_) {
        for (const auto& model : models_) {
            const auto* training_weight = jocelin::model::find_training_weight(model);
            const bool lifecycle_eligible =
                model.training_status == jocelin::model::ModelTrainingStatus::Completed ||
                model.training_status == jocelin::model::ModelTrainingStatus::Stopped ||
                model.training_status == jocelin::model::ModelTrainingStatus::Imported;
            const bool eligible = lifecycle_eligible && training_weight &&
                QFileInfo::exists(QString::fromStdString(training_weight->path));
            model_browser_->set_model_check_eligible(
                QString::fromStdString(model.id), eligible);
        }
    }

    const QString id = model_browser_->checked_model_id();
    if (id.isEmpty()) {
        model_compatibility_severity_.clear();
        source_status_->setText(tr("Source: New model"));
        source_status_->setToolTip(
            tr("No source model is selected; training will create a new model."));
        source_status_->setProperty("inlineWarning", false);
        start_->setEnabled(true);
        return;
    }
    if (!service_) return;

    const auto found = std::find_if(
        models_.begin(), models_.end(),
        [&id](const ProjectModel& value) {
            return QString::fromStdString(value.id) == id;
        });
    if (found == models_.end()) return;
    const auto& model = *found;
    const auto& task = service_->task();
    const auto task_type = jocelin::config::tool_type_from_string(task.task);
    const auto model_scale = jocelin::config::model_scale_from_string(task.model_scale);
    const auto* training_weight = jocelin::model::find_training_weight(model);
    const QString mode = training_source_mode();
    QStringList incompatible;

    if (!training_weight ||
        !QFileInfo::exists(QString::fromStdString(training_weight->path)))
        incompatible << tr("LibTorch training weight is missing");
    if (mode == "inherit") {
        if (model.task != task_type)
            incompatible << tr("task type differs");
        if (model.model_scale != model_scale)
            incompatible << tr("capacity differs");
        if (model.model_index != task.model_index)
            incompatible << tr("network/weight family differs");
    }

    QString severity = "compatible";
    QString message;
    QString detail;
    if (!incompatible.isEmpty()) {
        severity = "incompatible";
        message = tr("Source unavailable · %1").arg(incompatible.front());
        detail = tr("Cannot use %1: %2")
                     .arg(QString::fromStdString(model.name), incompatible.join("; "));
    } else if (mode == "inherit") {
        message = tr("Source: %1 · Inherit")
                      .arg(QString::fromStdString(model.name));
        detail = tr("The class schema matches; training will initialize from compatible weights in this model.");
    } else {
        message = tr("Source: %1 · Transfer")
                      .arg(QString::fromStdString(model.name));
        detail = tr("Training will load only parameters with compatible names and shapes from this model.");
    }
    model_compatibility_severity_ = severity;
    source_status_->setText(message);
    source_status_->setToolTip(detail);
    source_status_->setProperty("inlineWarning", severity != "compatible");
    source_status_->style()->unpolish(source_status_);
    source_status_->style()->polish(source_status_);
    start_->setEnabled(severity != "incompatible");
}

void TrainingWorkspace::commit_model_spec() {
    if (rebuilding_ || !service_) return;
    if (model_scale_->currentIndex() >= 0)
        service_->task().model_scale = model_scale_->currentData().toString().toStdString();
    if (model_index_->currentIndex() >= 0)
        service_->task().model_index = model_index_->currentData().toInt();
    update_model_compatibility();
    if (changed) changed();
}

void TrainingWorkspace::refresh() {
    if (!service_) return;
    rebuilding_ = true;
    const auto& task = service_->task();
    service_->refresh_quality();
    model_scale_->clear();
    for (const auto& option : model_spec_options) {
        if (task.task != option.task || model_scale_->findData(option.scale) >= 0) continue;
        model_scale_->addItem(model_scale_label(option.scale), option.scale);
    }
    model_scale_->setCurrentIndex(
        std::max(0, model_scale_->findData(QString::fromStdString(task.model_scale))));
    populate_model_options();
    model_index_->setCurrentIndex(
        std::max(0, model_index_->findData(task.model_index)));
    image_width_->setValue(task.training.image_width);
    image_height_->setValue(task.training.image_height);
    batch_size_->setValue(task.training.batch_size);
    epochs_->setValue(task.training.epochs);
    learning_rate_->setValue(task.training.learning_rate);
    workers_->setValue(task.training.workers);
    weight_decay_->setValue(task.training.weight_decay);
    optimizer_->setCurrentText(QString::fromStdString(task.training.optimizer));
    scheduler_->setCurrentText(QString::fromStdString(task.training.scheduler));
    use_gpu_->setChecked(task.device.type == jocelin::config::DeviceType::Gpu);
    output_directory_->setText(QString::fromStdString(task.training.output_directory));
    populate_models();
    const auto* model = controlled_model();
    const QString status = model
        ? QString::fromStdString(jocelin::model::to_string(model->training_status))
        : QStringLiteral("idle");
    const int completed_epochs = model ? model->completed_epochs : 0;
    set_runtime_state(state_, status);
    epoch_->setText(tr("Epoch %1 / %2").arg(completed_epochs).arg(task.training.epochs));
    loss_->setText(model
        ? tr("Loss %1").arg(model->training_loss, 0, 'g', 5)
        : tr("Loss —"));
    metric_->setText(
        !model || model->metric_name.empty()
            ? tr("Metric —")
            : tr("%1 %2").arg(QString::fromStdString(model->metric_name))
                             .arg(model->metric, 0, 'g', 5));
    progress_->setValue(task.training.epochs > 0
        ? qBound(0, completed_epochs * 100 / task.training.epochs, 100) : 0);
    const auto* training_weight = model
        ? jocelin::model::find_training_weight(*model)
        : nullptr;
    const bool resumable =
        model && model->training_status == jocelin::model::ModelTrainingStatus::Paused &&
        training_weight &&
        QFileInfo::exists(QString::fromStdString(training_weight->path));
    resume_->setVisible(resumable);
    resume_->setEnabled(resumable);
    rebuilding_ = false;
    update_advanced_presentation();
    QTimer::singleShot(
        0, this, [this] { update_training_content_height(); });
    update_model_compatibility();
}

void TrainingWorkspace::request_start() {
    if (!start_requested) return;
    TrainingLaunchRequest request;
    request.mode = training_source_mode();
    if (request.mode != "new") {
        request.model_id = model_browser_->checked_model_id();
        if (request.model_id.isEmpty()) return;
        const auto found = std::find_if(
            models_.begin(), models_.end(),
            [&request](const ProjectModel& value) {
                return QString::fromStdString(value.id) == request.model_id;
            });
        if (found == models_.end()) return;
        if (model_compatibility_severity_ == "incompatible") {
            QMessageBox::warning(
                this, tr("Model is incompatible"), source_status_->text());
            return;
        }
    }
    start_requested(request);
}

void TrainingWorkspace::set_busy(bool busy) {
    start_->setEnabled(!busy);
    start_->setVisible(!busy);
    stop_->setEnabled(busy);
    stop_->setVisible(busy);
    pause_->setEnabled(busy);
    pause_->setVisible(busy);
    resume_->setEnabled(false);
    resume_->setVisible(false);
    for (auto* widget : {
            static_cast<QWidget*>(model_index_),
            static_cast<QWidget*>(model_scale_),
            static_cast<QWidget*>(image_width_),
            static_cast<QWidget*>(image_height_),
            static_cast<QWidget*>(batch_size_),
            static_cast<QWidget*>(epochs_),
            static_cast<QWidget*>(learning_rate_),
            static_cast<QWidget*>(use_gpu_),
            static_cast<QWidget*>(output_directory_),
            static_cast<QWidget*>(workers_),
            static_cast<QWidget*>(weight_decay_),
            static_cast<QWidget*>(optimizer_),
            static_cast<QWidget*>(scheduler_),
            static_cast<QWidget*>(model_browser_)})
        widget->setEnabled(!busy);
    if (!busy) {
        update_model_compatibility();
        const auto* model = controlled_model();
        const auto* training_weight = model
            ? jocelin::model::find_training_weight(*model)
            : nullptr;
        const bool resumable = model &&
            model->training_status == jocelin::model::ModelTrainingStatus::Paused &&
            training_weight &&
            QFileInfo::exists(QString::fromStdString(training_weight->path));
        resume_->setVisible(resumable);
        resume_->setEnabled(resumable);
    }
}

void TrainingWorkspace::begin() {
    chart_->reset();
    progress_->setValue(0);
    epoch_->setText(tr("Epoch 0 / %1").arg(epochs_->value()));
    loss_->setText(tr("Loss —"));
    metric_->setText(tr("Metric —"));
    set_runtime_state(state_, QStringLiteral("running"));
}

void TrainingWorkspace::update_progress(const jocelin::train::TrainingProgress& value) {
    set_runtime_state(state_, QStringLiteral("running"));
    epoch_->setText(tr("Epoch %1 / %2").arg(value.completed_epochs).arg(value.total_epochs));
    loss_->setText(tr("Loss %1").arg(value.loss, 0, 'g', 5));
    metric_->setText(tr("%1 %2")
        .arg(QString::fromStdString(value.validation_metric_name))
        .arg(value.validation_metric, 0, 'g', 5));
    progress_->setValue(value.total_epochs > 0
        ? qBound(0, value.completed_epochs * 100 / value.total_epochs, 100) : 0);
    chart_->add_point(
        value.loss,
        value.validation_metric,
        QString::fromStdString(value.validation_metric_name));
}

void TrainingWorkspace::finish(
    const jocelin::training::TrainingResult& result,
    const QString& error)
{
    set_busy(false);
    set_runtime_state(
        state_, !error.isEmpty() ? QStringLiteral("error") :
                result.end_state == jocelin::training::TrainingEndState::Paused
                    ? QStringLiteral("paused") :
                result.end_state == jocelin::training::TrainingEndState::Stopped
                    ? QStringLiteral("stopped")
                    : QStringLiteral("completed"));
    refresh();
}

InferenceWorkspace::InferenceWorkspace(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                             ThemeTokens::Space2, ThemeTokens::Space2);
    root->setSpacing(ThemeTokens::Space1);

    auto* command_bar = new QFrame(this);
    command_bar->setObjectName("inferenceCommandBar");
    command_bar->setProperty("sectionPanel", true);
    auto* command_layout = new QGridLayout(command_bar);
    command_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                                       ThemeTokens::Space2, ThemeTokens::Space1);
    command_layout->setHorizontalSpacing(ThemeTokens::Space2);
    command_layout->setVerticalSpacing(ThemeTokens::Space1);
    auto* device_type_label_widget = new QLabel(tr("Device type"), command_bar);
    device_type_label_widget->setProperty("meta", true);
    device_type_ = new QComboBox(command_bar);
    device_type_->setObjectName("inferenceDeviceType");
    device_type_->setMinimumContentsLength(5);
    device_type_->setMaximumWidth(100);
    auto* device_label = new QLabel(tr("Device"), command_bar);
    device_label->setProperty("meta", true);
    device_ = new QComboBox(command_bar);
    device_->setObjectName("inferenceDevice");
    device_->setMinimumContentsLength(10);
    device_->setMaximumWidth(210);
    checked_target_ = new QLabel(command_bar);
    checked_target_->setObjectName("inferenceCheckedWeight");
    checked_target_->setTextFormat(Qt::PlainText);
    checked_target_->setWordWrap(true);
    checked_target_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* batch_label = new QLabel(tr("Batch"), command_bar);
    batch_label->setProperty("meta", true);
    batch_size_ = new QSpinBox(command_bar);
    batch_size_->setObjectName("inferenceBatchSize");
    batch_size_->setRange(1, 1024);
    batch_size_->setMaximumWidth(90);
    current_image_ = new QLabel(tr("Ready"), command_bar);
    current_image_->setObjectName("inferenceCurrentImage");
    current_image_->setProperty("meta", true);
    current_image_->setWordWrap(true);
    current_image_->setMinimumWidth(0);
    current_image_->setMaximumHeight(44);
    current_image_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    state_ = new QLabel(tr("Idle"), command_bar);
    state_->setObjectName("inferenceRuntimeState");
    state_->setProperty("statusBadge", true);
    start_ = new QPushButton(tr("Infer"), command_bar);
    start_->setObjectName("inferenceStartButton");
    start_->setProperty("primary", true);
    progress_ = new QProgressBar(command_bar);
    progress_->setObjectName("inferenceProgress");
    progress_->setRange(0, 100);
    command_layout->addWidget(device_type_label_widget, 0, 0);
    command_layout->addWidget(device_type_, 0, 1);
    command_layout->addWidget(device_label, 0, 2);
    command_layout->addWidget(device_, 0, 3);
    command_layout->addWidget(checked_target_, 0, 4, 1, 2);
    command_layout->addWidget(batch_label, 0, 6);
    command_layout->addWidget(batch_size_, 0, 7);
    command_layout->addWidget(state_, 0, 8);
    command_layout->addWidget(start_, 0, 9);
    command_layout->addWidget(current_image_, 1, 0, 1, 4);
    command_layout->addWidget(progress_, 1, 4, 1, 6);
    command_layout->setColumnStretch(3, 1);
    command_layout->setColumnStretch(5, 1);
    root->addWidget(command_bar);

    auto* split = new CollapsibleSplitter(Qt::Horizontal, this);
    split->setObjectName("inferenceWorkspaceSplitter");

    auto* samples = new QFrame(split);
    samples->setObjectName("inferenceSamplesPanel");
    samples->setMinimumWidth(220);
    auto* samples_layout = new QVBoxLayout(samples);
    samples_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                                       ThemeTokens::Space2, ThemeTokens::Space2);
    samples_layout->setSpacing(ThemeTokens::Space1);
    auto* sample_header = new QHBoxLayout;
    auto* samples_title = new QLabel(tr("SAMPLES"), samples);
    samples_title->setProperty("eyebrow", true);
    sample_header->addWidget(samples_title);
    scope_summary_ = new QLabel(samples);
    scope_summary_->setObjectName("inferenceScopeSummary");
    scope_summary_->setProperty("meta", true);
    sample_header->addWidget(scope_summary_);
    sample_header->addStretch();
    samples_layout->addLayout(sample_header);

    QWidget* filter_bar = new QWidget(samples);
    filter_bar->setObjectName("inferenceSampleFilterBar");
    QHBoxLayout* filter_layout = new QHBoxLayout(filter_bar);
    filter_layout->setContentsMargins(0, 0, 0, 0);
    filter_layout->setSpacing(ThemeTokens::Space1);
    QFont filter_font = filter_bar->font();
    filter_font.setPixelSize(11);
    sample_split_filter_ = new QComboBox(filter_bar);
    sample_split_filter_->setObjectName("inferenceSampleSplitFilter");
    sample_split_filter_->setFont(filter_font);
    sample_split_filter_->setFixedHeight(24);
    sample_split_filter_->setMinimumWidth(90);
    sample_split_filter_->setMaximumWidth(110);
    sample_split_filter_->addItem(tr("All splits"), "all");
    sample_split_filter_->addItem(tr("Train"), "train");
    sample_split_filter_->addItem(tr("Validation"), "validation");
    sample_split_filter_->addItem(tr("Test"), "test");
    sample_split_filter_->addItem(tr("Unassigned"), "unassigned");
    sample_split_filter_->setToolTip(tr("Filter by assigned sample split."));
    sample_search_ = new QLineEdit(filter_bar);
    sample_search_->setObjectName("inferenceSampleSearch");
    sample_search_->setFont(filter_font);
    sample_search_->setFixedHeight(24);
    sample_search_->setMinimumWidth(0);
    sample_search_->setPlaceholderText(tr("Find samples"));
    sample_search_->setClearButtonEnabled(true);
    filter_layout->addWidget(sample_split_filter_);
    filter_layout->addWidget(sample_search_, 1);
    samples_layout->addWidget(filter_bar);

    images_ = new QListWidget(samples);
    images_->setToolTip(
        tr("Select images to limit the inference scope. No selection means all matching images; use Ctrl or Shift for multi-selection."));
    images_->setObjectName("inferenceImageList");
    images_->setIconSize(QSize(56, 42));
    images_->setTextElideMode(Qt::ElideMiddle);
    images_->setUniformItemSizes(true);
    images_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    samples_layout->addWidget(images_, 1);

    auto* viewport = new QFrame(split);
    viewport->setObjectName("inferenceViewportPanel");
    auto* viewport_layout = new QVBoxLayout(viewport);
    viewport_layout->setContentsMargins(0, 0, 0, 0);
    viewport_layout->setSpacing(0);
    auto* layers_panel = new QFrame(viewport);
    layers_panel->setObjectName("inferenceLayerBar");
    auto* layers = new QHBoxLayout(layers_panel);
    layers->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                               ThemeTokens::Space2, ThemeTokens::Space1);
    overlay_layer_ = new QCheckBox(tr("Inference"), layers_panel);
    overlay_layer_->setObjectName("inferenceOverlayLayerCheck");
    overlay_layer_->setChecked(true);
    layers->addWidget(overlay_layer_);
    layers->addStretch();
    viewport_layout->addWidget(layers_panel);
    canvas_stack_ = new QStackedWidget(viewport);
    canvas_stack_->setObjectName("inferenceCanvasStack");
    segmentation_canvas_ = new ImageResultCanvas(canvas_stack_);
    segmentation_canvas_->setObjectName("inferenceSegmentationCanvas");
    segmentation_canvas_->set_prediction_visible(true);
    segmentation_canvas_->set_overlay_opacity(0.42);
    classification_canvas_ = new ClassificationCanvas(canvas_stack_);
    classification_canvas_->setObjectName("inferenceClassificationCanvas");
    canvas_stack_->addWidget(segmentation_canvas_);
    canvas_stack_->addWidget(classification_canvas_);
    viewport_layout->addWidget(canvas_stack_, 1);

    auto* models = new QFrame(split);
    models->setObjectName("inferenceModelsPanel");
    models->setMinimumWidth(260);
    auto* models_layout = new QVBoxLayout(models);
    models_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                                      ThemeTokens::Space2, ThemeTokens::Space2);
    models_layout->setSpacing(ThemeTokens::Space1);
    filter_panel_ = new InferenceFilterPanel(models);
    filter_panel_->applied = [this] {
        if (changed) {
            changed();
        }
    };
    models_layout->addWidget(filter_panel_);
    model_browser_ = new ProjectModelBrowser(models);
    model_browser_->setObjectName("inferenceProjectModelBrowser");
    model_browser_->set_branch_check_selection();
    model_browser_->set_management_multi_selection(true);
    model_browser_->set_detail_visible(false);
    model_browser_->convert_weight_requested = [this](const QString& model_id, const QString& weight_id, QWidget* anchor) {
        if (!service_ || busy_) {
            return;
        }
        const ProjectModel* model = service_->find_model(model_id);
        const jocelin::model::ModelWeight* source = model
            ? jocelin::model::find_weight(*model, weight_id.toStdString()) : nullptr;
        if (!model || !source) {
            return;
        }
        WeightConversionPopup popup(*model, *source, anchor);
        popup.adjustSize();
        const QPoint position = anchor->mapToGlobal(QPoint(0, anchor->height()));
        QScreen* screen = QGuiApplication::screenAt(position);
        if (screen) {
            const QRect bounds = screen->availableGeometry();
            popup.move(qBound(bounds.left(), position.x(), qMax(bounds.left(), bounds.right() - popup.width() + 1)),
                       qBound(bounds.top(), position.y(), qMax(bounds.top(), bounds.bottom() - popup.height() + 1)));
        } else {
            popup.move(position);
        }
        if (popup.exec() == QDialog::Accepted && convert_weight_requested) {
            convert_weight_requested(model_id, weight_id, popup.target_format());
        }
    };
    model_browser_->import_requested = [this] {
        if (import_model_requested) import_model_requested();
    };
    model_browser_->export_requested = [this](const QString& id) {
        if (export_model_requested) export_model_requested(id);
    };
    model_browser_->delete_models_requested = [this](const QStringList& ids) {
        if (delete_models_requested) delete_models_requested(ids);
    };
    model_browser_->delete_model_weights_requested = [this](const ModelWeightSelection& weights) {
        if (delete_model_weights_requested) {
            delete_model_weights_requested(weights);
        }
    };
    model_browser_->delete_model_weight_requested = [this](const QString& model_id, const QString& weight_id) {
        if (delete_model_weight_requested) {
            delete_model_weight_requested(model_id, weight_id);
        }
    };
    model_browser_->clear_models_requested = [this] {
        if (clear_models_requested) clear_models_requested();
    };
    model_browser_->weight_checks_changed = [this] {
        refresh_device_types();
        commit_result_selection();
        update_command_presentation();
    };
    models_layout->addWidget(model_browser_, 3);
    ResourceMonitorWidget* resources = new ResourceMonitorWidget(models);
    resources->setObjectName("inferenceResourceMonitor");
    models_layout->addWidget(resources, 1);

    split->addWidget(samples);
    split->addWidget(viewport);
    split->addWidget(models);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setCollapsible(0, true);
    split->setCollapsible(2, true);
    split->setHandleWidth(11);
    split->setSizes({280, 900, 340});
    split->handle(1)->setObjectName("inferenceSamplesHandle");
    split->handle(1)->setProperty("collapsePanelIndex", 0);
    split->handle(1)->setProperty("restoreWidth", 280);
    split->handle(1)->setToolTip(tr("Drag to resize samples; click to collapse or expand."));
    split->handle(2)->setObjectName("inferenceModelsHandle");
    split->handle(2)->setProperty("collapsePanelIndex", 2);
    split->handle(2)->setProperty("restoreWidth", 340);
    split->handle(2)->setToolTip(tr("Drag to resize Project Models; click to collapse or expand."));
    root->addWidget(split, 1);

    connect(batch_size_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (!service_ || rebuilding_) return;
        service_->task().inference_batch_size = value;
        if (changed) changed();
    });
    connect(device_type_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
        if (rebuilding_) return;
        refresh_devices();
        commit_device();
        update_command_presentation();
    });
    connect(device_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
        if (rebuilding_) return;
        commit_device();
        update_command_presentation();
    });
    const auto apply_sample_filter = [this] {
        if (rebuilding_) {
            return;
        }
        const QSignalBlocker blocker(images_);
        const QString split = sample_split_filter_->currentData().toString();
        const QString query = sample_search_->text().trimmed();
        QListWidgetItem* first_visible = nullptr;
        for (int index = 0; index < images_->count(); ++index) {
            QListWidgetItem* item = images_->item(index);
            const bool matches_split = split == "all" || item->data(Qt::UserRole + 1).toString() == split;
            const bool matches_query = query.isEmpty() ||
                item->text().contains(query, Qt::CaseInsensitive) ||
                item->data(Qt::UserRole).toString().contains(query, Qt::CaseInsensitive) ||
                item->data(Qt::UserRole + 2).toString().contains(query, Qt::CaseInsensitive);
            const bool visible = matches_split && matches_query;
            item->setHidden(!visible);
            if (visible && !first_visible) {
                first_visible = item;
            } else if (!visible) {
                item->setSelected(false);
            }
        }
        if (!first_visible) {
            images_->setCurrentRow(-1, QItemSelectionModel::NoUpdate);
        } else if (!images_->currentItem() || images_->currentItem()->isHidden()) {
            images_->setCurrentItem(first_visible, QItemSelectionModel::NoUpdate);
        }
        refresh_canvas();
        update_command_presentation();
    };
    connect(sample_split_filter_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [apply_sample_filter](int) { apply_sample_filter(); });
    connect(sample_search_, &QLineEdit::textChanged, this,
            [apply_sample_filter](const QString&) { apply_sample_filter(); });
    connect(images_, &QListWidget::itemSelectionChanged, this, [this] {
        update_command_presentation();
    });
    connect(images_, &QListWidget::currentRowChanged, this, [this](int) {
        refresh_canvas();
    });
    connect(overlay_layer_, &QCheckBox::toggled, this, [this](bool visible) {
        if (segmentation_canvas_) segmentation_canvas_->set_prediction_visible(visible);
        if (classification_canvas_) classification_canvas_->set_heatmap_visible(visible);
    });
    connect(start_, &QPushButton::clicked, this, [this] { request_start(); });
    update_command_presentation();
    UiMetrics::apply_semantic_metrics(this);
}

void InferenceWorkspace::set_service(TaskWorkspaceService* service) {
    if (service_ == service) {
        refresh();
        return;
    }

    filter_panel_->set_config(nullptr);
    filter_panel_->set_content(nullptr);
    model_browser_->set_models({});
    service_ = service;
    if (!service_) {
        refresh();
        return;
    }

    const TaskInferenceFilterSpec* spec = find_inference_filter_spec(service_->task().task);
    if (!spec) {
        refresh();
        return;
    }

    std::unique_ptr<InferenceFilterContent> content = spec->create_content();
    filter_panel_->set_content(std::move(content));
    jocelin::config::AiConfig& config = service_->task().inference_config;
    filter_panel_->set_config(&config);
    refresh();
}

void InferenceWorkspace::set_models(const std::vector<ProjectModel>& models) {
    models_ = models;
    refresh();
}

void InferenceWorkspace::refresh() {
    rebuilding_ = true;
    images_->clear();
    if (!service_) {
        available_devices_.clear();
        cpu_only_weight_ = false;
        device_type_->clear();
        device_->clear();
        model_browser_->set_models({});
        if (segmentation_canvas_) segmentation_canvas_->clear();
        if (classification_canvas_) classification_canvas_->clear_canvas();
        rebuilding_ = false;
        update_command_presentation();
        return;
    }
    const auto& task = service_->task();
    available_devices_ = jocelin::runtime::available_compute_devices();
    const auto task_type = jocelin::config::tool_type_from_string(task.task);
    std::vector<ProjectModel> task_models;
    for (const auto& model : models_)
        if (model.task == task_type) task_models.push_back(model);
    model_browser_->set_models(task_models);
    if (service_->selected_model_id().isEmpty() && !task_models.empty())
        service_->select_inference_model(QString::fromStdString(task_models.front().id));
    model_browser_->set_current_model_id(service_->selected_model_id());
    refresh_device_types();
    batch_size_->setValue(task.inference_batch_size);
    const std::set<std::string> selected(
        task.inference_images.begin(), task.inference_images.end());
    const auto& samples = service_->binding().samples;
    const QString split_filter = sample_split_filter_->currentData().toString();
    const QString query = sample_search_->text().trimmed();
    for (int sample_index = 0;
         sample_index < static_cast<int>(samples.size());
         ++sample_index) {
        const auto& sample = samples[static_cast<size_t>(sample_index)];
        if (!sample.selected) {
            continue;
        }
        const QString display_name = image_display_name(sample_index, sample);
        const QString tooltip = sample_image_tooltip(
            display_name, sample, annotation_label(sample));
        for (int image_index = 0;
             image_index < static_cast<int>(sample.image_paths.size());
             ++image_index) {
            const auto& path = sample.image_paths[static_cast<size_t>(image_index)];
            const QString item_text = sample.image_paths.size() > 1
                ? tr("%1 · %2/%3")
                      .arg(display_name)
                      .arg(image_index + 1)
                      .arg(sample.image_paths.size())
                : display_name;
            QListWidgetItem* item = new QListWidgetItem(item_text, images_);
            item->setData(Qt::UserRole, QString::fromStdString(path));
            item->setData(Qt::UserRole + 1, QString::fromStdString(normalized_split(sample.split)));
            item->setData(Qt::UserRole + 2, QString::fromStdString(sample.sample_id));
            item->setToolTip(tooltip);
            QImageReader reader(QString::fromStdString(path));
            reader.setAutoTransform(true);
            reader.setScaledSize(QSize(112, 84));
            const QImage thumbnail = reader.read();
            if (!thumbnail.isNull()) {
                item->setIcon(QPixmap::fromImage(thumbnail));
            }
            const bool matches_split = split_filter == "all" ||
                item->data(Qt::UserRole + 1).toString() == split_filter;
            const bool matches_query = query.isEmpty() ||
                item->text().contains(query, Qt::CaseInsensitive) ||
                item->data(Qt::UserRole).toString().contains(query, Qt::CaseInsensitive) ||
                item->data(Qt::UserRole + 2).toString().contains(query, Qt::CaseInsensitive);
            const bool visible = matches_split && matches_query;
            item->setHidden(!visible);
            if (visible && !task.inference_use_all_images && selected.count(path) != 0) {
                item->setSelected(true);
            }
        }
    }
    set_runtime_state(state_, QString::fromStdString(task.runtime.inference_status));
    if (!images_->currentItem() || images_->currentItem()->isHidden()) {
        QListWidgetItem* current = images_->selectedItems().isEmpty()
            ? nullptr : images_->selectedItems().front();
        for (int index = 0; !current && index < images_->count(); ++index) {
            if (!images_->item(index)->isHidden()) {
                current = images_->item(index);
            }
        }
        if (current) {
            images_->setCurrentItem(current, QItemSelectionModel::NoUpdate);
        } else {
            images_->setCurrentRow(-1, QItemSelectionModel::NoUpdate);
        }
    }
    rebuilding_ = false;
    update_command_presentation();
    refresh_canvas();
}

void InferenceWorkspace::commit_result_selection() {
    if (rebuilding_ || !service_) {
        return;
    }
    const ModelWeightSelection checked = model_browser_->checked_weights();
    if (checked.size() == 1) {
        service_->select_inference_result(checked.front().first, checked.front().second);
        refresh_canvas();
    }
}

void InferenceWorkspace::synchronize_result_selection() {
    if (!service_) {
        return;
    }
    model_browser_->set_current_model_id(service_->selected_model_id());
    update_command_presentation();
    refresh_canvas();
}

void InferenceWorkspace::refresh_device_types() {
    if (!device_type_) {
        return;
    }
    using jocelin::config::DeviceType;
    const jocelin::model::ModelWeight* weight = selected_weight(model_browser_->checked_weights());
    cpu_only_weight_ = false;
    if (weight && jocelin::model::weight_supports_device(*weight, DeviceType::Cpu)) {
        cpu_only_weight_ = !jocelin::model::weight_supports_device(*weight, DeviceType::Gpu);
        if (!cpu_only_weight_) {
            const jocelin::runtime::RuntimeCapabilities capabilities = jocelin::runtime::available_runtime_capabilities();
            cpu_only_weight_ = jocelin::runtime::runtime_supports_device(weight->format, DeviceType::Cpu, capabilities) &&
                !jocelin::runtime::runtime_supports_device(weight->format, DeviceType::Gpu, capabilities);
        }
    }
    QString preferred;
    if (service_)
        preferred = QString::fromStdString(
            jocelin::config::to_string(service_->task().device.type));

    QSignalBlocker blocker(device_type_);
    device_type_->clear();
    std::set<jocelin::config::DeviceType> types;
    for (const auto& device : available_devices_) {
        if (device.device.type != jocelin::config::DeviceType::Cpu &&
            device.device.type != jocelin::config::DeviceType::Gpu)
            continue;
        types.insert(device.device.type);
    }
    for (const auto type : {jocelin::config::DeviceType::Cpu,
                            jocelin::config::DeviceType::Gpu}) {
        if (types.count(type) == 0 || (cpu_only_weight_ && type != DeviceType::Cpu)) {
            continue;
        }
        device_type_->addItem(
            device_type_label(type),
            QString::fromStdString(jocelin::config::to_string(type)));
    }
    device_type_->setToolTip(cpu_only_weight_ ? tr("The checked Weight branch requires CPU.") : QString{});
    const int preferred_index = device_type_->findData(preferred);
    if (preferred_index >= 0) device_type_->setCurrentIndex(preferred_index);
    refresh_devices();
}

void InferenceWorkspace::refresh_devices() {
    if (!device_ || !device_type_) return;
    const QString selected_type = device_type_->currentData().toString();
    QString preferred_id;
    int preferred_index = -1;
    if (service_) {
        preferred_id = QString::fromStdString(service_->task().device.id);
        preferred_index = service_->task().device.index;
    }
    const jocelin::runtime::ComputeDevice* selected = selected_device();
    if (selected && QString::fromStdString(jocelin::config::to_string(selected->device.type)) == selected_type) {
        preferred_id = QString::fromStdString(selected->device.id);
        preferred_index = selected->device.index;
    }

    QSignalBlocker blocker(device_);
    device_->clear();
    int match = -1;
    for (size_t index = 0; index < available_devices_.size(); ++index) {
        const auto& compute = available_devices_[index];
        if (QString::fromStdString(jocelin::config::to_string(compute.device.type)) !=
            selected_type)
            continue;
        device_->addItem(
            QString::fromStdString(compute.display_name),
            static_cast<int>(index));
        if (QString::fromStdString(compute.device.id) == preferred_id &&
            compute.device.index == preferred_index)
            match = device_->count() - 1;
    }
    if (match >= 0) device_->setCurrentIndex(match);
}

void InferenceWorkspace::commit_device() {
    // Keep the user's device preference when CPU is required by the checked branch.
    if (rebuilding_ || !service_ || cpu_only_weight_) {
        return;
    }
    const auto* compute = selected_device();
    if (!compute || service_->task().device == compute->device) return;
    service_->task().device = compute->device;
    if (changed) changed();
}

const jocelin::runtime::ComputeDevice* InferenceWorkspace::selected_device() const {
    if (!device_ || device_->currentIndex() < 0) return nullptr;
    const int index = device_->currentData().toInt();
    if (index < 0 || index >= static_cast<int>(available_devices_.size())) return nullptr;
    return &available_devices_[static_cast<size_t>(index)];
}

const jocelin::model::ModelWeight* InferenceWorkspace::selected_weight(const ModelWeightSelection& checked) const {
    if (!service_ || checked.size() != 1) {
        return nullptr;
    }
    const ProjectModel* model = service_->find_model(checked.front().first);
    return model ? jocelin::model::find_weight(*model, checked.front().second.toStdString()) : nullptr;
}

namespace {

bool inference_weight_available(const jocelin::model::ModelWeight& weight, const jocelin::config::Device& device) {
    if (!ModelWeightAssets::missing(weight).isEmpty() || !jocelin::model::weight_supports_device(weight, device.type)) {
        return false;
    }
    const auto capabilities = jocelin::runtime::available_runtime_capabilities();
    return jocelin::runtime::runtime_is_available(weight.format, capabilities) &&
        jocelin::runtime::runtime_supports_device(weight.format, device.type, capabilities);
}

} // namespace

void InferenceWorkspace::refresh_canvas() {
    if (service_ && !busy_)
        set_runtime_state(state_, service_->selected_inference_result() ? QStringLiteral("completed") : QStringLiteral("idle"));
    if (!canvas_stack_ || !segmentation_canvas_ || !classification_canvas_) return;
    if (!service_ || !images_ || !images_->currentItem()) {
        segmentation_canvas_->clear();
        classification_canvas_->clear_canvas();
        return;
    }

    const QString image_path = images_->currentItem()->data(Qt::UserRole).toString();
    const auto& task = service_->task();
    const bool classification = task.task == "cls";
    canvas_stack_->setCurrentWidget(
        classification ? static_cast<QWidget*>(classification_canvas_)
                       : static_cast<QWidget*>(segmentation_canvas_));
    overlay_layer_->setText(classification ? tr("Heatmap") : tr("Inference"));

    if (classification) {
        const QImage image(image_path);
        if (image.isNull()) {
            classification_canvas_->clear_canvas();
            return;
        }
        classification_canvas_->set_image(image);
        classification_canvas_->set_heatmap({});
        classification_canvas_->set_heatmap_visible(overlay_layer_->isChecked());
    } else {
        QString error;
        if (!segmentation_canvas_->load(image_path, &error)) {
            segmentation_canvas_->clear();
            return;
        }
        segmentation_canvas_->set_prediction_overlay({});
    }

    const ResultReference* match = nullptr;
    const auto* selected_result = service_->selected_inference_result();
    const auto* sample = task_sample_for_path(service_->binding(), image_path);
    if (selected_result && sample) {
        for (const auto& result : selected_result->samples) {
            if (result.source_sample_id != sample->sample_id) continue;
            if (std::visit([&](const auto& value) {
                    return QFileInfo(QString::fromStdString(value.image_path)).absoluteFilePath() == QFileInfo(image_path).absoluteFilePath();
                }, result.result)) {
                match = &result; break;
            }
        }
    }
    if (!match) return;

    if (classification) {
        classification_canvas_->set_heatmap(
            QImage(QString::fromStdString(std::get<jocelin::result::cls::ClsResult>(match->result).heatmap_path)));
    } else {
        segmentation_canvas_->set_prediction_overlay(prediction_overlay_from_result(
            std::get<jocelin::result::sg::SgResult>(match->result)));
    }
}

void InferenceWorkspace::commit_selection_scope() {
    if (rebuilding_ || !service_ || !images_) {
        return;
    }
    TaskNodeConfig& task = service_->task();
    const QList<QListWidgetItem*> selected = images_->selectedItems();
    const bool filtered = sample_split_filter_->currentData().toString() != "all" ||
        !sample_search_->text().trimmed().isEmpty();
    task.inference_use_all_images = selected.isEmpty() && !filtered;
    task.inference_images.clear();
    if (!selected.isEmpty()) {
        for (QListWidgetItem* item : selected) {
            task.inference_images.push_back(
                item->data(Qt::UserRole).toString().toStdString());
        }
    } else if (filtered) {
        for (int index = 0; index < images_->count(); ++index) {
            QListWidgetItem* item = images_->item(index);
            if (!item->isHidden()) {
                task.inference_images.push_back(
                    item->data(Qt::UserRole).toString().toStdString());
            }
        }
    }
    if (changed) {
        changed();
    }
}

void InferenceWorkspace::update_command_presentation() {
    if (device_type_) {
        device_type_->setEnabled(!busy_ && device_type_->count() > 0);
    }
    if (device_) {
        device_->setEnabled(!busy_ && device_->count() > 0);
    }
    const int selected = images_ ? images_->selectedItems().size() : 0;
    int total = 0;
    if (images_) {
        for (int index = 0; index < images_->count(); ++index) {
            if (!images_->item(index)->isHidden()) {
                ++total;
            }
        }
    }
    const bool filtered = sample_split_filter_->currentData().toString() != "all" ||
        !sample_search_->text().trimmed().isEmpty();
    QString scope;
    QString summary;
    if (total == 0) {
        summary = filtered ? tr("No matches") : tr("No Task images");
    } else if (selected == 0) {
        scope = filtered ? tr("all %1 matching images").arg(total)
                         : tr("all %1 images").arg(total);
        summary = filtered ? tr("%1 matching").arg(total)
                           : tr("All %1 images").arg(total);
    } else if (selected == 1) {
        scope = tr("the selected image");
        summary = tr("1 image selected");
    } else {
        scope = tr("%1 selected images").arg(selected);
        summary = tr("%1 selected").arg(selected);
    }
    if (scope_summary_) {
        scope_summary_->setText(summary);
    }
    if (!start_) {
        return;
    }
    start_->setText(tr("Infer"));
    start_->setAccessibleName(tr("Infer"));

    const ModelWeightSelection checked = model_browser_->checked_weights();
    const jocelin::runtime::ComputeDevice* compute = selected_device();
    const jocelin::model::ModelWeight* weight = selected_weight(checked);
    const bool available = compute && weight && inference_weight_available(*weight, compute->device);
    QString checked_label;
    if (weight) {
        const ProjectModel* model = service_->find_model(checked.front().first);
        checked_label = tr("%1 · %2").arg(QString::fromStdString(model->name), checked.front().second);
    } else if (checked.empty()) {
        checked_label = tr("No Weight branch checked");
    } else {
        checked_label = tr("%1 Weight branches checked").arg(checked.size());
    }
    checked_target_->setText(checked_label);
    checked_target_->setToolTip(checked_label);
    if (total == 0) {
        start_->setToolTip(filtered ? tr("No images match the current filters.")
                                    : tr("Add Task images before starting inference."));
    } else if (!weight) {
        start_->setToolTip(tr("Check exactly one Weight branch to run inference."));
    } else if (!compute) {
        start_->setToolTip(tr("Select an available CPU or GPU device."));
    } else if (!available) {
        start_->setToolTip(tr("The checked Weight branch requires its asset files and a compatible device and runtime."));
    } else {
        start_->setToolTip(
            tr("Run inference for %1 with %2 on %3.")
                .arg(scope,
                     weight_type_label(weight->format),
                     QString::fromStdString(compute->display_name)));
    }
    start_->setEnabled(!busy_ && total > 0 && available);
}

void InferenceWorkspace::request_start() {
    if (busy_ || !images_ || !start_ || !start_->isEnabled() || !model_browser_) {
        return;
    }
    const ModelWeightSelection checked = model_browser_->checked_weights();
    const jocelin::runtime::ComputeDevice* compute = selected_device();
    const jocelin::model::ModelWeight* weight = selected_weight(checked);
    if (!compute || !weight || !inference_weight_available(*weight, compute->device)) {
        return;
    }
    InferenceLaunchRequest request;
    request.model_id = checked.front().first;
    request.weight_id = checked.front().second;
    request.device = compute->device;
    commit_result_selection();
    commit_selection_scope();
    commit_device();
    if (start_requested) {
        start_requested(request);
    }
}

void InferenceWorkspace::set_busy(bool busy) {
    busy_ = busy;
    update_command_presentation();
    filter_panel_->setEnabled(!busy);
    model_browser_->setEnabled(!busy);
    images_->setEnabled(!busy);
    sample_split_filter_->setEnabled(!busy);
    sample_search_->setEnabled(!busy);
    device_type_->setEnabled(!busy_ && device_type_->count() > 0);
    device_->setEnabled(!busy_ && device_->count() > 0);
    batch_size_->setEnabled(!busy);
}

void InferenceWorkspace::begin() {
    progress_->setValue(0);
    current_image_->setText(tr("Waiting for first sample"));
    current_image_->setToolTip({});
    set_runtime_state(state_, QStringLiteral("running"));
}

void InferenceWorkspace::update_progress(
    size_t done, size_t total, const QString& image)
{
    set_runtime_state(state_, QStringLiteral("running"));
    progress_->setValue(total > 0
        ? qBound(0, static_cast<int>(done * 100 / total), 100) : 0);
    QString display_name = tr("Task image");
    if (service_) {
        int sample_index = -1;
        int image_index = -1;
        const auto* sample = task_sample_for_path(
            service_->binding(), image, &sample_index, &image_index);
        if (sample) {
            display_name = image_display_name(sample_index, *sample);
            if (sample->image_paths.size() > 1)
                display_name = tr("%1 · %2/%3")
                    .arg(display_name)
                    .arg(image_index + 1)
                    .arg(sample->image_paths.size());
        }
    }
    current_image_->setText(tr("%1 / %2 · %3")
        .arg(done)
        .arg(total)
        .arg(display_name));
    current_image_->setToolTip(image);
}

void InferenceWorkspace::finish(const QString& error) {
    set_busy(false);
    set_runtime_state(
        state_, error.isEmpty() ? QStringLiteral("completed")
                                : QStringLiteral("error"));
    current_image_->setText(error.isEmpty()
        ? tr("Completed · review in Annotation or Results")
        : error);
    if (error.isEmpty()) progress_->setValue(100);
    refresh();
}

ResultsWorkspace::ResultsWorkspace(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                             ThemeTokens::Space2, ThemeTokens::Space2);
    root->setSpacing(ThemeTokens::Space1);

    auto* actions = new QHBoxLayout;
    actions->addStretch();
    open_folder_ = new QPushButton(tr("Open folder"), this);
    open_folder_->setProperty("compact", true);
    open_folder_->setToolTip(tr("Open the persisted result directory"));
    export_button_ = new QPushButton(tr("Export"), this);
    export_button_->setObjectName("exportTaskResultsButton");
    export_button_->setProperty("accentSecondary", true);
    export_button_->setToolTip(tr("Export the selected persisted result"));
    actions->addWidget(open_folder_);
    actions->addWidget(export_button_);
    root->addLayout(actions);

    model_browser_ = new ProjectModelBrowser(this);
    model_browser_->setObjectName("resultsProjectModelBrowser");
    model_browser_->set_model_check_selection(true);
    model_browser_->set_management_actions_enabled(false);
    model_browser_->set_detail_visible(false);
    model_browser_->setMaximumHeight(165);
    const auto select_model = [this](const QString& id) {
        if (service_ && service_->selected_model_id() != id) {
            service_->select_inference_model(id);
            refresh();
        }
    };
    model_browser_->selection_changed = select_model;
    model_browser_->checked_model_changed = select_model;
    model_browser_->weight_branch_selection_changed = [this, select_model](const QString& id, const QString& weight) {
        if (weight.isEmpty()) {
            select_model(id);
        } else if (service_) {
            service_->select_inference_result(id, weight);
            refresh();
        }
    };
    model_browser_->set_weight_selection_mode(true);
    root->addWidget(model_browser_);
    weight_branch_ = new QComboBox(this);
    weight_branch_->setObjectName("resultsWeightBranch");
    root->addWidget(weight_branch_);
    connect(weight_branch_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!service_) {
            return;
        }
        service_->select_inference_result(service_->selected_model_id(), weight_branch_->currentData().toString());
        refresh();
    });


    auto* summary = new QFrame(this);
    summary_ = summary;
    summary->setObjectName("taskResultsSummary");
    summary->setProperty("sectionPanel", true);
    summary->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* summary_layout = new QGridLayout(summary);
    summary_layout->setContentsMargins(ThemeTokens::Space3, ThemeTokens::Space2,
                                       ThemeTokens::Space3, ThemeTokens::Space2);
    context_ = new QLabel(tr("No result selected"), summary);
    context_->setProperty("workspaceTitle", true);
    summary_layout->addWidget(context_, 0, 0, 1, 6);
    summary_layout->addWidget(new QLabel(tr("Precision"), summary), 1, 0);
    precision_ = new QLabel(QStringLiteral("—"), summary);
    summary_layout->addWidget(precision_, 1, 1);
    summary_layout->addWidget(new QLabel(tr("Recall"), summary), 1, 2);
    recall_ = new QLabel(QStringLiteral("—"), summary);
    summary_layout->addWidget(recall_, 1, 3);
    summary_layout->addWidget(new QLabel(tr("F1"), summary), 1, 4);
    f1_ = new QLabel(QStringLiteral("—"), summary);
    summary_layout->addWidget(f1_, 1, 5);
    benchmark_label_ = new QLabel(summary);
    summary_layout->addWidget(benchmark_label_, 2, 0);
    benchmark_ = new QLabel(QStringLiteral("—"), summary);
    summary_layout->addWidget(benchmark_, 2, 1);
    summary_layout->setColumnStretch(6, 1);
    root->addWidget(summary);

    state_ = helper_label({}, this);
    state_->setObjectName("taskResultsState");
    root->addWidget(state_);

    empty_state_ = new QFrame(this);
    empty_state_->setObjectName("taskResultsEmptyState");
    empty_state_->setProperty("emptyState", true);
    empty_state_->setMaximumHeight(44);
    auto* empty_layout = new QHBoxLayout(empty_state_);
    empty_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                                     ThemeTokens::Space2, ThemeTokens::Space1);
    auto* empty_copy = new QLabel(
        tr("No persisted result for this model and weight format."), empty_state_);
    empty_copy->setProperty("meta", true);
    empty_layout->addWidget(empty_copy);
    empty_layout->addStretch();
    root->addWidget(empty_state_);

    empty_filler_ = new QWidget(this);
    empty_filler_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    root->addWidget(empty_filler_, 1);

    auto* content_split = new CollapsibleSplitter(Qt::Horizontal, this);
    content_ = content_split;
    content_split->setObjectName("taskResultsContent");

    auto* samples_panel = new QFrame(content_split);
    samples_panel->setObjectName("resultsSamplesPanel");
    samples_panel->setMinimumWidth(240);
    auto* samples_layout = new QVBoxLayout(samples_panel);
    samples_layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                                       ThemeTokens::Space2, ThemeTokens::Space2);
    samples_layout->setSpacing(ThemeTokens::Space1);
    auto* samples_title = new QLabel(tr("SAMPLES"), samples_panel);
    samples_title->setProperty("eyebrow", true);
    samples_layout->addWidget(samples_title);
    table_ = new QTableWidget(samples_panel);
    table_->setObjectName("taskResultsTable");
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(
        {tr("Sample"), tr("Prediction"), tr("Confidence"), tr("Area (px)" )});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    samples_layout->addWidget(table_, 1);

    auto* viewport = new QFrame(content_split);
    viewport->setObjectName("resultsViewportPanel");
    auto* viewport_layout = new QVBoxLayout(viewport);
    viewport_layout->setContentsMargins(0, 0, 0, 0);
    viewport_layout->setSpacing(0);
    auto* layers_panel = new QFrame(viewport);
    layers_panel->setObjectName("resultsLayerBar");
    auto* layers = new QHBoxLayout(layers_panel);
    layers->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space1,
                               ThemeTokens::Space2, ThemeTokens::Space1);
    overlay_layer_ = new QCheckBox(tr("Inference"), layers_panel);
    overlay_layer_->setObjectName("resultsOverlayLayerCheck");
    overlay_layer_->setChecked(true);
    layers->addWidget(overlay_layer_);
    layers->addStretch();
    viewport_layout->addWidget(layers_panel);
    canvas_stack_ = new QStackedWidget(viewport);
    canvas_stack_->setObjectName("resultsCanvasStack");
    segmentation_canvas_ = new ImageResultCanvas(canvas_stack_);
    segmentation_canvas_->setObjectName("resultsSegmentationCanvas");
    segmentation_canvas_->set_prediction_visible(true);
    segmentation_canvas_->set_overlay_opacity(0.42);
    classification_canvas_ = new ClassificationCanvas(canvas_stack_);
    classification_canvas_->setObjectName("resultsClassificationCanvas");
    canvas_stack_->addWidget(segmentation_canvas_);
    canvas_stack_->addWidget(classification_canvas_);
    viewport_layout->addWidget(canvas_stack_, 1);

    auto* detail = new QFrame(content_split);
    detail->setObjectName("resultsDetailPanel");
    detail->setMinimumWidth(220);
    auto* detail_layout = new QVBoxLayout(detail);
    detail_layout->setContentsMargins(ThemeTokens::Space3, ThemeTokens::Space3,
                                      ThemeTokens::Space3, ThemeTokens::Space3);
    auto* detail_title = new QLabel(tr("DETAIL"), detail);
    detail_title->setProperty("eyebrow", true);
    detail_layout->addWidget(detail_title);
    sample_detail_ = new QLabel(tr("Select a result sample."), detail);
    sample_detail_->setWordWrap(true);
    sample_detail_->setProperty("meta", true);
    detail_layout->addWidget(sample_detail_);
    detail_layout->addStretch();

    content_split->addWidget(samples_panel);
    content_split->addWidget(viewport);
    content_split->addWidget(detail);
    content_split->setStretchFactor(0, 0);
    content_split->setStretchFactor(1, 1);
    content_split->setStretchFactor(2, 0);
    content_split->setCollapsible(0, true);
    content_split->setCollapsible(2, true);
    content_split->setHandleWidth(11);
    content_split->setSizes({360, 900, 260});
    content_split->handle(1)->setObjectName("resultsSamplesHandle");
    content_split->handle(1)->setProperty("collapsePanelIndex", 0);
    content_split->handle(1)->setProperty("restoreWidth", 360);
    content_split->handle(1)->setToolTip(tr("Drag to resize result samples; click to collapse or expand."));
    content_split->handle(2)->setObjectName("resultsDetailHandle");
    content_split->handle(2)->setProperty("collapsePanelIndex", 2);
    content_split->handle(2)->setProperty("restoreWidth", 260);
    content_split->handle(2)->setToolTip(tr("Drag to resize result details; click to collapse or expand."));
    root->addWidget(content_, 1);

    connect(table_, &QTableWidget::itemSelectionChanged, this,
            [this] { refresh_sample_detail(); });
    connect(overlay_layer_, &QCheckBox::toggled, this, [this](bool visible) {
        if (segmentation_canvas_) segmentation_canvas_->set_prediction_visible(visible);
        if (classification_canvas_) classification_canvas_->set_heatmap_visible(visible);
    });
    connect(export_button_, &QPushButton::clicked, this,
            [this] { export_results(); });
    connect(open_folder_, &QPushButton::clicked, this, [this] {
        const auto* result = selected_result_set();
        if (!result) return;
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QString::fromStdString(result->result_directory)));
    });
    UiMetrics::apply_semantic_metrics(this);
}

void ResultsWorkspace::set_service(TaskWorkspaceService* service) {
    if (service_ != service) {
        model_browser_->set_models(service ? models_ : std::vector<ProjectModel>{});
    }
    service_ = service;
    refresh();
}

void ResultsWorkspace::set_models(const std::vector<ProjectModel>& models) {
    models_ = models;
    model_browser_->set_models(service_ ? models_ : std::vector<ProjectModel>{});
    refresh();
}

const InferenceResultSet* ResultsWorkspace::selected_result_set() const {
    return service_ ? service_->selected_inference_result() : nullptr;
}

void ResultsWorkspace::refresh() {
    table_->setRowCount(0);
    const QSignalBlocker blocker(weight_branch_);
    weight_branch_->clear();
    selected_model_id_ = service_ ? service_->selected_model_id() : QString{};
    if (service_) {
        const auto* model = service_->find_model(selected_model_id_);
        if (model) {
            for (const auto& weight : model->weights)
                weight_branch_->addItem(QString::fromStdString(weight.id) + " · " + weight_type_label(weight.format),
                    QString::fromStdString(weight.id));
            const QSignalBlocker browser_blocker(model_browser_);
            const jocelin::model::ModelWeight* weight = jocelin::model::find_weight(*model, service_->selected_weight_id().toStdString());
            if (weight) {
                model_browser_->set_current_weight(selected_model_id_, weight->format, QString::fromStdString(weight->id));
            } else {
                model_browser_->set_current_model_id(selected_model_id_);
                model_browser_->set_checked_model_id(selected_model_id_);
            }
            weight_branch_->setCurrentIndex(weight_branch_->findData(service_->selected_weight_id()));
        }
    }
    refresh_selected_result();
}

void ResultsWorkspace::refresh_selected_result() {
    table_->setRowCount(0);
    if (segmentation_canvas_) segmentation_canvas_->clear();
    if (classification_canvas_) classification_canvas_->clear_canvas();
    sample_detail_->setText(tr("Select a result sample."));
    const auto* result_set = selected_result_set();
    const auto metric_text = [](bool available, double value) {
        return available ? QString::number(value, 'f', 4) : QStringLiteral("—");
    };

    QString model_name = selected_model_id_;
    for (const auto& model : models_)
        if (QString::fromStdString(model.id) == selected_model_id_) {
            model_name = QString::fromStdString(model.name);
            break;
        }
    const QString format = service_ ? service_->selected_weight_id() : QString{};
    context_->setText(selected_model_id_.isEmpty()
        ? tr("No result selected")
        : tr("%1 · %2").arg(model_name, format));

    const bool has_result = result_set != nullptr;
    summary_->setVisible(has_result);
    empty_state_->setVisible(!has_result);
    empty_filler_->setVisible(!has_result);
    content_->setVisible(has_result);
    open_folder_->setEnabled(has_result);
    export_button_->setEnabled(has_result);
    benchmark_label_->setText(service_ && service_->task().task == "cls" ? tr("Accuracy") : tr("Mean IoU"));
    benchmark_->setText(QStringLiteral("—"));
    if (!has_result) {
        precision_->setText(QStringLiteral("—"));
        recall_->setText(QStringLiteral("—"));
        f1_->setText(QStringLiteral("—"));
        state_->clear();
        state_->hide();
        return;
    }

    const bool classification = service_->task().task == "cls";
    if (classification) {
        if (result_set->aggregate_metrics.available)
            benchmark_->setText(QString::number(result_set->aggregate_metrics.accuracy, 'f', 4));
        precision_->setText(metric_text(
            result_set->aggregate_metrics.available,
            result_set->aggregate_metrics.precision));
        recall_->setText(metric_text(
            result_set->aggregate_metrics.available,
            result_set->aggregate_metrics.recall));
        f1_->setText(metric_text(
            result_set->aggregate_metrics.available,
            result_set->aggregate_metrics.f1));
    } else {
        precision_->setText(QStringLiteral("—"));
        recall_->setText(QStringLiteral("—"));
        f1_->setText(QStringLiteral("—"));
    }

    state_->show();
    state_->setText(
        tr("%1 sample result(s) · weight %2")
            .arg(result_set->samples.size())
            .arg(QString::fromStdString(result_set->weight_id)));
    table_->setRowCount(static_cast<int>(result_set->samples.size()));
    const auto& binding = service_->binding();
    for (int row = 0; row < static_cast<int>(result_set->samples.size()); ++row) {
        const auto& result = result_set->samples[static_cast<size_t>(row)];
        const int source_index = task_sample_index(binding, result.source_sample_id);
        QString source_name = QString::fromStdString(result.source_sample_id);
        const auto* cls = std::get_if<jocelin::result::cls::ClsResult>(&result.result);
        const auto* sg = std::get_if<jocelin::result::sg::SgResult>(&result.result);
        QString source_tooltip = QString::fromStdString(cls ? cls->image_path : sg->image_path);
        if (source_index >= 0) {
            const auto& source_sample = binding.samples[static_cast<size_t>(source_index)];
            source_name = image_display_name(source_index, source_sample);
            source_tooltip = sample_image_tooltip(
                source_name, source_sample, annotation_label(source_sample));
        }
        auto* sample = new QTableWidgetItem(source_name);
        sample->setToolTip(source_tooltip);
        sample->setData(Qt::UserRole, row);
        auto* prediction = new QTableWidgetItem(
            cls
                ? (cls->predicted_class == -1 ? tr("Unknown") : tr("Class %1").arg(cls->predicted_class))
                : tr("Segmentation"));
        auto* confidence = new QTableWidgetItem(
            QString::number(cls ? cls->confidence : sg->mean_confidence, 'f', 4));
        auto* area = new QTableWidgetItem(
            cls ? QStringLiteral("—")
                           : QString::number(sg->predicted_area));
        for (auto* item : {sample, prediction, confidence, area})
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 0, sample);
        table_->setItem(row, 1, prediction);
        table_->setItem(row, 2, confidence);
        table_->setItem(row, 3, area);
    }
    if (table_->rowCount() > 0) table_->selectRow(0);
}

void ResultsWorkspace::refresh_sample_detail() {
    const auto* result_set = selected_result_set();
    if (!result_set || table_->currentRow() < 0 ||
        table_->currentRow() >= static_cast<int>(result_set->samples.size())) {
        sample_detail_->setText(tr("Select a result sample."));
        if (segmentation_canvas_) segmentation_canvas_->clear();
        if (classification_canvas_) classification_canvas_->clear_canvas();
        return;
    }

    const auto& result =
        result_set->samples[static_cast<size_t>(table_->currentRow())];
    const auto* cls = std::get_if<jocelin::result::cls::ClsResult>(&result.result);
    const auto* sg = std::get_if<jocelin::result::sg::SgResult>(&result.result);
    const bool classification = cls != nullptr;
    if (sg) {
        benchmark_->setText(sg->metrics.available
            ? QString::number(sg->metrics.mean_iou, 'f', 4) : QStringLiteral("—"));
        precision_->setText(sg->metrics.available
            ? QString::number(sg->metrics.precision, 'f', 4) : QStringLiteral("—"));
        recall_->setText(sg->metrics.available
            ? QString::number(sg->metrics.recall, 'f', 4) : QStringLiteral("—"));
        f1_->setText(sg->metrics.available
            ? QString::number(sg->metrics.f1, 'f', 4) : QStringLiteral("—"));
    }
    sample_detail_->setText(classification
        ? tr("Prediction: %1\nConfidence: %2\nInterest: %3")
              .arg(cls->predicted_class == -1 ? tr("Unknown") : tr("Class %1").arg(cls->predicted_class))
              .arg(cls->confidence, 0, 'f', 4)
              .arg(cls->interest ? tr("Yes") : tr("No"))
        : tr("Confidence: %1\nPredicted area: %2 px")
              .arg(sg->mean_confidence, 0, 'f', 4).arg(sg->predicted_area));

    if (!canvas_stack_ || !segmentation_canvas_ || !classification_canvas_) return;
    const QString source = QString::fromStdString(cls ? cls->image_path : sg->image_path);
    canvas_stack_->setCurrentWidget(
        classification ? static_cast<QWidget*>(classification_canvas_)
                       : static_cast<QWidget*>(segmentation_canvas_));
    overlay_layer_->setText(classification ? tr("Heatmap") : tr("Inference"));

    if (classification) {
        const QImage image(source);
        if (image.isNull()) {
            classification_canvas_->clear_canvas();
            return;
        }
        classification_canvas_->set_image(image);
        classification_canvas_->set_heatmap(
            QImage(QString::fromStdString(cls->heatmap_path)));
        classification_canvas_->set_heatmap_visible(overlay_layer_->isChecked());
        return;
    }

    QString error;
    if (!segmentation_canvas_->load(source, &error)) {
        segmentation_canvas_->clear();
        return;
    }
    segmentation_canvas_->set_prediction_overlay(prediction_overlay_from_result(*sg));
}

void ResultsWorkspace::export_results() {
    const auto* result_set = selected_result_set();
    if (!result_set) return;
    const QString selected = QFileDialog::getExistingDirectory(
        this, tr("Export selected result"));
    if (selected.isEmpty()) return;
    const QDir destination(selected);
    std::set<QString> sources;
    const QString manifest =
        QDir(QString::fromStdString(result_set->result_directory)).filePath("result.json");
    if (QFileInfo(manifest).isFile()) sources.insert(manifest);
    for (const auto& result : result_set->samples) {
        std::visit([&](const auto& task_result) {
            std::vector<std::string> paths{task_result.metadata_path};
            using Result = std::decay_t<decltype(task_result)>;
            if constexpr (std::is_same_v<Result, jocelin::result::sg::SgResult>) {
                paths.push_back(task_result.mask_path);
                paths.push_back(task_result.visualization_path);
            } else {
                paths.push_back(task_result.heatmap_path);
            }
            for (const auto& source : paths) {
                const QString path = QString::fromStdString(source);
                if (QFileInfo(path).isFile()) sources.insert(path);
            }
        }, result.result);
    }
    int copied = 0;
    for (const auto& path : sources)
        copied += QFile::copy(path, unique_destination(destination, path)) ? 1 : 0;
    state_->setText(
        tr("Exported %1 file(s) to %2.").arg(copied).arg(selected));
}

} // namespace studio
