# DeepLearning Studio module rules

A Studio module consumes Core task data and exposes it through the owning TaskNode. Core defines computation and typed task structures. Qt defines Project persistence, interaction and presentation.

The system definitions are in [project-overview.md](project-overview.md). Backend module rules are in [Core adding-modules.md](../../MyLibtorchLearning/docs/adding-modules.md).

## 1. Module responsibilities

| Module | Responsibility |
| --- | --- |
| src/modules/workflow/model/workflow.h | Workflow domain data, TaskNode configuration, Core ModelList and typed result associations |
| src/modules/task/application/catalog/task_module_catalog.h | Task ids and module availability |
| src/modules/task/application/catalog/model_spec_catalog.h | Model specifications offered by Qt |
| src/modules/workflow/model/workflow.cpp | Graph topology, connection validation and Node lifecycle in memory |
| src/modules/task/application/task_dataset_binding.cpp | Raw sample grouping, rebind comparison and binding synchronization |
| src/modules/task/application/task_workspace_service.cpp | Task operations, Core request construction, quality checks and result association |
| src/modules/workflow/storage/workflow_document.cpp | Exact Workflow schema, binding fields, typed results and Node asset references |
| src/modules/task/integration/storage/model_list_storage.cpp | Node ModelList serialization |
| src/modules/project/application/project_manager.cpp | Project save/open, resource paths and persistence coordination |
| src/modules/task/integration/exchange/annotation_codec.* and src/modules/task/integration/exchange/annotation_exchange.* | External annotation decoding, mapping and export |
| [Task pages](../src/modules/task/ui/pages) and annotation widgets | Editing, display and operation requests |
| src/modules/task/ui/monitoring/resource_monitor.cpp | Read-only system resource display shared by Training and Inference |
| src/modules/task/ui/task_workspace.cpp | Task page composition and shared result selection |
| src/app/main_window.cpp | Application shell: feature composition, page/Dock mounting, window layout and status presentation |
| src/modules/project/ui/project_module.cpp | Project commands, home page and metadata panel, using ProjectManager |
| src/modules/workflow/ui/workflow_module.cpp | Workflow canvas/tabs, node library, properties and Dataset-change confirmation |
| src/modules/model_management/application/model_actions.cpp | Named model import/export/delete/clear interactions for the owning TaskNode |
| src/modules/execution/application/execution_module.cpp | Existing training/inference Jobs, launch requests and progress/completion handling |
| src/modules/settings/ui/settings_module.cpp | Preferences and language commands |
| src/modules/diagnostics/activity_log.cpp | UTF-8 session log files, menu action and a separate log viewer |

Widgets keep viewport, drawing and selection state. Authoritative samples, classes, splits and results remain with the TaskNode. Core request construction belongs to TaskWorkspaceService.

The Training and Inference resource panel displays system-wide CPU and RAM use on Windows, and NVIDIA GPU utilization and VRAM through the driver NVML when its headers and library are available. It samples only while visible and does not store resource history in the Project.

Annotation tool definitions and gesture maintenance belong outside concrete Task widgets. A Task chooses applicable tools and owns target edits, undo history, validity and persistence; tool code only supplies interaction results. See the [annotation tool layering contract](annotation-tool-layering.md) before adding a task editor or a drawing tool.

Core geometry and task Result types can appear in domain data and persistence. Mathematical operations remain in Core. Qt's association layer relates those objects to sample, Model and Weight identities.

## 2. Ownership rules

Each TaskNode owns a Core ModelList. Model operations and result lookup begin with the current Workflow and Node. Model conversion adds a branch to its ModelEntry.

Inference lookup uses model_id + weight_id within the current TaskNode. Format and precision are branch metadata. Annotation, Inference and Results share one model/weight selection. An empty branch clears the prediction overlay.

Sample fields belong to TaskSampleBinding. Task-level settings belong to TaskNodeConfig or the settings structures it owns. AnnotationClass holds class metadata.

