# Annotation import and project storage

Annotation is the task target owned by a TaskNode for one complete sample bucket. External dataset formats are input adapters. A successful import commits annotations to the receiving TaskNode. Reopening, editing, training and inference use those committed targets independently of the original annotation files.

## Ownership and canonical data

External sources organize images and targets according to their own formats. Each adapter resolves its directory layout and annotation files. It maps the supported task target to complete Studio sample buckets.

The import pipeline is:

```text
External dataset
  -> source adapter / converter
  -> import representation
  -> sample and class mapping
  -> TaskNode-owned Annotation
  -> Core SampleSet for the selected operation
```

The receiving TaskNode owns the committed annotation. External format identifiers and source paths belong to import diagnostics.

DataNode owns the input image inventory. Annotation import uses input paths. Project Annotation stores typed targets and references; it does not copy input images during ordinary save.

## Sample and bucket binding

Each TaskNode owns its annotations and binds them to Dataset samples through stable `sample_id`. The Project locator is the owning Workflow, `node_id` and `sample_id`. A sample is one bucket and may contain one or more input image paths:

```text
sample_id
image_paths = [image_0, image_1, ..., image_n]
task annotation
```

The task defines the valid image count and input compatibility rules. A shared spatial annotation requires equal image width and height across the bucket's images as its baseline compatibility rule. The annotation belongs to the complete bucket.

Source adapters resolve filenames, relative paths, numeric ids or manifest records to the stable Studio `sample_id` used by committed annotations. Filename matching is a source-specific codec rule.

## Sample acquisition and converter exchange

The import rules define exactly two sample acquisition origins: Qt-selected and converter-selected. Sequential and random sampling are modes within the same converter-selected pipeline. Both origins produce the same import representation before TaskNode data is committed.

For Qt-selected samples, Qt sends a selection manifest for the `N` selected buckets. Each entry contains the stable `sample_id` and complete `image_paths` array. The converter locates the corresponding annotation using those paths and the external dataset rules. Annotation lookup uses path references and supports multi-image buckets.

For converter-selected samples, the converter selects `N` buckets using the requested sampling mode and returns those exact buckets with their annotations and complete `image_paths`. Qt establishes Project samples from the returned buckets. The converter's selection is authoritative; Qt must not repeat sampling or substitute a different image set.

The converter returns an explicit result for every requested or selected sample, including samples with missing or unusable targets. Each result identifies whether valid supervision exists. A missing annotation resource is `Unannotated` unless the source format or dataset rules explicitly establish a valid task target for that sample.

Import-local external ids help resolve correspondence. The Project binding key is `sample_id`: Qt-selected results preserve the original id; converter-selected results receive their Project identity from Qt before Annotation is committed.

## Annotation state

Annotation state describes supervision validity and has exactly two values:

- `Unannotated`: no valid supervision has been supplied for the sample.
- `Annotated`: the sample contains valid task supervision.

Each task owns target validity and annotation-state transitions. Classification requires an active, valid class. Semantic segmentation supports valid foreground regions and explicitly confirmed pure background as `Annotated` with empty regions. Empty regions alone leave an unfinished sample `Unannotated`.

Qt provides task-owned annotation actions. Classification disables `Set as empty`; segmentation asks the user to confirm pure background before clearing foreground and marking the sample `Annotated`.

The complete identity, state, target and interaction rules are defined in [annotation_spec.md](annotation_spec.md). Source adapters distinguish an explicitly valid zero mask from a missing target resource.

## Project storage

Workflow documents save persistent Node IDs, connections, allocation sequence and Node configuration. source_node_id is reconstructed from current edges. Each TaskNode owns `config.dataset_binding.classes` and `config.dataset_binding.samples`. Annotation scalar fields live in that Node's sample records; SG spatial data lives under the same Node's asset directory.

```text
project.json
workflows/<workflow_id>.json
nodes/<workflow_id>/<node_id>/annotations/<encoded_sample_id>.regions.json
```

