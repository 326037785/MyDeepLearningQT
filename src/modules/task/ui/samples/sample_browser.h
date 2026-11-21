#pragma once

#include <QAbstractItemView>
#include <QListWidget>
#include <QMenu>

#include <functional>
#include <string>
#include <vector>

namespace studio {

struct SampleBrowserEntry {
    std::string sample_id;
    QString image_path;
    QString display_name;
    QString annotation_state;
    QString split_state;
    QString tooltip;
    int source_index = -1;
};

class SampleBrowser final : public QListWidget {
public:
    explicit SampleBrowser(QWidget* parent = nullptr);

    void set_entries(const std::vector<SampleBrowserEntry>& entries);

    void update_entry_state(int source_index, const QString& display_name,
                            const QString& annotation_state, const QString& split_state,
                            const QString& tooltip = {});

    std::vector<std::string> selected_sample_ids() const;

    QString current_sample_id() const;

    int current_source_index() const;

    void set_current_sample_id(const QString& sample_id);

    void set_current_source_index(int source_index);

    void append_context_actions(QMenu& menu, const std::vector<std::string>& ids);

    std::function<bool(int, const QString&)> current_requested;
    std::function<void(const std::vector<std::string>&, const std::string&)>
        split_requested;
    std::function<void(QMenu&, const std::vector<std::string>&)>
        extend_context_menu;

private:
    void set_current_row_silently(int row);

    bool changing_current_ = false;
    int accepted_row_ = -1;
};

} // namespace studio
