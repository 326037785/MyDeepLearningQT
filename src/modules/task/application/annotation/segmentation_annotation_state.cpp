#include "modules/task/application/annotation/segmentation_annotation_state.h"

#include <common/processor/processor.h>
#include <QImageReader>
#include <algorithm>
#include <set>

namespace studio {
namespace {

bool valid_target(const std::vector<AnnotationClass>& classes, TaskSampleBinding& sample) {
    auto& issues = sample.quality_issues;
    issues.erase(std::remove_if(issues.begin(), issues.end(), [](const auto& value) {
        return value == "segmentation target is outside image bounds" ||
            value == "segmentation target contains undefined class values" ||
            value == "mask is unreadable" || value == "mask dimensions do not match the sample image";
    }), issues.end());
    const QSize dimensions = sample.image_paths.empty() ? QSize{}
        : QImageReader(QString::fromStdString(sample.image_paths.front())).size();
    std::set<int> defined;
    for (const auto& value : classes) defined.insert(value.mask_value);
    bool bounds = true, labels = true;
    for (const auto& region : sample.regions)
        for (const auto& run : region.vRun) {
            bounds = bounds && run.row >= 0 && run.start >= 0 && run.end > run.start &&
                (!dimensions.isValid() || (run.row < dimensions.height() && run.end <= dimensions.width()));
            labels = labels && run.label > 0 && run.label <= 255 && defined.count(run.label) != 0;
        }
    if (!bounds) issues.push_back("segmentation target is outside image bounds");
    if (!labels) issues.push_back("segmentation target contains undefined class values");
    return bounds && labels;
}

bool has_foreground(const TaskSampleBinding& sample) {
    return std::any_of(sample.regions.begin(), sample.regions.end(),
        [](const auto& region) { return !region.empty(); });
}

} // namespace

void confirm_empty_segmentation_annotation(TaskSampleBinding& sample) {
    sample.regions.clear();
    sample.annotation_state = "annotated";
}

void refresh_segmentation_annotation(
    const std::vector<AnnotationClass>& classes, TaskSampleBinding& sample)
{
    if (!valid_target(classes, sample)) sample.annotation_state = "unannotated";
    else if (has_foreground(sample)) sample.annotation_state = "annotated";
}

bool commit_segmentation_mask(
    const std::vector<AnnotationClass>& classes, TaskSampleBinding& sample,
    const QImage& mask)
{
    sample.regions.clear();
    sample.annotation_state = "unannotated";
    if (mask.isNull()) {
        sample.quality_issues.push_back("mask is unreadable");
        return false;
    }
    const QSize dimensions = sample.image_paths.empty() ? QSize{}
        : QImageReader(QString::fromStdString(sample.image_paths.front())).size();
    if (dimensions.isValid() && mask.size() != dimensions) {
        sample.quality_issues.push_back("mask dimensions do not match the sample image");
        return false;
    }
    const auto labels = mask.convertToFormat(QImage::Format_Grayscale8);
    auto region = jocelin::geo::label_image_to_region_simd_omp({labels.constBits(), labels.height(),
        labels.width(), static_cast<size_t>(labels.bytesPerLine())});
    if (!region.empty()) sample.regions.push_back(std::move(region));
    if (!valid_target(classes, sample)) {
        sample.regions.clear();
        return false;
    }
    if (has_foreground(sample)) {
        sample.annotation_state = "annotated";
    }
    return true;
}

void import_segmentation_annotation(
    const std::vector<AnnotationClass>& classes, TaskSampleBinding& sample,
    const QString& mask_path)
{
    if (mask_path.isEmpty()) {
        sample.regions.clear();
        sample.annotation_state = "unannotated";
        return;
    }
    const QImage mask(mask_path);
    if (commit_segmentation_mask(classes, sample, mask) && !has_foreground(sample)) {
        sample.annotation_state = "annotated";
    }
}

QImage segmentation_annotation_image(const TaskSampleBinding& sample) {
    if (sample.image_paths.empty()) return {};
    const auto dimensions = QImageReader(QString::fromStdString(sample.image_paths.front())).size();
    if (!dimensions.isValid()) return {};
    const auto pixels = jocelin::processor::regions_to_label_tensor(sample.regions,
        dimensions.height(), dimensions.width(), 0).to(torch::kUInt8).contiguous();
    return QImage(pixels.data_ptr<uint8_t>(), dimensions.width(), dimensions.height(),
        dimensions.width(), QImage::Format_Grayscale8).copy();
}

} // namespace studio
