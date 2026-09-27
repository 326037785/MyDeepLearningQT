#pragma once

#include "workflow.h"

#include <QImage>
#include <QLabel>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QLabel;
class QListWidget;
class QPushButton;
class QResizeEvent;

namespace studio {

class ClassificationCanvas final : public QLabel {
public:
    explicit ClassificationCanvas(QWidget* parent = nullptr);
    void set_image(const QImage& image);
    void set_heatmap(const QImage& image);
    void set_heatmap_visible(bool visible);
    void clear_canvas();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void update_view();
    QImage image_;
    QImage heatmap_;
    bool heatmap_visible_ = false;
};

class ClassificationAnnotationWidget final : public QWidget {
public:
    explicit ClassificationAnnotationWidget(QWidget* parent = nullptr);

    void set_dataset(const std::vector<TaskSampleBinding>& samples,
                     const QStringList& class_names,
                     const QVector<int>& class_labels);
    void set_inference_directory(const QString& directory);
    void refresh_inference_result();

    std::function<void(const QString&)> class_add_requested;
    std::function<void(const QString&, int)> class_assigned;
    std::function<void(const std::vector<std::string>&, const std::string&)>
        sample_split_requested;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void load_row(int row);
    void update_preview();
    void rebuild_images();
    void rebuild_classes();
    void set_view_mode(int mode);
    void update_review_state();
    void show_class_menu(const QPoint& global_position);
    int prompt_for_class();
    int add_or_select_class(const QString& name);
    void assign_current(int class_index);
    void assign_selected(int class_index);

    std::vector<TaskSampleBinding> sample_bindings_;
    QStringList image_paths_;
    QStringList display_names_;
    QStringList class_names_;
    QVector<int> class_labels_;
    QString inference_directory_;
    int current_row_ = -1;
    int view_mode_ = 0;
    int predicted_class_ = -1;
    double confidence_ = 0.0;
    bool has_prediction_ = false;
    QImage current_image_;
    QImage current_heatmap_;
    QListWidget* images_ = nullptr;
    QListWidget* classes_ = nullptr;
    QLabel* context_ = nullptr;
    ClassificationCanvas* preview_ = nullptr;
    QLabel* review_state_ = nullptr;
    QLabel* images_title_ = nullptr;
    QLabel* empty_hint_ = nullptr;
    QPushButton* assign_ = nullptr;
    QPushButton* ground_truth_ = nullptr;
    QPushButton* prediction_ = nullptr;
    QPushButton* heatmap_ = nullptr;
};

} // namespace studio
