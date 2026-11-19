#include "common/ui/ui_metrics.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFontMetrics>
#include <QLayout>
#include <QStyle>
#include <QToolBar>
#include <QWidget>

#include <algorithm>

namespace studio {

UiMetricSet UiMetrics::for_widget(const QWidget* widget) {
    const QFontMetrics font(widget ? widget->font() : QApplication::font());
    const QStyle* style = widget ? widget->style() : QApplication::style();
    const int frame = style ? style->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, widget) : 1;
    UiMetricSet result;
    result.control_height = std::max(26, font.height() + 10 + frame * 2);
    result.compact_control_height = std::max(22, font.height() + 5 + frame * 2);
    result.icon_button_extent = std::max(26, font.height() + 9);
    result.page_margin = std::max(10, font.height() - 2);
    result.section_gap = std::max(10, font.height() - 1);
    return result;
}

void UiMetrics::apply_semantic_metrics(QWidget* root) {
    if (!root) return;
    const auto metrics = for_widget(root);
    const auto buttons = root->findChildren<QAbstractButton*>();
    for (auto* button : buttons) {
        if (button->property("iconTool").toBool()) {
            button->setMinimumSize(metrics.icon_button_extent, metrics.icon_button_extent);
            button->setMaximumHeight(metrics.icon_button_extent);
        } else if (button->property("compact").toBool()) {
            button->setMinimumHeight(metrics.compact_control_height);
            button->setMaximumHeight(metrics.compact_control_height + 4);
        }
    }
}

void UiMetrics::apply_toolbar_spacing(QToolBar& toolbar, bool compact) {
    QLayout* layout = toolbar.layout();
    const QMargins margins = layout->contentsMargins();
    // Qt's toolbar layout initializes all four margins from the same style metric.
    const int vertical_margin = compact ? 2 : margins.left();
    layout->setContentsMargins(margins.left(), vertical_margin, margins.right(), vertical_margin);
}

} // namespace studio