Node IDs are unique within their Workflow. The asset namespace includes the Workflow ID, so identical Node IDs in different Workflows retain separate assets. Filenames use lowercase hexadecimal encoding of the stable sample ID's UTF-8 bytes, preserving identity on case-insensitive filesystems and avoiding reserved filesystem names.

Sample records store `sample_id`, ordered complete `image_paths`, `annotation_state`, selection, split assignments and the existing training-exclusion reference. The binding also saves `bucket_capacity` as its last verified bucket configuration (zero with no samples means fresh). Task target fields follow the task:

| Task | Annotation target fields | Spatial data |
| --- | --- | --- |
| Classification | `class_id` | Stable semantic class reference. Backend labels are calculated when consumed. |
| Semantic Segmentation | `region_file` | Reference relative to `nodes/<workflow_id>`, confined to the owning Node and sample. Empty Regions use an empty reference. |

The Node's Task type comes from its configuration. Saved input paths are the Task's last verified bucket snapshot. A graph edge establishes the current runtime source_node_id; disconnect sets it to zero without changing the snapshot or Annotation. Reconnect compares sample_id sets, ordered bucket paths and bucket_capacity before connecting. No historical source reference is stored.

Example SG record within Node `4`:

```json
{
  "sample_id": "sample-001",
  "image_paths": ["project:dataset/sample-001.png"],
  "selected": true,
  "training_mask_path": "",
  "split": "train",
  "split_manual": true,
  "annotation_state": "annotated",
  "region_file": "4/annotations/73616d706c652d303031.regions.json"
}
```

The referenced spatial asset contains an array of Regions. Each Region is an array of Core runs `[row, start, end, label]`; columns cover `[start, end)`. Region order and run order are preserved exactly. For example:

```json
[[[2, 3, 7, 1], [3, 3, 7, 1]]]
```

An unfinished empty target and a confirmed pure-background target both use `region_file: ""`. Their stored `annotation_state` distinguishes `Unannotated` from `Annotated`. Recovery reads the explicit target reference; a stale spatial file has no effect on an empty reference.

## Commit and recovery

Import adapters decode external masks into Core Regions before committing Annotation to the receiving TaskNode. Its binding retains resolved `sample_id`, class identity, state and typed target. Source mask paths belong to the temporary import representation. After import, Project Annotation uses the committed target independently of the original mask files.

The SG editor converts its canvas pixels to Core Regions when applying edits. `Set as empty` clears Regions and confirms `Annotated`. These operations update the authoritative TaskNode binding through its service.

Workflow serialization prepares JSON and Region file contents in memory. Region encoding preserves the task target. Project save serializes and validates its module documents before creating directories or writing files. It writes Node assets and module documents before the Project entry. Each file uses QSaveFile replacement. Save As copies Node assets and retains Node and sample IDs.

Preparation errors leave existing Project files unchanged. During the write phase, atomic replacement applies to each file; a later write failure can leave earlier files updated.

Project open restores Node IDs and connections, creates fresh runtime Task IDs, and resolves Region assets under `nodes/<workflow_id>`. Connecting restored Nodes verifies their saved bucket binding before ordinary synchronization. Detached Tasks retain their saved paths and targets without quality reinterpretation until reconnected. Connected Tasks run target validity checks after Project input paths are resolved. Each TaskNode receives its own binding and target values.

`quality_issues` contains runtime diagnostics derived from Dataset binding, current input resources and task target checks. For connected Tasks, Project recovery rebuilds these findings after restoring bindings and resolving paths; Workflow sample records omit this field. Diagnostics carried by the temporary import representation remain within the import flow.

**Annotation loading**: The loader reports malformed JSON, invalid annotation references and missing referenced Region assets. Task-specific semantic checks determine target validity. Model asset loading follows [Project rules](project_spec.md). Identity rules appear in [node_identity.md](node_identity.md); task rules appear in [annotation_spec.md](annotation_spec.md).

## Import representation

The import representation is a temporary adapter boundary that resolves external data into deterministic Studio samples and task targets. It may carry external ids, image paths, category ids and split names for mapping; Project storage retains the resolved task data.

Source format and task type are separate dimensions. Each adapter declares the task target it produces and resolves its inputs before commit.

