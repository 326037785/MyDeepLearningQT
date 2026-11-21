#include "modules/settings/ui/settings_module.h"
#include "modules/settings/application/annotation_preferences.h"
#include "common/ui/icon_provider.h"
#include "modules/settings/application/ui_language.h"

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>

namespace studio {

SettingsModule::SettingsModule(QWidget* parent) : QObject(parent), dialog_parent_(parent) {
    preferences_ = new QMenu(tr("Preferences..."), parent);
    preferences_->setObjectName("preferencesMenu");
    preferences_->menuAction()->setObjectName("preferencesAction");
    preferences_->setIcon(IconProvider::icon("settings"));
    QMenu* language_menu = preferences_->addMenu(tr("Language"));
    language_menu->setObjectName("preferencesLanguageMenu");
    language_menu->setToolTip(tr("Language changes take effect after restarting the application."));
    language_choices_ = new QActionGroup(this);
    language_choices_->setExclusive(true);
    language_ = new QComboBox(parent);
    language_->setObjectName("homeLanguageSelector");
    language_->addItem(QStringLiteral("English"), "en");
    language_->addItem(QString::fromUtf8("简体中文"), "zh_CN");
    language_->setCurrentIndex(qMax(0, language_->findData(ui_language())));
    for (int index = 0; index < language_->count(); ++index) {
        QAction* choice = language_menu->addAction(language_->itemText(index));
        choice->setData(language_->itemData(index));
        choice->setCheckable(true);
        choice->setChecked(choice->data().toString() == ui_language());
        language_choices_->addAction(choice);
        connect(choice, &QAction::triggered, this,
                [this, choice] { save_language(choice->data().toString()); });
    }
    QAction* annotation_auto_save = preferences_->addAction(tr("Auto-save annotations"));
    annotation_auto_save->setObjectName("annotationAutoSaveAction");
    annotation_auto_save->setCheckable(true);
    annotation_auto_save->setChecked(studio::annotation_auto_save());
    annotation_auto_save->setToolTip(tr("Save the current annotation when switching samples."));
    connect(annotation_auto_save, &QAction::toggled, this, [](bool enabled) {
        set_annotation_auto_save(enabled);
    });
    connect(language_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { save_language(language_->currentData().toString()); });
}

void SettingsModule::mount(QMenu& menu, QToolBar& toolbar) {
    menu.addMenu(preferences_);
    auto* button = new QToolButton(&toolbar);
    button->setIcon(preferences_->icon());
    button->setMenu(preferences_);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setText(tr("Settings"));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar.addWidget(button);
    toolbar.addWidget(language_);
}

void SettingsModule::save_language(const QString& choice) {
    if (ui_language() == choice) {
        return;
    }
    save_ui_language(choice);
    {
        const QSignalBlocker blocker(language_);
        language_->setCurrentIndex(language_->findData(choice));
    }
    for (QAction* action : language_choices_->actions()) {
        action->setChecked(action->data().toString() == choice);
    }
    QMessageBox::information(dialog_parent_, tr("Language saved"),
        tr("Restart the application to apply the language change."));
}

} // namespace studio
