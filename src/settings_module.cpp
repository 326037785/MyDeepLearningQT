#include "settings_module.h"
#include "icon_provider.h"
#include "ui_language.h"

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace studio {

SettingsModule::SettingsModule(QWidget* parent) : QObject(parent), dialog_parent_(parent) {
    preferences_ = new QAction(tr("Preferences..."), this);
    preferences_->setObjectName("preferencesAction");
    preferences_->setIcon(IconProvider::icon("settings"));
    language_ = new QComboBox(parent);
    language_->setObjectName("homeLanguageSelector");
    language_->addItem(QStringLiteral("English"), "en");
    language_->addItem(QString::fromUtf8("简体中文"), "zh_CN");
    language_->setCurrentIndex(qMax(0, language_->findData(ui_language())));
    connect(preferences_, &QAction::triggered, this, &SettingsModule::show_preferences);
    connect(language_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { save_language(language_->currentData().toString()); });
}

void SettingsModule::mount(QMenu& menu, QToolBar& toolbar) {
    menu.addAction(preferences_);
    auto* button = new QToolButton(&toolbar);
    button->setDefaultAction(preferences_);
    button->setText(tr("Settings"));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar.addWidget(button);
    toolbar.addWidget(language_);
}

void SettingsModule::save_language(const QString& choice) {
    if (ui_language() == choice) return;
    save_ui_language(choice);
    {
        const QSignalBlocker blocker(language_);
        language_->setCurrentIndex(language_->findData(choice));
    }
    QMessageBox::information(dialog_parent_, tr("Language saved"),
        tr("Restart the application to apply the language change."));
}

void SettingsModule::show_preferences() {
    QDialog dialog(dialog_parent_);
    dialog.setObjectName("preferencesDialog");
    dialog.setWindowTitle(tr("Preferences"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* language = new QComboBox(&dialog);
    language->setObjectName("languageSelector");
    language->addItem(QStringLiteral("English"), "en");
    language->addItem(QString::fromUtf8("简体中文（启动页和项目操作）"), "zh_CN");
    language->setCurrentIndex(qMax(0, language->findData(ui_language())));
    form->addRow(tr("Language"), language);
    layout->addLayout(form);
    auto* hint = new QLabel(tr("Language changes take effect after restarting the application."), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) save_language(language->currentData().toString());
}

} // namespace studio
