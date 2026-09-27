#pragma once

class QWidget;

namespace studio {

struct ThemeTokens {
    static constexpr int Space1 = 4;
    static constexpr int Space2 = 8;
    static constexpr int Space3 = 12;
    static constexpr int Space4 = 16;
    static constexpr int Space5 = 24;
};

struct UiMetricSet {
    int control_height = 28;
    int compact_control_height = 24;
    int icon_button_extent = 28;
    int page_margin = 12;
    int section_gap = 12;
};

class UiMetrics final {
public:
    static UiMetricSet for_widget(const QWidget* widget);
    static void apply_semantic_metrics(QWidget* root);
};

} // namespace studio
