#include "modules/model_management/application/weight_conversion.h"

#include <infer_network/conversion/onnx/native_export.h>
#include <infer_network/ov/cls/cls_native_export.h>
#include <infer_network/ov/od/od_native_export.h>
#include <infer_network/ov/common/openvino_runtime.h>
#include <infer_network/ov/sg/sg_native_export.h>
#include <QFileInfo>

namespace studio {

bool can_convert_weight(const jocelin::model::ModelEntry& model, const jocelin::model::ModelWeight& source,
                        jocelin::config::WeightType target) {
    using jocelin::config::WeightType;
    if (source.format != WeightType::LibTorch || source.precision != jocelin::model::WeightPrecision::Float32 ||
        model.training_status == jocelin::model::ModelTrainingStatus::Running ||
        !QFileInfo(QString::fromStdString(source.path)).isFile()) {
        return false;
    }
    jocelin::config::AiConfig config;
    config.tool_type = model.task;
    config.model_index = model.model_index;
    config.model_scale = model.model_scale;
    if (target == WeightType::Onnx) {
        return jocelin::infer_network::conversion::onnx::supports_native_export(config);
    }
    return target == WeightType::OpenVino && jocelin::Ov::cpu_runtime_available() &&
        (jocelin::Ov::supports_sg_network(config) || jocelin::Ov::supports_cls_network(config) ||
         jocelin::Ov::supports_od_network(config));
}

} // namespace studio
