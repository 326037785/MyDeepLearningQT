# Annotation import and project storage

This document defines the DeepLearningQT rules for importing external annotations and storing task annotations inside a Project. External dataset formats are input adapters. A successful import creates a Project-owned annotation snapshot that supports reopening, editing, training and inference independently of the original annotation files.

ROI and training exclusion masks are adjacent Core semantics. Changes to them require a current requirement that directly involves them.

## Ownership and canonical data

External sources such as LabelMe, COCO, YOLO, MVTec-style datasets and custom datasets may organize images and targets differently. Import support requires a task target and a deterministic mapping from that target to a Studio sample. Each source adapter resolves its own directory layout and annotation-file relationships.

The import pipeline is:

```text
External dataset
  -> source adapter / converter
  -> import representation
  -> sample and class mapping
  -> Project-owned annotation snapshot
  -> DeepLearning canonical task data
```

The committed Project snapshot is the authoritative annotation source. Source provenance may be retained for diagnostics.

Dataset owns the input image assets. Annotation import uses their existing paths. Portable image asset ownership is a separate Dataset lifecycle decision.

## Requirement and implementation priority

The annotation format serves DeepLearning Core's canonical task data. Qt modules serve product requirements and user workflow.

Core task semantics define Project annotation storage and import normalization: sample identity, bucket inputs, class identity, annotation state and the task's typed target. Qt widgets, services, file layouts and external-format adapters implement this contract.

DeepLearningQT provides sample selection, annotation editing, class editing, import dialogs and converter integration as required by the workflow. These modules consume and update the Project/Core data defined by the requirement.

The current requirement and this document define intended behavior; Core typed data defines the canonical backend representation; Qt and converter implementations satisfy that behavior. Source code and tests describe the current implementation. Requirement changes belong to the project owner.

Do not modify this specification or add wrappers, compatibility layers, duplicate fields, fallback branches or parallel old/new pipelines solely to preserve conflicting code. Such mechanisms require an independently valid current product need or an explicit project-owner decision.

When an existing interface or data flow conflicts with the requirement, report the conflict and change the implementation at the owning layer.

## Sample and bucket binding

One annotation binds to one Studio sample through its stable `sample_id`. A sample is one bucket and may contain one or more input image paths:

```text
sample_id
image_paths = [image_0, image_1, ..., image_n]
task annotation
```

The task defines the valid image count and input compatibility rules. A shared spatial annotation requires equal image width and height across the bucket's images as its baseline compatibility rule. The annotation belongs to the complete bucket.

Source adapters resolve filenames, relative paths, numeric ids or manifest records to the stable Studio `sample_id` used by committed annotations. Filename matching is a source-specific codec rule.

Tasks that need a different target for each image inside one bucket define the corresponding typed Core structure when that requirement arises.

## Sample acquisition and converter exchange

The import contract has exactly two sample acquisition origins: Qt-selected and converter-selected. Sequential and random sampling are modes within the same converter-selected pipeline. Both origins produce the same import representation before Project data is committed.

For Qt-selected samples, Qt sends a selection manifest for the `N` selected buckets. Each entry contains the stable `sample_id` and complete `image_paths` array. The converter locates the corresponding annotation using those paths and the external dataset rules. Annotation lookup uses path references and supports multi-image buckets.

For converter-selected samples, the converter selects `N` buckets using the requested sampling mode and returns those exact buckets with their annotations and complete `image_paths`. Qt establishes Project samples from the returned buckets. The converter's selection is authoritative; Qt must not repeat sampling or substitute a different image set.

The converter returns an explicit result for every requested or selected sample, including samples with missing or unusable targets. Each result identifies whether valid supervision exists. A missing annotation resource is `Unannotated` unless the source format or dataset rules explicitly establish a valid task target for that sample.

Import-local external ids help resolve correspondence. The Project binding key is `sample_id`: Qt-selected results preserve the original id; converter-selected results receive their Project identity from Qt before the annotation snapshot is committed.

## Annotation state

Annotation state describes supervision validity and has exactly two values:

- `Unannotated`: no valid supervision has been supplied for the sample.
- `Annotated`: the sample contains valid task supervision.

`BackgroundOnly` and `NeedsReview` are removed states. The two-state contract applies to import exchange, Project storage and Core metadata.

Task targets express supervision content:

- SG: `Annotated` with empty foreground regions represents an explicitly all-background sample. Background has label `0`.
- OD: `Annotated` with empty boxes represents an explicitly zero-object negative sample.
- CL: an unassigned `class_label = -1` is `Unannotated`. Classification has no segmentation-style background target.

Annotation state and task target together distinguish valid negative samples from missing supervision. A missing mask or label file represents valid supervision only when the source rules define the corresponding target deterministically.

