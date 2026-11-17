#include "modules/task/ui/pages/augmentation_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace studio {

namespace {

QString title_from_id(const std::string& id) {
    QString result = QString::fromStdString(id);
    result.replace('_', ' ');
    if (!result.isEmpty()) result[0] = result[0].toUpper();
    return result;
}

void clear_layout(QLayout* layout) {
    while (auto* item = layout->takeAt(0)) {
        if (item->layout()) clear_layout(item->layout());
        delete item->widget();
        delete item;
    }
}

} // namespace

AugmentationDialog::AugmentationDialog(
    jocelin::config::ToolType task,
    const jocelin::config::Augment& current,
    QWidget* parent)
    : QDialog(parent), task_(task), draft_(current),
      descriptors_(jocelin::config::AugmentCatalog::describe(task))
{
    setObjectName("trainingAugmentationDialog");
    setWindowTitle(tr("Data Augmentation"));
    setMinimumSize(780, 460);

    auto* root = new QVBoxLayout(this);

    auto* probability_row = new QHBoxLayout;
    probability_row->addWidget(new QLabel(tr("Probability"), this));
    probability_ = new QDoubleSpinBox(this);
    probability_->setObjectName("augmentationProbability");
    const auto probability_descriptor =
        jocelin::config::AugmentCatalog::probability_descriptor();
    probability_->setRange(
        probability_descriptor.minimum, probability_descriptor.maximum);
    probability_->setSingleStep(probability_descriptor.step);
    probability_->setDecimals(probability_descriptor.decimals);
    probability_->setValue(draft_.probability);
    probability_row->addWidget(probability_);
    probability_row->addStretch();
    root->addLayout(probability_row);
    connect(probability_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                draft_.probability = static_cast<float>(value);
            });

    auto* columns = new QHBoxLayout;
    operations_ = new QTreeWidget(this);
    operations_->setObjectName("augmentationOperatorCatalog");
    operations_->setColumnCount(1);
    operations_->setHeaderHidden(true);
    operations_->setRootIsDecorated(false);
    operations_->setMinimumWidth(400);
    for (const auto& descriptor : descriptors_) {
        auto* item = new QTreeWidgetItem(operations_);
        item->setText(0, title_from_id(descriptor.id));
        item->setData(0, Qt::UserRole, QString::fromStdString(descriptor.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        item->setCheckState(0,
            jocelin::config::AugmentCatalog::enabled(draft_, descriptor.id)
                ? Qt::Checked : Qt::Unchecked);
    }

    auto* parameters = new QWidget(this);
    parameters->setObjectName("augmentationParameters");
    parameters_layout_ = new QVBoxLayout(parameters);

    columns->addWidget(operations_, 3);
    columns->addWidget(parameters, 2);
    root->addLayout(columns, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Apply)->setObjectName("augmentationApply");
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, [this] { apply(); });
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    root->addWidget(buttons);

    connect(operations_, &QTreeWidget::itemChanged,
            this, [this](QTreeWidgetItem* item, int column) {
                if (column != 0) return;
                const auto id = item->data(0, Qt::UserRole).toString();
                if (id.isEmpty()) return;
                jocelin::config::AugmentCatalog::set_enabled(
                    draft_, id.toStdString(), item->checkState(0) == Qt::Checked);
            });
    connect(operations_, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                if (!current) return;
                const auto id = current->data(0, Qt::UserRole).toString();
                if (!id.isEmpty()) render_parameters(id.toStdString());
            });

    if (operations_->topLevelItemCount() > 0) {
        operations_->setCurrentItem(operations_->topLevelItem(0));
    }
}

