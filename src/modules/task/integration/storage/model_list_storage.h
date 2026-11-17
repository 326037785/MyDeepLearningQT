#pragma once

#include <common/model/model_list.h>
#include <QByteArray>
#include <QJsonValue>
#include <QString>

namespace studio {

class ModelListStorage {
public:
    static std::optional<jocelin::model::WeightNormalization> normalization_from_json(
        const QJsonValue& value, jocelin::config::WeightType format);
    static QJsonValue normalization_to_json(
        const std::optional<jocelin::model::WeightNormalization>& normalization,
        jocelin::config::WeightType format);
    static jocelin::model::ModelList load(const QString& node_directory, const QString& root_directory);
    static QByteArray serialize(const jocelin::model::ModelList& models, const QString& root_directory);
    static void save(const QByteArray& data, const QString& node_directory);
};

} // namespace studio
