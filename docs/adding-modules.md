# DeepLearning Studio module rules

A Studio module consumes Core task data and exposes it through the owning TaskNode. Core defines computation and typed task structures. Qt defines Project persistence, interaction and presentation.

The system definitions are in [project-overview.md](project-overview.md). Backend module rules are in [Core adding-modules.md](../../MyLibtorchLearning/docs/adding-modules.md).

## 1. Module responsibilities

| Module | Responsibility |
| --- | --- |
| src/workflow.h | Workflow domain data, TaskNode configuration, Core ModelList and typed result associations |
| src/task_module_catalog.h | Task ids and module availability |
| src/model_spec_catalog.h | Model specifications offered by Qt |
| src/workflow.cpp | Graph topology, connection validation and Node lifecycle in memory |
| src/task_dataset_binding.cpp | Raw sample grouping, rebind comparison and binding synchronization |
| src/task_workspace_service.cpp | Task operations, Core request construction, quality checks and result association |
| src/workflow_document.cpp | Exact Workflow schema, binding fields, typed results and Node asset references |
| src/model_list_storage.cpp | Node ModelList serialization |
| src/project_manager.cpp | Project save/open, resource paths and persistence coordination |
| src/annotation_codec.* and src/annotation_exchange.* | External annotation decoding, mapping and export |
| Annotation widgets and src/task_workspace_pages.cpp | Editing, display and operation requests |
| src/task_workspace.cpp | Task page composition and shared result selection |
| src/main_window.cpp | User confirmation, Project context and training/inference jobs |

Widgets keep viewport, drawing and selection state. Authoritative samples, classes, splits and results remain with the TaskNode. Core request construction belongs to TaskWorkspaceService.

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

A task representation starts in Core. Qt consumes the public type and service contract. Model specifications offered by Qt must exist in Core's model registration.

For a domain field, update its owning structure, writer, reader and exact-field list together. Map it into Core requests only where the selected operation consumes it. The relevant view edits the owning data through the service.

For result presentation, consume the task's complete Core Result. Keep prediction filtering, metric computation and mask rendering in the Core task module.

Source files are discovered through the CMake source glob. Tests require explicit studio_add_test registration. Extend the existing contract tests for binding identity, persistence, Node isolation and affected UI behavior.

Use the owner-approved preset and existing Core build target. Build and test commands are in [README.md](../README.md). Studio imports DeepLearning::Core from that build or an explicitly selected installation.
