#pragma once

#include <QFrame>

#include <functional>
#include <memory>

class QLabel;
class QGridLayout;
class QPushButton;
class QToolButton;

namespace jocelin::config { struct AiConfig; }

namespace studio {

class InferenceFilterContent : public QWidget {
public:
    using QWidget::QWidget;
    virtual void load(const jocelin::config::AiConfig& config) = 0;
    virtual bool apply_to(jocelin::config::AiConfig& config, QString& error) const = 0;
};

class InferenceFilterPanel final : public QFrame {
public:
    explicit InferenceFilterPanel(QWidget* parent = nullptr);
    void set_content(std::unique_ptr<InferenceFilterContent> content);
    void set_config(std::function<jocelin::config::AiConfig&()> config);

    std::function<void()> applied;

private:
    void build_ui();
    void connect_controls();

    InferenceFilterContent* content_ = nullptr; // Owned by the Qt widget tree.
    std::function<jocelin::config::AiConfig&()> config_;
    QToolButton* toggle_ = nullptr;
    QPushButton* reset_ = nullptr;
    QPushButton* apply_ = nullptr;
    QWidget* body_ = nullptr;
    QGridLayout* fields_ = nullptr;
    QLabel* error_ = nullptr;
};

} // namespace studio
