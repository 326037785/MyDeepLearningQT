#pragma once
#include <common/geometry/run.h>
#include <QByteArray>
#include <QString>

namespace studio {
QByteArray encode_segmentation_regions(const std::vector<jocelin::geo::Region>& regions);
std::vector<jocelin::geo::Region> load_segmentation_regions(const QString& path);
} // namespace studio
