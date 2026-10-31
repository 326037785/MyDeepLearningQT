# DeepLearningQT

`DeepLearningQT` is the Qt frontend for `DeepLearning::Core`. Qt owns Project interaction, Workflow topology, Node assets and UI selection. Core owns task data, model computation, training, inference and metrics.

## Project and Node ownership

A Project contains Workflows. Each Workflow contains persistent Nodes and connections.

```text
Project
└── Workflow
    ├── DataNode
    │   └── input inventory and sample buckets
    └── TaskNode
        ├── Task configuration
        ├── Dataset binding and Annotation
        ├── ModelList
        │   └── ModelEntry
        │       └── ModelWeight
        └── Inference
            └── model_id + weight_id -> latest result
```

A DataNode can connect to several TaskNodes. Each TaskNode owns its Annotation, ModelList and Inference independently. Nodes of the same task type also have separate assets and model-number sequences.

`workflow_id + node_id` locates a Node in the Project. `task_id` identifies a runtime Task instance. `sample_id` identifies a complete sample bucket. See [node_identity.md](node_identity.md).

## Dataset binding

A DataNode owns its input inventory, stable sample ids and `bucket_capacity`. One sample can contain several ordered image paths.

`TaskDatasetBinding` belongs to a TaskNode. Its samples contain `sample_id`, complete ordered `image_paths`, Annotation, selection, split assignments and training-mask references. Its `bucket_capacity` records the last verified bucket configuration. These fields form the retained binding snapshot.

`source_node_id` expresses the current connection. Workflow edges determine its value. It is zero without a Dataset edge and is not persisted as a source reference.

Classes, split settings and preprocessing are Task configuration. Annotation and input associations belong to samples. Core receives the resulting `SampleSet` and `AiMetaData` through `TaskWorkspaceService`.

### Connection lifecycle

A fresh binding has no samples and has `bucket_capacity == 0`. It connects and synchronizes normally. A verified empty Dataset has a positive bucket capacity.

A detached Task has no Dataset edge and retains its binding snapshot. It also retains Annotation, Inference, ModelList and weights. Disconnecting does not clear these assets. Deleting a DataNode has no effect on Tasks that are already detached.

Before reconnecting a detached Task, compare the candidate Dataset with its retained binding:

- the sample_id sets must match;
- each sample must have the same complete image_paths, including count and order;
- bucket_capacity must match.

Comparison normalizes paths using the Project and runtime path conventions. It does not check file availability or task compatibility. A match permits connection and ordinary synchronization. A mismatch leaves the edge absent and retained data unchanged. Existing quality checks diagnose missing files, invalid inputs and task targets.

### DataNode deletion and replacement

Deletion and destructive replacement require user confirmation. The affected Tasks are determined by current edges. Cancelling changes neither topology nor assets.

Confirmed cleanup removes sample bindings and their Annotation, selection, split assignments and training-mask associations. It also clears Inference records, status and input/output path associations. Classes, split settings, preprocessing, other Task settings, ModelList and weights remain.

Cleared references must save successfully before the affected TaskNode's `annotations/` and `inference/` directories are removed. Cleanup errors report the exact paths. External input images and training masks are retained.

Replacement then writes the new Dataset and synchronizes fresh bindings. Reviewing an unchanged image inventory preserves Annotation and Inference. Annotation-package import explicitly supplies the receiving Task's new classes and annotations. Other connected Tasks retain their class configuration and receive fresh samples.

## ModelList

`jocelin::model::ModelList` is the Core model container held by each `TaskNodeConfig`. Training, Inference and Results access that Node's same list through `TaskWorkspaceService`.

One `ModelEntry` represents one logical model created by training or external import. New, Inherit and Transfer create rows. Pause and Resume continue an unfinished row. Model conversion adds `ModelWeight` branches to the same row and records their source through `parent_weight_id`.

The owning TaskNode determines the list's task type. Rows may have different model indices, scales, input sizes and class schemas. Compatibility is checked for the selected operation. The UI's model Capacity means `model_scale`; Dataset `bucket_capacity` means input images per sample.

Each ModelWeight has a branch id, format, precision, role and artifact/runtime metadata. The browser groups branches under LibTorch, ONNX, OpenVINO and TensorRT columns. A concrete branch is selected by weight_id.

Import creates a row in the receiving Node. Core matches external LibTorch/PyTorch parameters by registered name and shape. Unmatched values retain initialization; zero matches are valid for import. Only Resume requires a complete training Checkpoint.

`runtime.training_model_id` refers to the row controlled by the current TaskNode. Training progress and checkpoint ownership stay with that row. Export copies selected model assets outward.

**Model deletion**: The application removes the ModelEntry, its model assets,
controlled-training association, all inference records for its model_id and
`inference/<model_id>/`.

**Weight deletion**: The application removes one ModelWeight, its assets and the
inference records and directory for its model_id + weight_id. It preserves other
branches and the model training status, clears parent_weight_id on direct children
and permits an empty model row.

**Clear ModelList**: The application removes the current TaskNode's models,
model assets, inference records and inference/ directory. ProjectManager saves the
ModelList and Workflow references; Qt refreshes selection. Other Nodes retain their assets.