## 3. Binding rules

The existing samples' ordered image_paths and binding bucket_capacity are persisted binding facts. source_node_id comes from current edges and becomes zero on disconnect.

A fresh Task connects and synchronizes normally. A detached Task compares its snapshot with the candidate Dataset before an edge is added. Comparison checks identity and bucket membership only. Existing quality checks handle resource and task validity.

Destructive Dataset changes require confirmation at the application boundary. Clear sample-dependent state before synchronizing the new inventory. Preserve classes, split_settings, preprocessing, other Task configuration and models. Explicit annotation import supplies the receiving Task's new classes and targets.

Persist removed references before deleting the affected TaskNode's annotations/ and inference/ directories. Cleanup uses current edges and explicit Node paths.

## 4. Persistence rules

Writers, readers and exact-field lists in workflow_document.cpp define the same schema. Stored fields require matching serialization and parsing. Runtime diagnostics such as quality_issues are derived and are not persisted.

Resource paths use ProjectStorage portable/runtime conversion. Save As relocates internal paths, including detached binding paths and inference artifacts. External paths retain their external scope.

ModelListStorage owns the Node's models/model_list.json. Workflow result entries hold model/weight references. Full typed results belong in the exact branch's result.json. Referenced spatial targets belong in the owning Node's annotations/ directory.

Ordinary save uses atomic replacement per file. Inference replacement preserves the selected branch until its new result commits. See [project_spec.md](project_spec.md) and [inference-modelist-todo-guide.md](inference-modelist-todo-guide.md).

## 5. Extension and validation

UI features create their actions, views and interactions in their own module. Use
Qt's QMenu/QToolBar APIs for actions and MainWindow::mount_page/mount_dock for
views; connect feature requests explicitly in the composition code. Keep complex
operations in named methods, leaving short connections in the shell. Domain state
stays in ProjectManager, Workflow and TaskNode; do not add a UI state center.
Extract a service/coordinator only for a flow with real cross-module reuse.

Command availability belongs to its feature module. Query ExecutionModule for
busy state; it includes the existing operation identity through startup and queued
completion, as well as the Jobs. Guard project transitions at their named entry
points so home callbacks and recent-project commands obey the same rule as toolbar
actions. Refreshing a view must preserve the execution lock.

Write diagnostic text through the supplied log callback. ActivityLog owns the
file and its on-demand dialog; do not place raw logs, checkpoint paths or error
traces in workspace labels. Keep actionable status, progress and results in the
owning feature. Session logs stay in `logs/` beside the executable; do not use
AppData or the working directory. Training retains its existing per-run log file
inside the owning project.

WorkflowModule builds the canvas, node library and properties panel separately.
Tab selection, closing and renaming share its named activation method; connect
widget requests to these operations rather than repeating switch logic in lambdas.

Modules receive their domain references and output callbacks explicitly. The
shell owns built-in module lifetimes; mounted widgets belong to the Qt parent
tree, and actions belong to their feature QObject or menu. These are source-level
extension points. DLL loading and runtime unloading are not implemented. See the
[UI modularization plan](ui-modularization-plan.md) for boundaries and evidence.

A task representation starts in Core. Qt consumes the public type and service contract. Model specifications offered by Qt must exist in Core's model registration.

For a domain field, update its owning structure, writer, reader and exact-field list together. Map it into Core requests only where the selected operation consumes it. The relevant view edits the owning data through the service.

For result presentation, consume the task's complete Core Result. Keep prediction filtering, metric computation and mask rendering in the Core task module.

Source files are discovered through the CMake source glob. Tests require explicit studio_add_test registration. Extend the existing contract tests for binding identity, persistence, Node isolation and affected UI behavior.

Use the owner-approved preset and existing Core build target. Build and test commands are in [README.md](../README.md). Studio imports DeepLearning::Core from that build or an explicitly selected installation.
