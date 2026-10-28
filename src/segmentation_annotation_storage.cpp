#include "segmentation_annotation_storage.h"
#include "project_storage.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace studio {
QByteArray encode_segmentation_regions(const std::vector<jocelin::geo::Region>& regions) {
    QJsonArray data;
    for (const auto& region : regions) {
        QJsonArray runs;
        for (const auto& run : region.vRun)
            runs.append(QJsonArray{run.row, run.start, run.end, run.label});
        data.append(runs);
    }
    return QJsonDocument(data).toJson(QJsonDocument::Compact);
}

std::vector<jocelin::geo::Region> load_segmentation_regions(const QString& path) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(ProjectStorage::read(path), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray())
        throw std::runtime_error("invalid Node segmentation region asset");
    std::vector<jocelin::geo::Region> regions;
    for (const auto& value : document.array()) {
        if (!value.isArray()) throw std::runtime_error("region must contain a run array");
        jocelin::geo::Region region;
        for (const auto& entry : value.toArray()) {
            if (!entry.isArray() || entry.toArray().size() != 4)
                throw std::runtime_error("run must contain row, start, end, label");
            int coordinates[4];
            const auto run = entry.toArray();
            for (int index = 0; index < 4; ++index) {
                const double number = run[index].toDouble(-1);
                if (!run[index].isDouble() || !std::isfinite(number) || std::trunc(number) != number ||
                    number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max())
                    throw std::runtime_error("run coordinates must be integers");
                coordinates[index] = static_cast<int>(number);
            }
            region.vRun.emplace_back(coordinates[0], coordinates[1], coordinates[2], coordinates[3]);
        }
        regions.push_back(std::move(region));
    }
    return regions;
}
} // namespace studio
