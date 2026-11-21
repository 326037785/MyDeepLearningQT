#pragma once

#include <common/config/runtime_route.h>
#include <QSplitter>
#include <QString>

#include <string>

class QLabel;

namespace studio {

struct TaskSampleBinding;

class CollapsibleSplitter final : public QSplitter {
public:
    explicit CollapsibleSplitter(Qt::Orientation orientation, QWidget* parent = nullptr);

protected:
    QSplitterHandle* createHandle() override;
};

namespace task_page {

QLabel* helper_label(const QString& text, QWidget* parent);
QWidget* metric_card(const QString& caption, QLabel*& value, QWidget* parent);
QString weight_type_label(jocelin::config::WeightType type);
void set_runtime_state(QLabel* label, const QString& value);

} // namespace task_page

} // namespace studio
