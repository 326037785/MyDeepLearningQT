#pragma once

#include <common/model/model_list.h>
#include <QJsonValue>
#include <QStringList>

namespace studio::ModelWeightAssets {

QStringList files(jocelin::config::WeightType format, const QString& primary);
QStringList files(const jocelin::model::ModelWeight& weight);
QStringList missing(const jocelin::model::ModelWeight& weight);
QJsonValue associated_files(jocelin::config::WeightType format, const QString& primary);
void validate_associated_files(const QJsonValue& value, jocelin::config::WeightType format, const QString& primary);

} // namespace studio::ModelWeightAssets
