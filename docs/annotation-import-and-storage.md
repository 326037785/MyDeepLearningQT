# Annotation import and project storage

This document defines the DeepLearningQT rules for importing external annotations and storing task annotations inside a Project. External dataset formats are input adapters. After a successful import, the Project owns its annotation snapshot and does not depend on the original annotation files.

## Ownership and canonical data

External sources such as LabelMe, COCO, YOLO, MVTec-style datasets and custom datasets may organize images and targets differently. Import support is based on whether the source can provide the task target and a deterministic mapping from that target to a Studio sample. Support is not defined by directory names or by requiring one annotation file per image.

The import pipeline is:

```text
External dataset
  -> source adapter / converter
  -> import representation
  -> sample and class mapping
  -> Project-owned annotation snapshot
  -> DeepLearning canonical task data
```

The source format stops being authoritative after the Project snapshot is committed. Reopening, editing, training and inference consume Project-owned data. Source provenance may be retained for diagnostics, but the original annotation layout is not another source of truth.

Image ownership remains a Dataset concern. Importing annotations does not by itself require copying the source images into the Project. Portable image asset ownership, if added later, is a separate Dataset lifecycle decision.

## Requirement and implementation priority

The annotation format exists to serve DeepLearning Core's canonical task data. Qt modules exist to serve product requirements and user workflow. These two responsibilities must not be reversed.

Project annotation storage and import normalization are designed from the task semantics that Core must consume: sample identity, bucket inputs, class identity, annotation state and the task's typed target. A Qt widget, an existing service boundary or a convenient file layout must not redefine those semantics merely because the current implementation is easier to reuse that way. External formats are adapters into this model; they are not allowed to become the model.

DeepLearningQT is organized around the required workflow. Qt may provide sample selection, annotation editing, class editing, import dialogs, review surfaces and converter integration as needed by the product. These modules consume and update the Project/Core data defined by the requirement; their current class layout, helper functions and historical assumptions are implementation details rather than constraints on the requirement.

When documentation, an explicit project-owner decision and existing code disagree, use the following precedence: the current requirement and this document define the intended behavior; Core typed data defines the canonical backend representation; Qt and converter implementations are then changed to satisfy that behavior. Existing source code and tests describe the current implementation only where they agree with the current requirement.

Do not reverse this precedence by deriving a new requirement from an old function signature, an existing JSON shape, a widget assumption or a test fixture. In particular, do not modify this specification merely to preserve existing code, and do not add wrappers, compatibility layers, duplicate fields, fallback branches or parallel old/new pipelines solely to avoid changing code that conflicts with the documented behavior. Such mechanisms require an independently valid current product need or an explicit project-owner decision.

If an implementation cannot satisfy a documented rule without changing an existing interface or data flow, report the conflict and change the implementation at the owning layer. Do not silently weaken the rule, reinterpret its semantics or add an intermediate representation that makes the old implementation appear compliant.

## Sample and bucket binding

One annotation binds to one Studio sample through its stable `sample_id`. A sample is one bucket and may contain one or more input image paths:

```text
sample_id
image_paths = [image_0, image_1, ..., image_n]
task annotation
```

The valid image count is task-specific. Multiple images in the same bucket must satisfy the task's input compatibility rules; the baseline rule for a shared spatial annotation is equal image width and height. An annotation belongs to the bucket, not to an arbitrary physical filename inside that bucket.

External formats may identify samples by filenames, relative paths, numeric ids or manifest records. A source adapter may use those values while resolving the import, but committed Project annotation data binds to the stable Studio `sample_id`. Filename equality is therefore a codec rule when applicable, not a global annotation rule.

If a future task requires a different target for each image inside one bucket, that task must define the corresponding typed backend structure. The common annotation contract does not pre-allocate a universal per-image target array.

## Sample acquisition and converter exchange

The same import contract supports both Qt-selected and converter-selected samples. These are two selection origins, not two annotation pipelines. Both paths must end in the same import representation before Project data is committed.

When Qt has already imported or selected `N` samples, Qt sends the converter a selection manifest. Each selected bucket provides its stable `sample_id` and the complete `image_paths` array. The converter uses those paths and the external dataset rules to locate the corresponding annotation. Qt does not copy the image merely to ask for annotation lookup, and the converter must not assume that one bucket contains only one image.

