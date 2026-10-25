# AiMetaData in DeepLearningQT

`DeepLearningQT` consumes `jocelin::meta::SampleSet` and `jocelin::meta::AiMetaData` from `DeepLearning` directly. `DeepLearning/docs/ai_metadata.md` defines the canonical metadata; this document defines the Qt-side boundary.

## Direct consumption

One `AiMetaData` is one sample/bucket. Qt reads and edits that backend object directly.

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

File-backed annotation formats and canvas buffers adapt these authoritative fields for storage and interaction.

`jocelin::model::ModelList` owns Project model identity and weight branches. Qt reads model ids, checkpoint information and LibTorch/ONNX/TensorRT branch state from that container.

## UI-only structures

Qt may define structures that exist only for presentation or interaction, for example:

- canvas tool state;
- temporary polygon or drag handles;
- viewport transform;
- thumbnail/display rows;
- transient widget selection.

These structures reference `sample_id` or backend geometry. The backend object remains the authoritative source of sample business data.

## Annotation conversion

When Qt imports or edits annotation data, it converts the result into the corresponding `DeepLearning` representation before the sample is consumed by training or inference.

A segmentation mask file or `QImage` becomes `std::vector<Region>` in `AiMetaData::regions`. A training exclusion drawing becomes `AiMetaData::mask`. Neither path is appended to `AiMetaData::image_paths`, which stores only bucket input image paths, not image data. Core transforms the image, spatial annotation and exclusion mask together during geometric augmentation, then applies boolean exclusion after augmentation.

## New task modules

New task data for object detection, OCR or another task starts in `DeepLearning`:

1. add the required geometry/data structure under the backend `common/geometry` or related core module;
2. add the typed metadata field required by the task;
3. implement the backend producer and consumer.

Qt then adds the corresponding editor and visualization. Core owns the domain definition; Qt owns visual interaction and presentation.
