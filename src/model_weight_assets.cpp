#include "model_weight_assets.h"

#include <infer_network/ov/common/openvino_runtime.h>
#include <QFileInfo>
#include <QJsonArray>
#include <filesystem>
#include <stdexcept>

namespace studio::ModelWeightAssets {

using jocelin::config::WeightType;

QStringList files(WeightType format, const QString& primary) {
    QStringList result{primary};
    if (format == WeightType::OpenVino) {
        const std::filesystem::path bin = jocelin::Ov::ir_weights_path(std::filesystem::u8path(primary.toStdString()));
        if (!bin.empty()) {
            result.push_back(QString::fromStdString(bin.u8string()));
        }
    }
    return result;
}

QStringList files(const jocelin::model::ModelWeight& weight) {
    return files(weight.format, QString::fromStdString(weight.path));
}

QStringList missing(const jocelin::model::ModelWeight& weight) {
    const QStringList assets = files(weight);
    if (weight.format == WeightType::OpenVino && assets.size() != 2) {
        return assets;
    }
    QStringList result;
    for (const QString& asset : assets) {
        if (!QFileInfo(asset).isFile()) {
            result.push_back(asset);
        }
    }
    return result;
}

QJsonValue associated_files(WeightType format, const QString& primary) {
    const QStringList assets = files(format, primary);
    if (assets.size() == 1) {
        return QJsonValue(QJsonValue::Undefined);
    }
    return QJsonArray{QFileInfo(assets.back()).fileName()};
}

void validate_associated_files(const QJsonValue& value, WeightType format, const QString& primary) {
    if (!value.isUndefined() && value != associated_files(format, primary)) {
        throw std::runtime_error(("invalid associated model weight files for " + primary).toStdString());
    }
}

} // namespace studio::ModelWeightAssets
