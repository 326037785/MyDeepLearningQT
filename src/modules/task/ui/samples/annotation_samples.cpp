#include "modules/task/ui/samples/annotation_samples.h"

#include "modules/task/presentation/sample_presentation.h"

#include <QFileInfo>
#include <QImageReader>

namespace studio {

bool annotation_sample_visible(const TaskSampleBinding& sample) {
    if (!sample.selected || sample.image_paths.empty()) {
        return false;
    }
    const QString path = QString::fromStdString(sample.image_paths.front());
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    return QFileInfo(path).isFile() && reader.canRead();
}

SampleBrowserEntry annotation_sample_entry(const TaskSampleBinding& sample,
                                           int sample_index, int source_index,
                                           const QString& state) {
    const QString path = QString::fromStdString(sample.image_paths.front());
    const QString name = image_display_name(sample_index, sample);
    const QString annotation = state.isEmpty() ? annotation_label(sample) : state;
    return {sample.sample_id, path, name, annotation, split_label(sample.split),
        sample_image_tooltip(name, sample, annotation), source_index};
}

} // namespace studio