void AugmentationDialog::render_parameters(const std::string& operator_id) {
    clear_layout(parameters_layout_);
    const auto found = std::find_if(
        descriptors_.begin(), descriptors_.end(),
        [&](const auto& descriptor) { return descriptor.id == operator_id; });
    if (found == descriptors_.end()) return;

    auto* title = new QLabel(title_from_id(found->id), this);
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    parameters_layout_->addWidget(title);

    if (found->parameters.empty()) {
        parameters_layout_->addStretch();
        return;
    }

    auto* editor = new QWidget(this);
    auto* form = new QFormLayout(editor);
    for (const auto& parameter : found->parameters) {
        const auto current = jocelin::config::AugmentCatalog::parameter_value(
            draft_, found->id, parameter.id);
        if (parameter.kind == jocelin::config::AugmentParameterKind::Boolean) {
            auto* box = new QCheckBox(editor);
            box->setObjectName(QString::fromStdString(parameter.id));
            box->setChecked(std::get<bool>(current));
            connect(box, &QCheckBox::toggled, this,
                    [this, operator_id, id = parameter.id](bool checked) {
                        jocelin::config::AugmentCatalog::set_parameter_value(
                            draft_, operator_id, id, checked, true);
                    });
            form->addRow(title_from_id(parameter.id), box);
        } else if (parameter.kind == jocelin::config::AugmentParameterKind::Integer) {
            auto* box = new QSpinBox(editor);
            box->setObjectName(QString::fromStdString(parameter.id));
            box->setRange(static_cast<int>(parameter.minimum),
                          static_cast<int>(parameter.maximum));
            box->setSingleStep(static_cast<int>(parameter.step));
            box->setValue(std::get<int>(current));
            connect(box, qOverload<int>(&QSpinBox::valueChanged), this,
                    [this, operator_id, id = parameter.id](int value) {
                        jocelin::config::AugmentCatalog::set_parameter_value(
                            draft_, operator_id, id, value, true);
                    });
            form->addRow(title_from_id(parameter.id), box);
        } else if (parameter.kind == jocelin::config::AugmentParameterKind::Float) {
            auto* box = new QDoubleSpinBox(editor);
            box->setObjectName(QString::fromStdString(parameter.id));
            box->setRange(parameter.minimum, parameter.maximum);
            box->setSingleStep(parameter.step);
            box->setDecimals(parameter.decimals);
            box->setValue(std::get<double>(current));
            connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                    [this, operator_id, id = parameter.id](double value) {
                        jocelin::config::AugmentCatalog::set_parameter_value(
                            draft_, operator_id, id, value, true);
                    });
            form->addRow(title_from_id(parameter.id), box);
        } else if (parameter.kind == jocelin::config::AugmentParameterKind::IntervalVector) {
            const auto intervals =
                std::get<std::vector<jocelin::geo::Interval>>(current);
            for (size_t index = 0; index < intervals.size(); ++index) {
                auto* row = new QWidget(editor);
                auto* row_layout = new QHBoxLayout(row);
                row_layout->setContentsMargins(0, 0, 0, 0);
                auto make_endpoint = [&](double value, bool low) {
                    auto* box = new QDoubleSpinBox(row);
                    box->setObjectName(
                        QString::fromStdString(parameter.id) +
                        (low ? "Low" : "High") + QString::number(index));
                    box->setRange(parameter.minimum, parameter.maximum);
                    box->setSingleStep(parameter.step);
                    box->setDecimals(parameter.decimals);
                    box->setValue(value);
                    connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                            [this, operator_id, id = parameter.id, index, low](double endpoint) {
                                auto value = std::get<std::vector<jocelin::geo::Interval>>(
                                    jocelin::config::AugmentCatalog::parameter_value(
                                        draft_, operator_id, id));
                                if (low) value[index].low = endpoint;
                                else value[index].high = endpoint;
                                jocelin::config::AugmentCatalog::set_parameter_value(
                                    draft_, operator_id, id, std::move(value), true);
                            });
                    return box;
                };
                row_layout->addWidget(new QLabel(tr("Low"), row));
                row_layout->addWidget(make_endpoint(intervals[index].low, true));
                row_layout->addWidget(new QLabel(tr("High"), row));
                row_layout->addWidget(make_endpoint(intervals[index].high, false));
                form->addRow(
                    intervals.size() == 1
                        ? title_from_id(parameter.id)
                        : tr("%1 [%2]").arg(title_from_id(parameter.id)).arg(index),
                    row);
            }
        }
    }
    parameters_layout_->addWidget(editor);
    parameters_layout_->addStretch();
}

void AugmentationDialog::apply() {
    try {
        jocelin::config::AugmentCatalog::validate(draft_, task_);
        accept();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Invalid augmentation"),
                             QString::fromUtf8(error.what()));
    }
}

} // namespace studio
