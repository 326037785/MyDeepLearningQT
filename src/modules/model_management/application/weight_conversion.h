#pragma once

#include <common/model/model_list.h>

namespace studio {

bool can_convert_weight(const jocelin::model::ModelEntry& model, const jocelin::model::ModelWeight& source,
                        jocelin::config::WeightType target);

} // namespace studio
