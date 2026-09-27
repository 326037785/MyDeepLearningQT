#include "dataset_import_dialog.h"

#include "dataset_import_service.h"
#include "dataset_item_model.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QListView>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QUrl>
#include <QVBoxLayout>

namespace studio {

DatasetImportDialog::DatasetImportDialog(const DatasetConfig& current, QWidget* parent)
    : QDialog(parent), working_(current) {
    setObjectName("datasetImportDialog");
    setWindowTitle(tr("Dataset resources"));
    setAcceptDrops(true);
    const QRect available = screen() ? screen()->availableGeometry() : QRect(0, 0, 1024, 700);
    resize(qMin(920, available.width() - 48), qMin(620, available.height() - 48));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 12);
    root->setSpacing(10);
    auto* title = new QLabel(tr("Dataset image resources"), this);
    title->setProperty("heading", true);
    root->addWidget(title);
    auto* helper = new QLabel(
        tr("Import a folder, multiple images, or drop files here. Import is independent of Task nodes; task-specific labels are validated only when training or inference starts."), this);
    helper->setWordWrap(true);
    root->addWidget(helper);

    auto* path_row = new QHBoxLayout;
    auto* path = new QLineEdit(this);
    path->setObjectName("datasetImportPathEdit");
    path->setPlaceholderText(tr("Paste an image or folder path"));
    auto* add_path = new QPushButton(tr("Add path"), this);
    add_path->setObjectName("datasetAddPathButton");
    path_row->addWidget(path, 1);
    path_row->addWidget(add_path);
    root->addLayout(path_row);

    auto* actions = new QHBoxLayout;
    auto* folder = new QPushButton(tr("Add folder..."), this);
    folder->setObjectName("datasetAddFolderButton");
    auto* files = new QPushButton(tr("Add images..."), this);
    files->setObjectName("datasetAddImagesButton");
    auto* replace = new QPushButton(tr("Replace from folder..."), this);
    auto* clear = new QPushButton(tr("Clear"), this);
    clear->setObjectName("datasetClearButton");
    actions->addWidget(folder);
    actions->addWidget(files);
    actions->addWidget(replace);
    actions->addWidget(clear);
    actions->addStretch();
    summary_ = new QLabel(this);
    summary_->setProperty("muted", true);
    actions->addWidget(summary_);
    root->addLayout(actions);

    auto* content = new QHBoxLayout;
    content->setSpacing(12);
    model_ = new DatasetItemModel(this);
    images_ = new QListView(this);
    images_->setObjectName("datasetImportImageList");
    images_->setModel(model_);
    images_->setViewMode(QListView::IconMode);
    images_->setIconSize(QSize(104, 78));
    images_->setGridSize(QSize(144, 118));
    images_->setResizeMode(QListView::Adjust);
    images_->setSelectionMode(QAbstractItemView::SingleSelection);
    images_->setWordWrap(true);
    content->addWidget(images_, 3);

    auto* preview_column = new QVBoxLayout;
    preview_ = new QLabel(tr("Select an image to preview"), this);
    preview_->setObjectName("datasetImportPreview");
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumSize(220, 180);
    preview_->setProperty("imagePreview", true);
    preview_column->addWidget(preview_, 1);
    detail_ = new QLabel(this);
    detail_->setWordWrap(true);
    detail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    preview_column->addWidget(detail_);
    content->addLayout(preview_column, 2);
    root->addLayout(content, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Apply"));
    root->addWidget(buttons);

    connect(folder, &QPushButton::clicked, this, [this] { choose_folder(); });
    connect(files, &QPushButton::clicked, this, [this] { choose_files(); });
    connect(replace, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(
            this, tr("Replace dataset from folder"), {},
            QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
        if (!path.isEmpty()) import_paths({path}, false);
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        working_ = DatasetImportService::cleared(working_);
        refresh();
    });
    const auto add_entered_path = [this, path] {
        const QString source = path->text().trimmed();
        if (!source.isEmpty()) import_paths({source}, true);
    };
    connect(add_path, &QPushButton::clicked, this, add_entered_path);
    connect(path, &QLineEdit::returnPressed, this, add_entered_path);
    connect(images_, &QListView::clicked, this, [this](const QModelIndex& index) { show_preview(index); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refresh();
}

void DatasetImportDialog::choose_folder() {
    const QString path = QFileDialog::getExistingDirectory(
        this, tr("Add image folder"), {},
        QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (!path.isEmpty()) import_paths({path}, true);
}

void DatasetImportDialog::choose_files() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add images"), {}, tr("Images and files (*.*)"), nullptr,
        QFileDialog::DontUseNativeDialog);
    if (!paths.isEmpty()) import_paths(paths, true);
}

void DatasetImportDialog::import_paths(const QStringList& paths, bool append) {
    const auto imported = DatasetImportService::import_paths(working_, paths, append);
    if (!imported.error.isEmpty()) {
        QMessageBox::warning(this, tr("Dataset import"), imported.error);
        return;
    }
    working_ = imported.config;
    refresh();
    summary_->setToolTip(tr("Added %1; skipped %2 duplicate(s)")
                             .arg(imported.added).arg(imported.duplicate));
}

void DatasetImportDialog::refresh() {
    model_->set_config(working_);
    int valid = 0;
    for (const auto& item : working_.items) valid += item.valid ? 1 : 0;
    summary_->setText(tr("%1 valid · %2 failed").arg(valid).arg(working_.items.size() - valid));
    preview_->setPixmap({});
    preview_->setText(working_.items.empty() ? tr("Drop images or choose a source") : tr("Select an image to preview"));
    detail_->clear();
}

void DatasetImportDialog::show_preview(const QModelIndex& index) {
    const QString path = index.data(DatasetItemModel::PathRole).toString();
    const bool valid = index.data(DatasetItemModel::ValidRole).toBool();
    detail_->setText(valid ? path : path + "\n" + index.data(DatasetItemModel::ErrorRole).toString());
    if (!valid) {
        preview_->setPixmap({});
        preview_->setText(tr("Cannot decode this file"));
        return;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    preview_->setPixmap(QPixmap::fromImage(image).scaled(preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void DatasetImportDialog::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void DatasetImportDialog::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls())
        if (url.isLocalFile()) paths.push_back(url.toLocalFile());
    if (!paths.isEmpty()) {
        import_paths(paths, true);
        event->acceptProposedAction();
    }
}

} // namespace studio
