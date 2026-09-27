# AiMetaData in DeepLearningQT

`DeepLearningQT` consumes `jocelin::meta::SampleSet` and `jocelin::meta::AiMetaData` from `DeepLearning` directly. The authoritative definition is `DeepLearning/docs/ai_metadata.md`; this file only defines the Qt-side boundary.

## Direct consumption

One `AiMetaData` is one sample/bucket. Qt reads and edits the same backend object instead of building a parallel sample structure.

The following data stays in `AiMetaData`:

- all input image paths in the bucket;
- region annotation;
- ROI and ROI four-point geometry;
- training exclusion masks;
- class label;
- sample selection;
- train/validation split and split mode;
- annotation state;
- dataset and sample identity.

File-backed annotation formats and canvas buffers are adapters around these fields. They are not another canonical sample model.

Project model identity and model weight branches are also outside `AiMetaData`. Qt reads those from the backend `jocelin::model::ModelList`; sample metadata must not duplicate model ids, checkpoint catalogs or LibTorch/ONNX/TensorRT branch state.

## UI-only structures

Qt may define structures that exist only for presentation or interaction, for example:

- canvas tool state;
- temporary polygon or drag handles;
- viewport transform;
- thumbnail/display rows;
- transient widget selection.

These structures may reference `sample_id` or backend geometry, but they must not duplicate the sample's business data as an independent source of truth.

## Annotation conversion

When Qt imports or edits annotation data, it converts the result into the corresponding `DeepLearning` representation before the sample is consumed by training or inference.

A segmentation mask file or `QImage` becomes `std::vector<Region>` in `AiMetaData::regions`. A training exclusion drawing becomes `AiMetaData::mask`. Neither path is appended to `AiMetaData::image_paths`, because `image_paths` contains bucket input images only.

## New task modules

If a new task such as object detection or OCR requires a structure that `AiMetaData` cannot express, do not define the business structure in `DeepLearningQT`.

First extend `DeepLearning`:

1. add the required geometry/data structure under the backend `common/geometry` or related core module;
2. add the typed metadata field required by the task;
3. implement the backend producer and consumer.

After that, Qt may add the corresponding editor and visualization for the new structure. Qt owns the visual management of backend structures, not their domain definition.