When the converter selects `N` samples itself, either sequentially or randomly, the converter returns the exact selected buckets together with their annotations. Each returned bucket contains the complete `image_paths` used for that selection. Qt binds those returned buckets to Project samples; it must not rescan the dataset, repeat the random selection, or infer a different image set from filenames. Sampling order and random sampling are source-selection behavior only and do not create a different Project annotation format.

Every requested or selected sample must have an explicit import result. The converter must not silently omit samples for which no usable target was found. The result must distinguish at least valid annotation, explicit background-only/negative annotation, unannotated data and data requiring review through the existing annotation-state semantics. A missing annotation resource is `Unannotated` unless the source format or dataset rules explicitly establish that its absence means a valid negative sample.

The converter may use an import-local external id while resolving correspondence, but that id is not a replacement for the Project `sample_id`. For Qt-selected samples, the original `sample_id` is the round-trip binding key. For converter-selected samples, Qt establishes the Project sample identity from the returned bucket before committing the Project-owned annotation snapshot.

## Annotation state

Annotation state describes supervision semantics and is independent from the presence of an annotation file. The existing states keep the following meanings:

- `Unannotated`: no valid supervision has been supplied for the sample.
- `BackgroundOnly`: the sample has been explicitly annotated as containing no foreground target.
- `Annotated`: the sample contains valid task supervision.
- `NeedsReview`: annotation information exists but cannot currently be accepted as trusted supervision.

A valid negative sample is not `Unannotated`. Examples include an all-background segmentation target, an object-detection image explicitly containing zero objects, and an anomaly dataset sample explicitly marked as good/background. A missing mask or missing label file must not be silently interpreted as a valid negative sample unless the source format itself provides that meaning.

## Project storage

Project storage keeps task annotation compact and independent from external file formats. JSON stores metadata; spatial data is stored separately using the Core run representation where that representation is appropriate.

Core `jocelin::geo::Run` has the semantic tuple

```text
(row, start, end, label)
```

with the covered column interval `[start, end)`. Core `Region` is a collection of these runs. Segmentation regions, ROI and training exclusion regions can therefore be persisted as Project-owned run data without storing dense mask images. The physical run-file encoding may evolve independently, but it must preserve the Core `Run` semantics exactly.

Project metadata follows these rules:

- prefer flat JSON records;
- one semantic value has one authoritative field;
- use nesting only when a real one-to-many or structured relation cannot be represented clearly without it;
- do not mirror the same value at outer and inner levels;
- do not embed large run arrays or dense mask pixels in JSON;
- do not duplicate Dataset `image_paths` in Project annotation metadata when `sample_id` already resolves the bucket;
- keep task-specific target fields only when the task actually uses them.

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

This is a storage-shape example, not a requirement that every task use every field. Classification does not need a region file; an unannotated or background-only sample may not need one either. Future OD/OCR structures are added only after their typed Core target exists.

Image width and height are validation inputs, not metadata that should be repeated through several annotation layers. Spatial annotation must be compatible with the bucket image geometry, but dimensions should be read from the owning Dataset/image information or validated when the Project data is loaded.

## Import representation

The import representation is an adapter boundary, not another Project data model. It may temporarily contain information that Project storage does not retain, including external ids, external image paths, source category ids and source split names. Its purpose is to resolve the external dataset into deterministic Studio samples and task targets.

Source format and task type are separate dimensions. COCO, LabelMe or another format is not itself a task. A source adapter must identify which task target it can produce, such as classification labels, semantic regions, detection boxes or OCR geometry/text.

Rich external-format conversion belongs primarily in the Python converter layer. Python may parse LabelMe, COCO, YOLO, MVTec-style layouts and other ecosystem formats and normalize them into the Studio import representation. C++ remains responsible for the Project boundary, Project-owned persistence and conversion into the corresponding DeepLearning canonical structures. Small direct C++ codecs may exist when they are simple and useful, but DeepLearningQT is not the general external-format conversion library.

The converter does not need to preserve unrelated source metadata. Workspace urls, download metadata, augmentation descriptions and other fields that do not affect the task target are not copied merely because the source format provides them.

## Class and split mapping

External category identifiers are source identifiers only. They must not be assumed to equal the Project class id, segmentation mask value or backend class label. Import resolves these identities explicitly. A numeric source value is not itself the category semantic name.

For semantic segmentation, background and foreground classes have explicit meanings. Project segmentation reserves background as background; it does not introduce an `unknown` foreground class. If a source provides a usable mask but omits category names, the importer enumerates the distinct foreground labels and initializes readable placeholder classes as `classA`, `classB`, `classC`, and so on. The placeholder is deliberately non-numeric so a later session does not reinterpret source mask values such as `1`, `3` or `7` as Project category identities. The source mask value remains import mapping information. Core `Run::label` remains an integer storage field because that is the backend representation, but the integer is not the category's user-facing semantic identity.

