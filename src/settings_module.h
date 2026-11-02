#pragma once

#include <QCoreApplication>
#include <QObject>

class QActionGroup;
class QComboBox;
class QMenu;
class QToolBar;
class QWidget;

namespace studio {

class SettingsModule final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(studio::SettingsModule)
public:
    explicit SettingsModule(QWidget* parent);
    void mount(QMenu& menu, QToolBar& toolbar);
private:
    void save_language(const QString& language);
    QWidget* dialog_parent_;
    QMenu* preferences_;
    QActionGroup* language_choices_;
    QComboBox* language_;
};

} // namespace studio
