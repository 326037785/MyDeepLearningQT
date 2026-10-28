#pragma once

#include "workflow.h"

#include <QString>
#include <QStringList>

namespace studio {

enum class AnnotationExchangeFormat {
    StudioNative,
    ILabelledSegmentation
};

struct AnnotationImportResult {
    DatasetConfig dataset;
    TaskDatasetBinding binding;
    // Open string identifier supplied by the selected codec. Import formats
    // are deliberately not a closed enum so future codecs can be registered
    // without changing the application-domain API.
    QString format_id;
    QString source;
    QStringList warnings;
};

struct AnnotationImportStats {
    int image_count = 0;
    int mask_count = 0;
    int unannotated_count = 0;
    int class_count = 0;
    int review_count = 0;
};

class AnnotationExchange final {
public:
    static AnnotationImportResult import_auto(const QString& source);
    static AnnotationImportStats import_stats(
        const AnnotationImportResult& imported);
    static QString export_task(const TaskDatasetBinding& binding,
                               const QString& destination,
                               AnnotationExchangeFormat format);
    static QString format_name(AnnotationExchangeFormat format);
};

} // namespace studio
