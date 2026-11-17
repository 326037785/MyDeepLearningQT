#include "modules/project/storage/training_run_store.h"
#include "modules/project/storage/project_storage.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <limits>
#include <stdexcept>

namespace studio {

TrainingRunStore TrainingRunStore::load(const QString& root_directory) {
    const QString path = QDir(root_directory).filePath("runs/training_runs.json");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(ProjectStorage::read(path), &error);
    const auto root = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        root.size() != 1 || !root.value("next_run_number").isDouble())
        throw std::runtime_error("invalid training run sequence");
    TrainingRunStore result;
    result.next_number_ = root.value("next_run_number").toInt();
    if (result.next_number_ < 1 ||
        root.value("next_run_number").toDouble() != result.next_number_)
        throw std::runtime_error("invalid training run number");
    return result;
}

QByteArray TrainingRunStore::serialize() const {
    return QJsonDocument(QJsonObject{{"next_run_number", next_number_}})
        .toJson(QJsonDocument::Indented);
}

void TrainingRunStore::save(const QString& root_directory, const QByteArray& data) {
    const QDir runs(QDir(root_directory).filePath("runs"));
    if (!QDir().mkpath(runs.path()))
        throw std::runtime_error("cannot create training run directory");
    ProjectStorage::write_atomic(runs.filePath("training_runs.json"), data);
    dirty_ = false;
}

int TrainingRunStore::allocate() {
    if (next_number_ == std::numeric_limits<int>::max())
        throw std::runtime_error("training run number limit reached");
    dirty_ = true;
    return next_number_++;
}

} // namespace studio
