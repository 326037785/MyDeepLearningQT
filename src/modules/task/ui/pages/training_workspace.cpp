#include "modules/task/ui/pages/training_workspace.h"

#include "common/ui/icon_provider.h"
#include "common/ui/ui_metrics.h"
#include "modules/task/application/annotation/classification_annotation_state.h"
#include "modules/task/application/catalog/model_spec_catalog.h"
#include "modules/task/application/task_workspace_service.h"
#include "modules/task/ui/common/model_presentation.h"
#include "modules/task/ui/common/project_model_browser.h"
#include "modules/task/ui/common/resource_monitor.h"
#include "modules/task/ui/pages/augmentation_dialog.h"
#include "modules/task/ui/pages/task_page_ui.h"

#include <checkpoint/checkpoint_catalog.h>
#include <common/geometry/interval.h>
#include <training/training_service.h>

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace studio {

using task_page::helper_label;
using task_page::set_runtime_state;

class LossChart final : public QWidget {
public:
    explicit LossChart(QWidget* parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void reset(const jocelin::model::ModelEntry* model = nullptr) {
        points_.clear();
        if (model && !model->asset_directory.empty()) {
            QFile file(QDir(QString::fromStdString(model->asset_directory)).filePath(
                QString::fromLatin1(jocelin::training::training_history_filename)));
            if (file.open(QIODevice::ReadOnly) && file.readLine().trimmed() == jocelin::training::training_history_header) {
                while (!file.atEnd()) {
                    const QByteArray line = file.readLine();
                    if (!line.endsWith('\n')) {
                        break;
                    }
                    const QList<QByteArray> fields = line.trimmed().split(',');
                    if (fields.size() != 6) {
                        continue;
                    }
                    bool valid_epoch = false;
                    bool valid_loss = false;
                    bool valid_metric = false;
                    jocelin::train::TrainingProgress point;
                    point.completed_epochs = fields[0].toInt(&valid_epoch);
                    point.loss = fields[1].toDouble(&valid_loss);
                    point.validation_metric_name = fields[3].toStdString();
                    point.training_metric = fields[4].toDouble(&valid_metric);
                    if (!valid_epoch || point.completed_epochs < 1 ||
                        (!points_.empty() && point.completed_epochs <= points_.back().completed_epochs) ||
                        !valid_loss || !std::isfinite(point.loss) ||
                        !valid_metric || !std::isfinite(point.training_metric)) {
                        continue;
                    }
                    if (!fields[2].isEmpty() || !fields[5].isEmpty()) {
                        const double validation_loss = fields[2].toDouble(&valid_loss);
                        point.validation_metric = fields[5].toDouble(&valid_metric);
                        if (!valid_loss || !std::isfinite(validation_loss) ||
                            !valid_metric || !std::isfinite(point.validation_metric)) {
                            continue;
                        }
                        point.validation_loss = validation_loss;
                    }
                    points_.push_back(std::move(point));
                }
            }
        }
        update();
    }

    void add_point(const jocelin::train::TrainingProgress& point) {
        if (points_.empty() || point.completed_epochs > points_.back().completed_epochs) {
            points_.push_back(point);
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#F8FAFA"));
        const QRectF plot = rect().adjusted(52, 34, -52, -47);
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
        if (points_.empty()) {
            painter.setPen(QColor("#718386"));
            painter.drawText(plot, Qt::AlignCenter, tr("No training history"));
            return;
        }

        double minimum_loss = points_.front().loss;
        double maximum_loss = minimum_loss;
        for (const jocelin::train::TrainingProgress& point : points_) {
            const double validation_loss = point.validation_loss.value_or(point.loss);
            minimum_loss = std::min({minimum_loss, point.loss, validation_loss});
            maximum_loss = std::max({maximum_loss, point.loss, validation_loss});
        }
        if (maximum_loss - minimum_loss < 1e-12) {
            const double pad = std::max(1e-6, std::abs(maximum_loss) * 0.1);
            minimum_loss -= pad;
            maximum_loss += pad;
        }
        const jocelin::geo::Interval loss_range{minimum_loss, maximum_loss};
        const jocelin::geo::Interval metric_range{0.0, 1.0};
        const int first_epoch = points_.front().completed_epochs;
        const int last_epoch = points_.back().completed_epochs;
        const QColor loss_color("#27879B");
        const QColor metric_color("#A74720");
        const auto draw_series = [&](const auto& value_at,
                                     const jocelin::geo::Interval& range,
                                     const QPen& pen) {
            const double span = std::max(1e-12, range.high - range.low);
            QPainterPath path;
            QPolygonF samples;
            int previous_epoch = 0;
            for (const jocelin::train::TrainingProgress& point : points_) {
                const std::optional<double> value = value_at(point);
                if (!value) {
                    continue;
                }
                const qreal x = first_epoch == last_epoch
                    ? plot.center().x()
                    : plot.left() + plot.width() * (point.completed_epochs - first_epoch) / (last_epoch - first_epoch);
                const qreal normalized = std::clamp(
                    (*value - range.low) / span, 0.0, 1.0);
                const QPointF sample(x, plot.bottom() - plot.height() * normalized);
                if (previous_epoch != 0 && point.completed_epochs - previous_epoch == 1) {
                    path.lineTo(sample);
                } else {
                    path.moveTo(sample);
                }
                samples.append(sample);
                previous_epoch = point.completed_epochs;
            }
            painter.setPen(pen);
            painter.drawPath(path);
            painter.drawPoints(samples);
        };

        draw_series([](const auto& point) { return point.loss; }, loss_range, QPen(loss_color, 2));
        draw_series([](const auto& point) { return point.validation_loss; }, loss_range, QPen(loss_color, 2, Qt::DashLine));
        draw_series([](const auto& point) { return point.training_metric; }, metric_range, QPen(metric_color, 2));
        draw_series([](const auto& point) -> std::optional<double> {
            return point.validation_loss ? std::optional<double>(point.validation_metric) : std::nullopt;
        }, metric_range, QPen(metric_color, 2, Qt::DashLine));

        painter.setPen(QColor("#718386"));
        painter.drawText(QRectF(4, plot.top() - 7, 44, 16), Qt::AlignRight,
                         QString::number(loss_range.high, 'g', 4));
        painter.drawText(QRectF(4, plot.bottom() - 8, 44, 16), Qt::AlignRight,
                         QString::number(loss_range.low, 'g', 4));
        painter.drawText(QRectF(plot.right() + 5, plot.top() - 7, 44, 16), Qt::AlignLeft,
                         QString::number(metric_range.high, 'g', 4));
        painter.drawText(QRectF(plot.right() + 5, plot.bottom() - 8, 44, 16), Qt::AlignLeft,
                         QString::number(metric_range.low, 'g', 4));
        const QRectF epoch_labels(plot.left(), plot.bottom() + 3, plot.width(), 16);
        painter.drawText(epoch_labels, first_epoch == last_epoch ? Qt::AlignHCenter : Qt::AlignLeft,
                         QString::number(first_epoch));
        if (first_epoch != last_epoch) {
            painter.drawText(epoch_labels, Qt::AlignRight, QString::number(last_epoch));
        }
        painter.drawText(QRectF(plot.left(), plot.bottom() + 21, plot.width(), 18), Qt::AlignCenter,
                         tr("Epoch · Train (solid) · Validation (dashed)"));
        painter.setPen(loss_color);
        painter.drawText(QRectF(plot.left(), 21, plot.width(), 16), Qt::AlignLeft, tr("Loss"));
        painter.setPen(metric_color);
        painter.drawText(QRectF(plot.left(), 21, plot.width(), 16), Qt::AlignRight,
                         QString::fromStdString(points_.back().validation_metric_name));
    }

private:
    std::vector<jocelin::train::TrainingProgress> points_;
};

namespace {

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

QLabel* title_label(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setProperty("sectionTitle", true);
    return label;
}

QFrame* divider(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setProperty("sectionDivider", true);
    return line;
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

} // namespace

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
        if (!rebuilding_ && service_) {
            service_->select_inference_model(id);
            const jocelin::model::ModelEntry* active = controlled_model();
            if (!active || active->training_status != jocelin::model::ModelTrainingStatus::Running) {
                chart_->reset(model_browser_->current_model());
            }
        }
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
                [&model_id](const jocelin::model::ModelEntry& value) {
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
    if (service_) {
        refresh();
    } else {
        chart_->reset();
    }
}

void TrainingWorkspace::set_models(
    const std::vector<jocelin::model::ModelEntry>& models)
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
    std::vector<jocelin::model::ModelEntry> task_models;
    if (service_) {
        const auto& task = service_->task();
        const auto task_type = jocelin::config::tool_type_from_string(task.task);
        for (const auto& model : models_)
            if (model.task == task_type)
                task_models.push_back(model);
    }
    model_browser_->set_models(task_models);
    const jocelin::model::ModelEntry* active = controlled_model();
    if (!active || active->training_status != jocelin::model::ModelTrainingStatus::Running) {
        chart_->reset(model_browser_->current_model());
    }
    update_model_compatibility();
}

const jocelin::model::ModelEntry* TrainingWorkspace::controlled_model() const {
    return service_ ? service_->find_model(QString::fromStdString(service_->task().runtime.training_model_id)) : nullptr;
}

QString TrainingWorkspace::training_source_mode() const {
    if (!service_ || !model_browser_) return "new";
    const QString id = model_browser_->checked_model_id();
    if (id.isEmpty()) return "new";
    const auto found = std::find_if(
        models_.begin(), models_.end(),
        [&id](const jocelin::model::ModelEntry& value) {
            return QString::fromStdString(value.id) == id;
        });
    if (found == models_.end()) return "new";

    std::vector<jocelin::model::ModelClass> current_classes;
    const bool classification = service_->task().task == "cls";
    if (classification) {
        for (const auto* value : classification_classes(service_->binding().classes))
            current_classes.push_back({value->id, value->name, 0});
    } else {
        for (const AnnotationClass& value : service_->binding().classes) {
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
        [&id](const jocelin::model::ModelEntry& value) {
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
            [&request](const jocelin::model::ModelEntry& value) {
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
    const jocelin::model::ModelEntry* model = controlled_model();
    if (model) {
        set_models(service_->models());
        model_browser_->set_current_model_id(QString::fromStdString(model->id));
    }
    chart_->reset(model);
    const int completed_epochs = model ? model->completed_epochs : 0;
    progress_->setValue(qBound(0, completed_epochs * 100 / epochs_->value(), 100));
    epoch_->setText(tr("Epoch %1 / %2").arg(completed_epochs).arg(epochs_->value()));
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
    chart_->add_point(value);
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

} // namespace studio
