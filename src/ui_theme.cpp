#include "ui_theme.h"

#include <QApplication>

namespace studio {

void apply_ui_theme(QApplication& application) {
    if (application.property("studioThemeApplied").toBool()) return;
    application.setProperty("studioThemeApplied", true);
    application.setStyleSheet(QStringLiteral(R"QSS(
QMainWindow, QWidget { background: #F6F8FA; color: #172B32; }
QLabel, QCheckBox, QRadioButton { background: transparent; }
QMenuBar { font-size: 12px; background: #F1F5FB; border-bottom: 1px solid #DCE4EF; padding: 1px 6px; }
QMenuBar::item { padding: 3px 7px; }
QMenuBar::item:selected { background: #EEF8F5; border-radius: 4px; }
QToolBar { background: #173F46; border: 0; padding: 1px 8px; spacing: 2px; }
QToolBar QToolButton { font-size: 12px; font-weight: 500; color: white; background: transparent; border-radius: 4px; padding: 1px 9px; min-height: 2px; }
QToolBar QToolButton:hover { background: #315D63; }
QToolBar QToolButton:disabled { color: #7D9699; }
QToolBar QToolButton[runAction=true] { background: #0EA67A; color: white; font-weight: 700; padding-left: 14px; padding-right: 14px; }
QToolBar QToolButton[runAction=true]:hover { background: #0A8E69; }
QToolBar QToolButton[destructive=true] { color: #FFD6D1; }
QDockWidget { font-size: 12px; font-weight: 600; }
QDockWidget::title { background: #FFFFFF; padding: 8px 10px; border-bottom: 1px solid #DCE4E7; color: #C65F24; }
QGroupBox { background: #FFFFFF; font-weight: 600; border: 1px solid #DCE4E7; border-radius: 7px; margin-top: 10px; padding: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
QLabel[heading=true] { font-size: 16px; font-weight: 600; color: #173F46; padding: 2px 0 4px 0; }
QLabel[pageTitle=true] { font-size: 20px; font-weight: 700; color: #172B32; }
QLabel[sectionTitle=true] { font-size: 16px; font-weight: 700; color: #172B32; padding-top: 1px; }
QLabel[eyebrow=true] { font-size: 11px; font-weight: 700; letter-spacing: 1px; color: #C65F24; }
QLabel[muted=true] { color: #667A83; }
QLabel[meta=true] { font-size: 11px; color: #667A83; }

QToolBar#homeToolbar { background: #F1F5FB; border-bottom: 1px solid #DCE4EF; padding: 5px 10px; spacing: 4px; }
QToolBar#homeToolbar QToolButton { color: #17243C; padding: 5px 9px; }
QToolBar#homeToolbar QToolButton:hover { background: #DFEAF9; }
QToolBar#homeToolbar QToolButton:disabled { color: #8A96A6; }
QWidget#homeToolbarSpacer { background: transparent; }
QWidget#projectHome, QWidget#homeContent { background: #F8FAFD; }
QScrollArea#homeScroll { background: #F8FAFD; border: 0; }
QFrame#homeHero { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #F8FAFD,stop:1 #E6F0FF); border: 0; }
QLabel[homeTitle=true] { font-size: 40px; font-weight: 700; color: #0C1E3A; }
QPushButton[homeAction=true] { background: #FBFCFE; border: 1px solid #D5DEEA; border-radius: 8px; text-align: left; }
QPushButton[homeAction=true][primaryCard=true] { background: #EFF6FF; border-color: #9AC1FF; }
QPushButton[homeAction=true]:hover { background: #EAF3FF; border-color: #82AFF3; }
QLabel[homeActionTitle=true] { color: #0C1E3A; font-size: 16px; font-weight: 700; }
QLabel[homeActionHint=true] { color: #53647D; font-size: 13px; }
QLabel[homeActionArrow=true] { color: #61748C; font-size: 24px; }
QFrame#recentProjectsCard { background: transparent; border: 0; border-top: 1px solid #DCE4EF; border-radius: 0; }
QTableWidget#recentProjectsTable { background: transparent; border: 0; border-radius: 0; }
QTableWidget#recentProjectsTable QHeaderView::section { background: #F8FAFD; border-bottom: 1px solid #DCE4EF; }
QWidget#recentProjectCell { background: #F8FAFD; }
QLabel#recentProjectsEmpty, QFrame[emptyState=true] { background: #F8FAFB; border: 1px dashed #C8D6DA; border-radius: 8px; padding: 16px; }

QFrame#workflowContextHeader { background: #FFFFFF; border-bottom: 1px solid #DCE4E7; }
QLabel[workflowContextTitle=true] { font-size: 13px; font-weight: 700; color: #173F46; }
QLabel[workspaceTitle=true] { font-size: 14px; font-weight: 700; color: #173F46; }
QLabel[annotationContext=true]{font-weight: 600;  color: #172B32;}
QLabel[emptyHint=true]{color: #667A83;background: #F4F7F8;border: 0;border-radius: 5px;padding: 8px;}
QLabel[workflowSteps=true] { font-size: 10px; color: #087A5C; background: #EAF7F4; border: 1px solid #CDE9E2; border-radius: 9px; padding: 3px 7px; }
QTabBar#workflowTabs { background: #FFFFFF; }
QTabBar#workflowTabs::tab { background: #FFFFFF; border: 1px solid #DCE4E7; border-bottom: 0; padding: 7px 12px; margin-right: 3px; min-width: 90px; }
QTabBar#workflowTabs::tab:selected { color: #087A5C; border-top: 2px solid #0EA67A; background: #F8FCFB; }
QToolButton#workflowTabCloseButton { color: #667A83; background: transparent; border: 0; border-radius: 4px; font-weight: 700; padding: 0; }
QToolButton#workflowTabCloseButton:hover { color: #A33B32; background: #FCEBE9; }
QFrame#canvasEmptyState { background: #FFFFFF; border: 1px dashed #C8D6DA; border-radius: 10px; }
QFrame#canvasViewTools { background: #FFFFFF; border: 1px solid #DCE4E7; border-radius: 7px; }
QFrame#canvasViewTools QToolButton { background: #FFFFFF; border: 1px solid #C8D6DA; border-radius: 5px; padding: 5px 8px; min-height: 18px; }
QFrame#canvasViewTools QToolButton:hover { background: #EEF8F5; border-color: #70BFAE; }
QFrame#workflowOverview, QFrame#minimapPanel { background: #FFFFFF; border-top: 1px solid #DCE4E7; }
QGraphicsView#workflowMinimapView { background: #F4F7F6; border: 1px solid #DCE4E7; border-radius: 5px; }
QLabel[overviewValue=true] { font-weight: 700; color: #172B32; }
QLabel[status=ready] { color: #0EA67A; font-weight: 700; }
QLabel[status=running] { color: #C9821E; font-weight: 700; }
QLabel[status=error] { color: #C0392B; font-weight: 700; }

QPushButton { background: #FFFFFF; border: 1px solid #C8D6DA; border-radius: 5px; padding: 3px 7px; text-align: left; }
QPushButton:hover { background: #EEF8F5; border-color: #70BFAE; }
QPushButton:disabled { color: #8A9B99; background: #F4F7F8; border-color: #DCE4E7; }
QPushButton[primary=true] { background: #0EA67A; color: white; border-color: #0A8E69; font-weight: 700; padding: 5px 11px; }
QPushButton[primary=true]:hover { background: #0A8E69; }
QPushButton[accentSecondary=true] { background: white; color: #087A5C; border-color: #20A987; font-weight: 700; }
QPushButton[destructive=true] { color: #C0392B; border-color: #E7B8B2; }
QPushButton[nodeLibraryItem=true] { background: #FFFFFF; padding: 6px 8px; font-size: 12px; font-weight: 600; }
QPushButton[nodeLibraryItem=true][group=data] { border-left: 4px solid #2C91A5; }
QPushButton[nodeLibraryItem=true][group=task] { border-left: 4px solid #D78A2F; }
QPushButton[stepNav=true] { background: transparent; border: 0; border-bottom: 2px solid transparent; color: #667A83; padding: 5px 8px; text-align: center; }
QPushButton[stepNav=true][active=true] { color: #087A5C; border-bottom-color: #0EA67A; font-weight: 700; }
QPushButton[stepNav=true][done=true] { color: #0EA67A; }
QPushButton[stepNav=true]:disabled { color: #8A9B99; border-bottom-color: transparent; font-weight: 500; }
QPushButton[inspectorTab=true] { border: 0; border-bottom: 2px solid transparent; border-radius: 0; text-align: center; }
QPushButton[inspectorTab=true][active=true] { color: #087A5C; border-bottom-color: #0EA67A; font-weight: 700; }

QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QTextEdit, QPlainTextEdit { background: white; border: 1px solid #C8D6DA; border-radius: 4px; padding: 3px 5px; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid #0EA67A; }
QTableWidget, QListWidget, QTreeWidget { background: #FFFFFF; border: 1px solid #DCE4E7; border-radius: 7px; gridline-color: #E8EEF0; }
QHeaderView::section { background: #F4F7F8; border: 0; border-bottom: 1px solid #DCE4E7; padding: 6px; font-weight: 600; color: #425765; }
QTableWidget::item:selected, QListWidget::item:selected { background: #DDF5EE; color: #123C35; }
QCheckBox:disabled { color: #8A9B99; }
QLabel[statusBadge=true] { background: #EEF2F3; color: #31575C; border-radius: 8px; padding: 2px 6px; font-weight: 600; }
QLabel[statusBadge=true][runtimeState="running"] { background: #FFF0D9; color: #8A5A10; }
QLabel[statusBadge=true][runtimeState="pausing"] { background: #FFF0D9; color: #8A5A10; }
QLabel[statusBadge=true][runtimeState="paused"] { background: #E8EEFA; color: #365B91; }
QLabel[statusBadge=true][runtimeState="completed"] { background: #DDF5EE; color: #087A5C; }
QLabel[statusBadge=true][runtimeState="error"] { background: #FCEBE9; color: #A33B32; }
QLabel[statusBadge=true][runtimeState="stopped"] { background: #EEF2F3; color: #667A83; }
QLabel[inlineWarning=true] { background: #FFF5E6; color: #8A5A10; border-left: 3px solid #C9821E; padding: 7px 9px; }
QFrame#datasetStatsPanel { background: #FFFFFF; border: 1px solid #DCE4E7; border-radius: 8px; }
QWidget#datasetImportRow { background: transparent; }
QFrame#nodeWorkspaceHeader { background: #FFFFFF; border: 1px solid #DCE4E7; border-radius: 8px; }
QFrame#taskWorkspaceHeader { background: #FFFFFF; border-bottom: 1px solid #DCE4E7; }
QWidget#taskWorkspace QLabel[workspaceTitle=true] { font-size: 12px; font-weight: 600; }
QWidget#taskWorkspace QLabel[workspacePath=true] { font-size: 11px; color: #667A83; }
QWidget#taskWorkspace QLabel[sectionTitle=true] { font-size: 13px; font-weight: 600; padding: 0; }
QWidget#taskWorkspace QPushButton[stepNav=true] { font-size: 11px; padding: 3px 5px; }
QWidget#taskWorkspace QFrame[metricCard=true] { background: #FFFFFF; border: 1px solid #DCE4E7; border-radius: 5px; }
QWidget#taskWorkspace QLabel[metricValue=true] { font-size: 13px; font-weight: 700; }
QFrame[metricCard=true] { background: #FFFFFF; border: 1px solid #DCE4E7; border-radius: 7px; }
QFrame[sectionPanel=true] { background: #FFFFFF; border: 1px solid #DCE4E7; border-radius: 7px; }
QFrame#taskResultsEmptyState { padding: 0; }
QFrame[configSection=true] { background: #FFFFFF; border: 0; border-bottom: 1px solid #DCE4E7; border-radius: 0; }
QLabel[configFieldLabel=true] { font-size: 10px; color: #667A83; }
QLabel[configReadOnly=true] { background: #F4F7F8; border: 1px solid #DCE4E7; border-radius: 4px; padding: 4px 6px; color: #31575C; }
QWidget#trainingSettingsPanel { background: #FFFFFF; }
QFrame#trainingModelConfigSection, QFrame#trainingRunConfigSection { background: transparent; border: 0; }
QPushButton#trainingAdvancedToggle[disclosureRow=true] { background: transparent; border: 0; border-radius: 0; color: #31575C; padding: 4px 8px; text-align: left; font-weight: 600; }
QPushButton#trainingAdvancedToggle[disclosureRow=true]:hover { background: #F4F7F8; }
QPushButton#trainingAdvancedToggle[disclosureRow=true]:checked { background: #F8FAFA; color: #173F46; }
QCheckBox#trainingUseGpu { color: #31575C; spacing: 6px; }
QCheckBox#trainingUseGpu::indicator { width: 28px; height: 14px; border: 1px solid #AABDB9; border-radius: 7px; background: #E7ECEB; }
QCheckBox#trainingUseGpu::indicator:checked { border-color: #0A8E69; background: #0EA67A; }
QToolButton[panelCommand=true] { background: #FFFFFF; color: #31575C; border: 1px solid #C8D6DA; border-radius: 5px; padding: 3px 7px; }
QToolButton[panelCommand=true]:hover { background: #EEF8F5; border-color: #70BFAE; }
QToolButton[panelCommand=true]:checked { background: #DDF5EE; color: #087A5C; border-color: #70BFAE; }
QTreeWidget#projectModelBrowserList { font-size: 10px; }
QTreeWidget#projectModelBrowserList QHeaderView::section { font-size: 11px; }
QTreeWidget#projectModelBrowserList::item:selected { background: #DDF5EE; color: #123C35; }
QLabel[metricValue=true] { font-size: 17px; font-weight: 700; color: #173F46; }
QLabel[runtimeMetric=true] { color: #31575C; font-weight: 600; padding-right: 10px; }
QFrame[sectionDivider=true] { color: #DCE4E7; background: #DCE4E7; max-height: 1px; }
QLabel[datasetMetric=true] { font-size: 16px; font-weight: 700; color: #173F46; }

QFrame#annotationContextBar { background: #FFFFFF; border: 0;    border-bottom: 1px solid #CBD8D5;border-radius: 0; }
QFrame#classificationContextBar { background: #FFFFFF; border: 1px solid #CBD8D5; border-radius: 7px; }
QFrame#annotationSamplesPanel, QFrame#inferenceSamplesPanel, QFrame#resultsSamplesPanel { background: #EDF2F0; border: 0; border-right: 1px solid #CBD8D5; border-radius: 0; }
QFrame#classificationImagesPanel { background: #EDF2F0; border: 1px solid #CBD8D5; border-radius: 7px; }
QFrame#annotationClassesPanel, QFrame#inferenceModelsPanel, QFrame#resultsDetailPanel { background: #FFFFFF; border: 0; border-left: 1px solid #CBD8D5; border-radius: 0; }
QFrame#classificationClassesPanel { background: #FFFFFF; border: 1px solid #CBD8D5; border-radius: 7px; }
QListWidget#annotationImageList { background: #F9FBFA; border: 0; border-radius: 4px; }
QListWidget#annotationImageList::item:selected { background: #CCE2DC; color: #173F46; }
QListWidget#classificationImageList { background: #F9FBFA; border: 0; border-radius: 5px; }
QListWidget#classificationImageList::item:selected { background: #CCE2DC; color: #173F46; }
QLabel#annotationQualitySummary[qualityState=ready] { color: #087A5C; }
QLabel#annotationQualitySummary[qualityState=warning] { color: #9A5D13; }
QListWidget#segmentationClassList { background: #F9FBFA; border: 1px solid #DCE4E7; border-radius: 5px; }
QListWidget#segmentationClassList::item { padding: 3px 5px; }
QListWidget#segmentationClassList::item:selected { background: #DDF5EE; color: #123C35; }
QFrame#annotationViewportPanel, QFrame#inferenceViewportPanel, QFrame#resultsViewportPanel { background: #18292D; border: 1px solid #31575C; border-radius: 7px; }
QFrame#classificationPreviewPanel { background: #18292D; border: 1px solid #31575C; border-radius: 7px; }
QFrame#annotationToolBar, QFrame#annotationLayerBar, QFrame#inferenceLayerBar, QFrame#resultsLayerBar { background: #E3EBE8; border: 0; }
QFrame#annotationToolBar { border-bottom: 1px solid #B9CCC8; }
QFrame#annotationLayerBar, QFrame#inferenceLayerBar, QFrame#resultsLayerBar { border-top: 1px solid #B9CCC8; }
QToolButton[iconTool=true] { background: #FFFFFF; border: 1px solid #B9CCC8; border-radius: 5px; padding: 3px; }
QToolButton[iconTool=true]:hover { background: #EEF8F5; border-color: #70BFAE; }
QToolButton[iconTool=true]:checked { background: #BFE8DC; border: 2px solid #0A8E69; padding: 2px; }
QToolButton[iconTool=true]:focus { border-color: #0EA67A; }
QToolButton[iconTool=true]:disabled { background: #EEF2F3; border-color: #DCE4E7; }
QFrame#annotationContextBar QToolButton, QFrame#annotationToolBar QToolButton { padding: 3px; }
QFrame#taskInspector { background: #EAF0EE; border: 1px solid #C2D0CD; border-radius: 8px; }
QFrame#taskInspectorHeader { background: #DCE6E3; border-bottom: 1px solid #C2D0CD; }
QToolBox#taskInspectorSections { background: #EAF0EE; border: 0; }
QToolBox#taskInspectorSections::tab { background: #DCE6E3; color: #345156; border-top: 1px solid #C2D0CD; padding: 6px 8px; font-weight: 600; }
QToolBox#taskInspectorSections::tab:selected { background: #FFFFFF; color: #173F46; border-left: 4px solid #0EA67A; font-weight: 700; }
QToolBox#taskInspectorSections QScrollArea { background: #FFFFFF; border: 0; }
QFrame#trainingAdvancedPanel { background: transparent; border: 0; }
QFrame#trainingAdvancedPanel QLabel { font-size: 10px; color: #667A83; }
QSplitter::handle { background: #D9E3E1; }
QSplitter::handle:hover { background: #70BFAE; }
QLabel[imagePreview=true] { background: #18292D; color: #C6D5D3; border: 1px solid #31575C; border-radius: 7px; padding: 12px; }
QGraphicsView { background: #EFF3F1; border: 0; }
QStatusBar { background: #FFFFFF; border-top: 1px solid #DCE4E7; padding: 3px 8px; color: #425765; }
)QSS"));
}

} // namespace studio