AnnotationExchange handles Studio bundles and iLabelled segmentation packages. The codec registry also reads dense-mask directories with sibling images/ and masks/ folders. Codecs describe external samples and categories. AnnotationBindingMapper resolves them into DatasetConfig, TaskDatasetBinding and Core targets. External converters can produce the same import representation. Project persistence belongs to the receiving TaskNode.

The converter retains source fields that contribute to sample resolution, task targets, class/split mapping or diagnostics.

## Class and split mapping

Import explicitly maps external category identifiers, source mask values, Project class identities and backend class labels. Each value has a defined role; the user-facing semantic name belongs to the Project class metadata.

Semantic segmentation uses background label `0` and concrete foreground classes. When a usable source mask lacks category names, the importer enumerates distinct foreground labels and creates readable placeholders `classA`, `classB`, `classC`, and so on. These names keep Project identity distinct from numeric source values such as `1`, `3` or `7`. Source mask values remain import mapping information; Core `Run::label` is an integer backend storage field. Segmentation must not introduce an `unknown` foreground class or use `-1` as a Region/RLE label.

A generated segmentation class has a stable Project identity and an editable semantic name. For example, id `classA` may initially have name `classA`; the user can rename it to `car`. Confirming the edit immediately updates Project metadata while preserving the existing Region/RLE data and class identity.

For classification, `-1` is the unknown/unassigned target sentinel. A real class named `unknown` requires an explicit dataset definition. Assigning a real class updates the Project class identity/label and makes the sample `Annotated`.

Explicit annotation-package import replaces the receiving Task's classes and sample annotations after confirmation. It retains that Task's split_settings and preprocessing. Other Tasks connected to the same DataNode retain their classes and settings. They receive fresh samples from the replacement Dataset.

External split metadata is optional input. Source train/validation assignments map to the current Project split when the correspondence is direct. External test partitions belong to application-level independent evaluation selection. Core `AiMetaData::Split` retains `Unassigned`, `Train` and `Validation`.

## Validation and commit

Parsing and mapping complete before the user confirms Dataset replacement. Parse failure and cancellation preserve existing data. After confirmation, the application clears Dataset-dependent references for currently connected Tasks and saves them. It then removes their annotations/ and inference/ directories, installs the replacement Dataset and imports the receiving Task's targets. File cleanup failures report exact paths.

Structural ambiguity rejects the import. Examples include an unknown task target, ambiguous sample correspondence, duplicate sample identity that cannot be resolved, or contradictory class definitions.

When sample identity is deterministic but sample-local annotation problems prevent valid supervision, return `Unannotated` with import diagnostics explaining the problem. Explicitly valid empty task targets remain `Annotated`.

Automatic format probing identifies formats with deterministic task semantics and sample correspondence. Ambiguous detection requires an explicit format or conversion path.

## Extension rule

DeepLearning owns canonical task data. SG uses AiMetaData::regions and CL uses class_label. Task extensions define their typed data and consumers in Core before Qt imports, persists, edits or visualizes them.

The stable contract consists of TaskNode-owned sample binding and Core task semantics. Supporting another external source means adding an adapter for an already-defined task target.

## Dataset lifecycle

Disconnect removes the Dataset edge and sets source_node_id to zero. It retains samples, their Annotation, ordered input paths and assignments. Binding bucket_capacity is retained too. ModelList and Inference remain with the TaskNode.

A detached Task compares a candidate Dataset with this binding snapshot before connection. The sample_id sets, ordered complete image_paths and bucket_capacity must match. Comparison normalizes paths but does not check input quality. Mismatch leaves all retained data unchanged.

DataNode deletion or destructive replacement affects only currently connected Tasks. It clears their samples and Inference while retaining classes, split_settings, preprocessing, other Task configuration and models. Persisted references must disappear successfully before their exact Node annotations/ and inference/ directories are deleted. Cleanup failures report paths. Detached Tasks and external source images/masks remain intact.

The complete lifecycle and result-selection rules are in [inference-modelist-todo-guide.md](inference-modelist-todo-guide.md).