A generated segmentation class has a stable Project identity and an editable semantic name. For example, a class may initially use id `classA` and name `classA`; after review the user may change its name to `car` or another real semantic description. Confirming a class-name edit writes the Project metadata immediately. Renaming a class does not modify the spatial Region/RLE data because the labelled geometry has not changed. Any implementation that rewrites the mask merely because the display semantic changed is outside this contract.

For classification, `-1` represents an unknown or unassigned sample target. It is not a real category and must not be added to the class schema as an `unknown` class unless a dataset explicitly defines such a class as part of its own semantics. Classification has no segmentation-style background label. Once the user assigns a real class, the Project metadata is updated to the corresponding class identity/label.

When importing into a new Task, the source class schema may initialize the Task class schema. When importing into an existing Task, conflicting source and Project classes require explicit class mapping before commit. The importer must not rename, merge, reorder or infer equivalence between existing Project classes by guesswork. User-confirmed class edits and mappings become Project-owned metadata and must be persisted rather than reconstructed from the original dataset on the next load.

External split metadata is optional input. Source train/validation assignments may map to the current Project split semantics when the mapping is direct. External test partitions are not converted into a new `AiMetaData::Split` value; current Core semantics keep independent evaluation selection outside the intrinsic sample split.

## Validation and commit

Import is a bounded transaction. Parsing and mapping complete before the current Project annotation snapshot is replaced or extended. A failed import must not leave a partially committed annotation set.

Structural ambiguity rejects the import. Examples include an unknown task target, ambiguous sample correspondence, duplicate sample identity that cannot be resolved, or contradictory class definitions.

Sample-local problems may remain visible as warnings or `NeedsReview` when the sample identity and intended target are still deterministic. Examples include one damaged image, malformed geometry in one sample or a missing resource whose absence is not semantically equivalent to a negative annotation.

Automatic format probing is a convenience only. It must not invent task semantics from a vaguely matching directory layout. When detection is ambiguous, the importer requires an explicit format or conversion path.

## Extension rule

DeepLearning remains the owner of canonical task data. Current SG uses `AiMetaData::regions` and CL uses `class_label`. OD, OCR and other tasks must first add their real typed geometry/metadata to DeepLearning. DeepLearningQT then imports, persists, edits and visualizes those structures without introducing a universal annotation AST or a parallel backend-style target model.

The stable contract is therefore the Project-owned sample binding and task semantics, not the list of supported external products. Adding a new external source means adding an adapter that can produce an already-defined task target; it does not create a new Project annotation model.

## Guard rules

The rules in this document are product requirements, not suggestions for an implementation to reinterpret. Future work must preserve them unless the project owner explicitly changes the requirement.

- Do not replace the Project-owned annotation snapshot with live references to external annotation files.
- Do not require one annotation file per image; annotation binds to the sample/bucket and the bucket may contain multiple compatible image paths.
- Do not create separate Qt-selected and converter-selected persistence formats. Selection origin changes how samples are obtained, not how they are committed.
- Do not silently drop converter-requested samples, repeat converter sampling in Qt, or infer a different bucket after the converter has returned the selected image paths.
- Do not infer `BackgroundOnly` from a missing annotation resource unless the source semantics explicitly define that absence as a valid negative sample.
- Do not add `unknown` as a semantic-segmentation foreground class. Segmentation uses background plus concrete foreground classes; missing class names receive `classA`, `classB`, and similar editable placeholders.
- Do not use source numeric labels as Project semantic identities merely because their values are available. Source values, Project class identity and user-editable class names have separate roles.
- Do not use `-1` as a segmentation Region/RLE label. In the current rules, `-1` is reserved for classification unknown/unassigned target semantics.
- Do not rewrite Region/RLE geometry when only a class name changes. A confirmed semantic edit updates Project metadata.
- Do not introduce nested JSON when a flat authoritative field is sufficient, and do not duplicate one semantic value in several JSON levels.
- Do not move general LabelMe/COCO/YOLO/MVTec ecosystem conversion into C++ merely to centralize code. Rich external conversion remains a converter responsibility unless a specific direct C++ codec is explicitly approved.
- Do not invent OD/OCR target schemas before the corresponding typed DeepLearning structures are defined.
