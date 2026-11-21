#pragma once

#include "modules/workflow/model/workflow.h"
#include <QUndoCommand>
#include <optional>
#include <vector>

namespace studio {

class DetectionBoxEdit final : public QUndoCommand {
public:
    DetectionBoxEdit(std::vector<jocelin::meta::BboxComp>& boxes, int index, std::optional<jocelin::meta::BboxComp> before, std::optional<jocelin::meta::BboxComp> after)
        : boxes_(boxes), index_(index), before_(std::move(before)), after_(std::move(after)) {}

    void undo() override { apply(after_, before_); }
    void redo() override { apply(before_, after_); }

private:
    void apply(const std::optional<jocelin::meta::BboxComp>& from, const std::optional<jocelin::meta::BboxComp>& to) {
        const auto position = boxes_.begin() + index_;
        if (from && to) {
            *position = *to;
        } else if (to) {
            boxes_.insert(position, *to);
        } else {
            boxes_.erase(position);
        }
    }

    std::vector<jocelin::meta::BboxComp>& boxes_;
    int index_;
    std::optional<jocelin::meta::BboxComp> before_;
    std::optional<jocelin::meta::BboxComp> after_;
};


} // namespace studio
