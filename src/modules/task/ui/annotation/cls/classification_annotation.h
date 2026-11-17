#pragma once

#include "modules/workflow/model/workflow.h"
#include "modules/task/ui/common/result_inspection.h"

#include <QImage>
#include <QLabel>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QResizeEvent;
class QTabWidget;

namespace studio {

class ClassificationCanvas final : public QLabel {
public:
    explicit ClassificationCanvas(QWidget* parent = nullptr, QLabel* overlay = nullptr);
    void set_image(const QImage& image);
    void set_heatmap(const QImage& image);
    void set_heatmap_visible(bool visible);
    void clear_canvas();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void update_view();
    QLabel* overlay_ = nullptr;
    QImage image_;
    QImage heatmap_;
    bool heatmap_visible_ = false;
};

class ClassificationAnnotationWidget final : public QWidget {
public:
    explicit ClassificationAnnotationWidget(QWidget* parent = nullptr, QTabWidget* inspector = nullptr);

    void set_dataset(const std::vector<TaskSampleBinding>& samples,
                     const QStringList& class_names,
                     const QVector<int>& class_labels,
                     const std::vector<AnnotationClass>& class_metadata = {});
    void set_inference_result(const InferenceResultSet* result, const QStringList& model_classes = {});
    void refresh_inference_result();

    std::function<std::vector<AnnotationClass>(const QString&)> class_add_requested;
    std::function<void(const std::vector<std::string>&, int)> class_assigned;
    std::function<void(const std::vector<std::string>&, const std::string&)>
        sample_split_requested;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void load_row(int row);
    void update_preview();
    void refresh_images();
    void rebuild_classes();
    void update_review_state();
    void show_class_menu(const QPoint& global_position);
    int prompt_for_class();
    int add_or_select_class(const QString& name);
    void assign_selected(int class_index);

    std::vector<TaskSampleBinding> sample_bindings_;
    QStringList image_paths_;
    QStringList display_names_;
    QStringList class_names_;
    std::vector<AnnotationClass> class_metadata_;
    QVector<int> class_labels_;
    std::vector<ResultReference> inference_results_;
    QVector<ResultPresentationItem> result_items_;
    QPointer<QMenu> result_popup_;
    QStringList prediction_class_names_;
    int current_row_ = -1;
    double confidence_ = 0.0;
    QImage current_image_;
    QImage current_heatmap_;
    QListWidget* images_ = nullptr;
    QListWidget* classes_ = nullptr;
    QLabel* context_ = nullptr;
    ClassificationCanvas* preview_ = nullptr;
    QLabel* image_labels_ = nullptr;
    QLabel* review_state_ = nullptr;
    QLabel* result_details_ = nullptr;
    QLabel* images_title_ = nullptr;
    QLabel* empty_hint_ = nullptr;
    QPushButton* assign_ = nullptr;
    QCheckBox* heatmap_layer_ = nullptr;
};

} // namespace studio