## Project storage

Project storage uses JSON for compact metadata and separate spatial assets using the Core run representation where appropriate.

Core `jocelin::geo::Run` has the semantic tuple

```text
(row, start, end, label)
```

with the covered column interval `[start, end)`. Core `Region` is a collection of these runs. Project spatial assets store this run data. Physical run-file encodings may evolve while preserving Core `Run` semantics exactly.

Project metadata follows these rules:

- use flat records for scalar metadata and nesting for genuine one-to-many or structured relations;
- give each semantic value one authoritative field;
- store large run arrays and dense mask pixels in separate spatial assets;
- resolve bucket `image_paths` through `sample_id` and the owning Dataset;
- include target fields used by the task.

A conceptual sample record is intentionally shallow:

```json
{
  "sample_id": "sample-001",
  "annotation_state": "annotated",
  "class_id": "",
  "region_file": "annotations/sample-001.rle",
  "split": "train"
}
```

Each task uses the applicable fields from this example. Classification omits the region file. Unannotated samples and explicitly all-background SG samples may also omit it; the latter retain `annotation_state: annotated` and empty foreground regions. Annotation state is read from its authoritative field. OD/OCR storage fields follow their typed Core targets.

Image width and height are validation inputs obtained from the owning Dataset/image information. Loading validates spatial annotation against the bucket image geometry.

## Import representation

The import representation is a temporary adapter boundary that resolves external data into deterministic Studio samples and task targets. It may carry external ids, image paths, category ids and split names for mapping; Project storage retains the resolved task data.

Source format and task type are separate dimensions. Each adapter declares the task target it can produce, such as classification labels, semantic regions, detection boxes or OCR geometry/text.

Python converters own rich external-format conversion for LabelMe, COCO, YOLO, MVTec-style layouts and other ecosystem formats. They normalize source data into the Studio import representation. C++ owns the Project boundary, Project persistence and conversion into DeepLearning canonical structures. Small direct C++ codecs require explicit project-owner approval.

The converter retains source fields that contribute to sample resolution, task targets, class/split mapping or diagnostics.

## Class and split mapping

Import explicitly maps external category identifiers, source mask values, Project class identities and backend class labels. Each value has a defined role; the user-facing semantic name belongs to the Project class metadata.

Semantic segmentation uses background label `0` and concrete foreground classes. When a usable source mask lacks category names, the importer enumerates distinct foreground labels and creates readable placeholders `classA`, `classB`, `classC`, and so on. These names keep Project identity distinct from numeric source values such as `1`, `3` or `7`. Source mask values remain import mapping information; Core `Run::label` is an integer backend storage field. Segmentation must not introduce an `unknown` foreground class or use `-1` as a Region/RLE label.

A generated segmentation class has a stable Project identity and an editable semantic name. For example, id `classA` may initially have name `classA`; the user can rename it to `car`. Confirming the edit immediately updates Project metadata while preserving the existing Region/RLE data and class identity.

For classification, `-1` is the unknown/unassigned target sentinel. A real class named `unknown` requires an explicit dataset definition. Assigning a real class updates the Project class identity/label and makes the sample `Annotated`.

For a new Task, the source class schema may initialize the Task schema. For an existing Task, class conflicts require explicit mapping before commit; renaming, merging, reordering and equivalence decisions require user confirmation. The Project persists confirmed edits and mappings for subsequent loads.

External split metadata is optional input. Source train/validation assignments map to the current Project split when the correspondence is direct. External test partitions belong to application-level independent evaluation selection. Core `AiMetaData::Split` retains `Unassigned`, `Train` and `Validation`.

## Validation and commit

Import is a bounded transaction. Parsing and mapping complete before the current Project annotation snapshot is replaced or extended. A failed import preserves the previous snapshot.

Structural ambiguity rejects the import. Examples include an unknown task target, ambiguous sample correspondence, duplicate sample identity that cannot be resolved, or contradictory class definitions.

When sample identity is deterministic but sample-local annotation problems prevent valid supervision, return `Unannotated` with import diagnostics explaining the problem. Explicitly valid empty task targets remain `Annotated`.

Automatic format probing identifies formats with deterministic task semantics and sample correspondence. Ambiguous detection requires an explicit format or conversion path.

## Extension rule

DeepLearning owns canonical task data. SG uses `AiMetaData::regions` and CL uses `class_label`. OD, OCR and other tasks first define their typed geometry/metadata in DeepLearning. DeepLearningQT then imports, persists, edits and visualizes those Core structures directly.

The stable contract consists of Project-owned sample binding and task semantics. Supporting another external source means adding an adapter for an already-defined task target.
