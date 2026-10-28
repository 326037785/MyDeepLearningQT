# AiMetaData in DeepLearningQT

AiMetaData is Core's canonical metadata for one complete sample bucket. SampleSet contains these samples. Qt's TaskDatasetBinding stores the owning TaskNode's persistent sample associations and task targets.

Core field semantics are defined in [Core ai_metadata.md](../../MyLibtorchLearning/docs/ai_metadata.md).

## 1. Task data

TaskWorkspaceService builds AiMetaData from the current TaskNode binding. Core receives:

- all ordered input image paths in the bucket;
- spatial annotation, ROI and exclusion regions;
- the task's class target;
- sample selection, split and split mode;
- annotation state and sample identity.

SG annotation uses Core Regions. CL annotation references a stable class_id in Qt and becomes a Core class_label at the consumption boundary.

A segmentation mask or canvas QImage is converted into AiMetaData::regions. A training exclusion mask becomes AiMetaData::mask. image_paths contains input image paths only.

Core applies geometric augmentation to the image, annotation and exclusion mask together. Boolean exclusion is applied after augmentation.

## 2. Binding facts

Each TaskSampleBinding persists sample_id and complete ordered image_paths. TaskDatasetBinding persists bucket_capacity. These existing fields form the last verified bucket snapshot.

source_node_id expresses the current Dataset edge. It is zero after disconnect and is not a persisted source identity. Detached Tasks retain samples, targets and assignments.

Rebind comparison checks sample_id sets, ordered bucket paths and bucket_capacity. It uses Project/runtime path normalization. It does not perform quality checks. Synchronization and refresh_quality handle current input and target validity after a connection is established.

Classes, split_settings and preprocessing belong to Task configuration. Confirmed DataNode deletion/replacement clears samples and Inference only for currently connected Tasks. The class schema, settings and models remain. Explicit annotation-package import supplies its receiving Task's new classes and targets.

## 3. Models and results

Each TaskNode holds Core ModelList. ModelEntry holds model identity and specification. ModelWeight holds branch identity, format and artifact/runtime metadata. Models are independent of the current Dataset binding.

Core task Result objects hold predictions, task parameters, flags and metrics. Qt associates them with source_sample_id and the owning Node's model_id + weight_id. Each pair has one latest result. Core Result structures do not define Project topology or storage ownership.

Annotation, Inference and Results share the current Model/Weight selection. A branch without a result clears the inference overlay.

## 4. Presentation and validity

Widgets retain viewport transforms, temporary drawing handles, display rows and selection. Authoritative sample data remains in the TaskNode binding.

Annotation has two states: Unannotated and Annotated. Classification requires a valid class. SG distinguishes an unfinished empty target from confirmed pure background through annotation_state.

Core owns task geometry, result filtering, metric computation and mask rendering. Qt owns image loading, editing interaction, persistence and presentation.

Identity and ownership are defined in [node_identity.md](node_identity.md). Annotation rules are in [annotation_spec.md](annotation_spec.md). Lifecycle and result selection are in [inference-modelist-todo-guide.md](inference-modelist-todo-guide.md).