**Paused**: Qt registers last.pt for Resume and Infer. Resume requires a valid
LibTorch training checkpoint in the Paused model.

**Completed**: Qt registers best.pt for Infer.

**Stopped**: Core ends the run at a batch boundary and retains checkpoints from
complete epochs. Qt registers an available best.pt for Infer and keeps the Stopped
status. Stopping before the first completed epoch leaves the weight collection empty.

## Inference and result selection

Inference belongs to a TaskNode. Within that Node, `model_id + weight_id` identifies one latest result. The ids come from Core ModelEntry and ModelWeight. Format and precision describe a branch; they do not identify a result.

`ResultReference` associates source_sample_id with a complete Core ClsResult or SgResult. The Core image path distinguishes image records within a bucket. Qt keeps the typed result and its application association together.

Annotation, Inference and Results share the TaskWorkspaceService model/weight selection. Annotation contains a view of the current Node's ModelList and a branch selector. Clicking a model changes model_id. Choosing a branch changes weight_id.

All result views use the current TaskNode and exact selected pair. A branch without a result shows an empty result state and clears the prediction overlay. It does not display another branch's result. Runtime availability controls execution, while saved branch results remain selectable for viewing.

### Successful replacement

Inference saves ordinary Project state before execution. The worker writes new artifacts to a temporary directory beside the selected branch. The current valid branch remains accessible during execution.

On success, Qt prepares result.json and relocates artifact paths to the final branch. It publishes the branch and Workflow reference using a temporary backup for rollback. Memory changes after commit. Inference or commit failure preserves the valid result and its files. Temporary output and backup directories serve only this replacement operation.

Rerunning a branch replaces only its latest result. Other models, branches and Nodes keep their results.

## Persistence

```text
project.json
workflows/<workflow_id>.json
nodes/<workflow_id>/<node_id>/
├── annotations/<encoded_sample_id>.regions.json
├── models/model_list.json
└── inference/<model_id>/<weight_id>/
    ├── result.json
    └── prediction artifacts
```

Workflow configuration saves Task bindings and model/weight result references. ModelListStorage saves `models` and `next_number` in each Node's model_list.json. Each result.json contains the selected model/weight ids, format metadata, aggregate metrics and typed sample results.

ProjectManager coordinates these modules. Ordinary save prepares serialization before writing. Each file uses QSaveFile replacement, and the Project entry is written last. Atomicity is per file. See [project_spec.md](project_spec.md).

**Project open**: ProjectManager restores Nodes, bindings, result assets and each
Node's ModelList. It checks result associations within the owning Node and checks
artifact paths within their branch. Detached bindings retain paths and Annotation.

**Missing model assets**: ProjectManager returns diagnostics for missing weights
and fully unavailable models while preserving ModelList entries and inference.
A missing model_list.json opens with an empty Node ModelList and retained inference
references. Qt asks the user to clean up the missing dependencies through the
ordinary model/weight deletion path. Cancel preserves Project data and files.
Ordinary save preserves a missing catalog while orphaned inference references remain.

**Invalid model metadata**: ProjectManager rejects malformed JSON, invalid fields,
duplicate identities and invalid lifecycle metadata. Other asset and reference
validation follows the owning module's rules.

Save As copies Project assets and relocates internal resource paths. Workflow, Node, sample, model and weight ids remain stable. External resource references remain external.

## Core task data and computation

Core owns the task representations in AiMetaData, including bucket inputs, annotation, ROI, exclusion regions, class labels, selection, splits and annotation state. Qt owns drawing, viewport state and display rows. Persistent business data remains in the TaskNode binding. See [ai_metadata.md](ai_metadata.md) and [annotation-import-and-storage.md](annotation-import-and-storage.md).

SG Annotation uses Core Regions. The editor converts canvas pixels when applying edits. Core owns label scanning, region operations, tensor conversion and result-mask rendering. Qt loads images and presents diagnostics.

Qt annotation tools have task-independent definitions and gesture maintenance; each Task composes only the tools it needs and owns target edits, undo history and validation. The [annotation tool layering contract](annotation-tool-layering.md) records the implemented ownership boundary and behavior constraints.

Training uses LibTorch on CPU or a selected GPU. Inference combines the selected weight with a compatible device and available executor. LibTorch and ONNX support CPU/GPU routes, OpenVINO supports CPU, and TensorRT supports GPU. Qt filters unsupported routes. Core reports device adjustments while preserving the selected weight; Qt displays the warning.

Qt sends task filtering inputs through InferenceRequest::config. Core produces the effective parameters, flags, predictions and metrics in task Result objects. SG previews use SgResult::render_interest_mask(). Classification rejection is -1 and displays as Unknown.

Training validation uses Mean IoU for SG and Accuracy for CL. Inference consumes Core's task metrics, including Precision, Recall and F1. Label-dependent metrics require valid supervision. An unavailable metric remains unavailable. Qt does not recalculate metrics.

Core's pipeline exceptions carry execution errors to Qt. Pause, Stop and Completed are training lifecycle outcomes. Extension rules are in [adding-modules.md](adding-modules.md).
