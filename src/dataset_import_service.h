#pragma once

#include "workflow.h"

#include <QStringList>

namespace studio {

struct DatasetImportResult {
    DatasetConfig config;
    int added = 0;
    int duplicate = 0;
    QString error;
};

class DatasetImportService final {
public:
    static DatasetImportResult import_paths(
        const DatasetConfig& current, const QStringList& paths, bool append);
    static DatasetConfig cleared(const DatasetConfig& current);
    static QStringList valid_paths(const DatasetConfig& config);
};

} // namespace studio
