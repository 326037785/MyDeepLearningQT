#pragma once

#include <QString>

#include <stdexcept>

namespace studio {

inline QString model_scale_name(const QString& code) {
    if (code == "n") return QStringLiteral("Nano");
    if (code == "s") return QStringLiteral("Small");
    if (code == "m") return QStringLiteral("Medium");
    if (code == "l") return QStringLiteral("Large");
    if (code == "x") return QStringLiteral("Extra Large");
    throw std::runtime_error("unsupported model scale code: " + code.toStdString());
}

inline QString model_scale_label(const QString& code) {
    return model_scale_name(code) + QStringLiteral(" (") + code + QStringLiteral(")");
}

} // namespace studio
