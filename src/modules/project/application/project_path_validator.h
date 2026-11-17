#pragma once

#include <QString>

namespace studio {

struct ProjectPathValidation {
    QString final_path;
    QString error;

    explicit operator bool() const { return error.isEmpty(); }
};

class ProjectPathValidator {
public:
    static QString validate_name(const QString& name);
    static ProjectPathValidation validate_save_as(
        const QString& name,
        const QString& parent_directory,
        const QString& source_root = {});
};

} // namespace studio
