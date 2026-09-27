#pragma once

#include "workflow.h"

#include <QDialog>

class QLabel;
class QListView;
class QDragEnterEvent;
class QDropEvent;
class QModelIndex;

namespace studio {

class DatasetItemModel;

class DatasetImportDialog final : public QDialog {
public:
    explicit DatasetImportDialog(const DatasetConfig& current, QWidget* parent = nullptr);
    const DatasetConfig& result() const { return working_; }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void choose_folder();
    void choose_files();
    void import_paths(const QStringList& paths, bool append);
    void refresh();
    void show_preview(const QModelIndex& index);

    DatasetConfig working_;
    DatasetItemModel* model_ = nullptr;
    QListView* images_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* detail_ = nullptr;
};

} // namespace studio
