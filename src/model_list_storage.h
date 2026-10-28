#pragma once

#include <common/model/model_list.h>
#include <QByteArray>
#include <QString>

namespace studio {

class ModelListStorage {
public:
    static jocelin::model::ModelList load(const QString& node_directory, const QString& root_directory);
    static QByteArray serialize(const jocelin::model::ModelList& models, const QString& root_directory);
    static void save(const QByteArray& data, const QString& node_directory);
};

} // namespace studio
