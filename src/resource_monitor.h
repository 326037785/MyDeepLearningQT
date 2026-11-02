#pragma once

#include <QFrame>

namespace studio {

class ResourceMonitorWidget final : public QFrame {
public:
    explicit ResourceMonitorWidget(QWidget* parent = nullptr);
};

} // namespace studio
