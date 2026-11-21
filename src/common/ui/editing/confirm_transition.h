#pragma once

#include <functional>

class QWidget;

namespace studio {

// The editor owns its state and persistence; this component only decides how
// to finish a pending edit before its caller continues a transition.
bool confirm_transition(QWidget* parent, bool dirty, bool auto_save,
                        const std::function<bool()>& save,
                        const std::function<void()>& discard);

} // namespace studio
