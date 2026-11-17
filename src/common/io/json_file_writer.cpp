#include "common/io/json_file_writer.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
namespace studio {
void write_json_file(const QString& path, const QJsonObject& object) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
}
} // namespace studio
