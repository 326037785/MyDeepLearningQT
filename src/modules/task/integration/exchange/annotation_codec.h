#pragma once

#include "modules/workflow/model/workflow.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace studio {

// Format-neutral annotation data. Codecs only describe external data; they do
// not mutate a WorkflowGraph or construct UI/application state.
struct AnnotationCategoryRecord {
    QString external_id;
    int mask_value = 0;
    QString name;
};

struct AnnotationSampleRecord {
    QString external_id;
    QStringList image_paths;
    QString mask_path;
    QString split = QStringLiteral("unassigned");
};

struct AnnotationDocument {
    QString format_id;
    QString source;
    QString source_root;
    int bucket_capacity = 1;
    std::vector<AnnotationCategoryRecord> categories;
    std::vector<AnnotationSampleRecord> samples;
    QStringList warnings;
};

class IAnnotationCodec {
public:
    virtual ~IAnnotationCodec() = default;

    virtual QString format_id() const = 0;
    virtual int probe(const QString& source) const = 0;
    virtual AnnotationDocument read(const QString& source) const = 0;
};

class AnnotationCodecRegistry final {
public:
    void add(std::unique_ptr<IAnnotationCodec> codec);
    AnnotationDocument read_auto(const QString& source) const;

private:
    std::vector<std::unique_ptr<IAnnotationCodec>> codecs_;
};

struct AnnotationImportPackage {
    DatasetConfig dataset;
    TaskDatasetBinding binding;
    QString format_id;
    QString source;
    QStringList warnings;
};

class AnnotationClassResolver final {
public:
    static std::vector<AnnotationClass> resolve(
        const AnnotationDocument& document);
};

class AnnotationBindingMapper final {
public:
    static AnnotationImportPackage map(
        AnnotationDocument document);
};

std::unique_ptr<IAnnotationCodec> make_dense_mask_codec();
AnnotationCodecRegistry make_default_annotation_codec_registry();

} // namespace studio
