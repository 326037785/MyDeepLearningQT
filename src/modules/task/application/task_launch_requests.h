#pragma once

#include <common/config/runtime_route.h>

#include <QString>

namespace studio {

struct TrainingLaunchRequest {
    QString mode = "new";
    QString model_id;
};

struct InferenceLaunchRequest {
    QString model_id;
    QString weight_id;
    jocelin::config::Device device;
};

} // namespace studio
