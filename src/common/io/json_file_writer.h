#pragma once
#include <QString>
class QJsonObject;
namespace studio {
void write_json_file(const QString& path, const QJsonObject& object);
} // namespace studio
