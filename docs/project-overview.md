# DeepLearningQT

`DeepLearningQT` is the Qt frontend for `DeepLearning::Core`. It manages project interaction, workflow presentation, annotation editing, training/inference controls and result visualization. Backend task data and task mathematics belong to `DeepLearning`.

## Current workflow

```text
Project
  -> Workflow
       -> Dataset -> Task
```

Dataset import establishes the raw assets and sample buckets. A Task operates on the backend sample container produced from those assets.

## Data boundary

Per-sample task data uses `jocelin::meta::SampleSet` and `jocelin::meta::AiMetaData` from `DeepLearning` directly. Qt must not maintain another business structure that duplicates:

- bucket image paths;
- task annotation;
- ROI;
- training exclusion masks;
- sample class labels;
- sample selection;
- train/validation split;
- annotation state.

Qt may keep presentation-only state such as widget selection, viewport transforms, temporary drawing handles and display rows. Presentation state must not become a second source of truth for backend sample data.

The authoritative metadata rules are defined in `DeepLearning/docs/ai_metadata.md`. See [ai_metadata.md](ai_metadata.md) for the Qt consumption rules.

## ModelList boundary

Project models use `jocelin::model::ModelList` from `DeepLearning`. Qt persists the same container inside the Project and presents it through one shared model browser; Training and Inference must not maintain separate model inventories or raw checkpoint catalogs.

The Task workspace presents the ModelList **within one task type** (for example SG). A shared task type does not require the same Dataset, class schema, `model_index`, or **Capacity** across rows. The browser's Capacity means Core `model_scale`; it is **not** Dataset `bucket_capacity` (input images per sample). Model-specific compatibility is checked only for the user-selected operation, rather than restricting all rows to identical model specs.

**One row identifies one independent training run's logical model, or one externally imported model that was trained before import.** An imported trained model receives its own row. New, Inherit and Transfer are new training runs and create new logical model rows; pausing and resuming an unfinished run operates on its existing row, not one new row per Resume or per Epoch checkpoint. Training status distinguishes Running, Paused, Completed, Failed and Imported when relevant.

Columns display **the weight formats and derived branches of that same row** (not the four training modes):

```text
Model | LibTorch | ONNX | OpenVINO | TensorRT
```

The UI exposes all four weight families, while a family is selectable for inference only when its runtime is installed and a compatible weight exists. A cell can contain multiple branches/precisions such as `FP32 / FP16`. Converted/optimized weights stay in the **same row**, with their source relationship recorded by the weight branch (e.g. `parent_weight_id`), rather than becoming independent trained models.

**LibTorch serves both training and inference.** ONNX, OpenVINO and TensorRT are inference-only. Device and weight type are independent: LibTorch/ONNX support CPU and GPU, OpenVINO is CPU-only, and TensorRT is GPU-only. Inference selection is ordered: choose CPU/GPU first, choose a concrete GPU when applicable, then show only compatible weights whose runtimes are installed. Training source selection uses a compatible LibTorch weight belonging to a selected ModelList row. The ordinary inference path does not ask users to browse arbitrary checkpoints.

Core fallback is defensive and does not drive the UI choices. If a UI defect or stale project produces an invalid route, Core tries the requested weight on CPU, then LibTorch on the requested device, then LibTorch on CPU. Every changed route returns a structured fallback notice that Studio writes to the visible log.

The Qt operation defines the Core `WeightMode`: default pretrained, inheritance, transfer or resume. ModelList row identity, weight-format column and `WeightMode` are three separate concepts. An imported Model Weights file does **not** need Optimizer/Scheduler/Epoch to be a legitimate imported row and inheritance/transfer source; only Resume requires a complete training Checkpoint. One full Checkpoint can also contribute Model parameters for inheritance without forcing that operation to Resume.

Import, Export, Delete and Clear are ModelList operations. Import copies an externally trained model into Project-owned storage and creates its row; Export copies a Project model outward without changing ownership. Delete removes one selected model asset and its row; Clear removes the collection. Project Delete is a separate project-lifecycle operation.

A Task keeps only the `training_model_id` of the ModelList row it controls. Pause/resume state and the LibTorch checkpoint path live on that model row; they are not a second model inventory or a general checkpoint browser. Stop/failed runs do not become completed model results; handling their in-progress row remains a lifecycle concern.

Qt does not implement forward or backward propagation. It sends the selected ModelList source, task settings and Stop/Pause requests to Core, then renders Core's `Completed`, `Paused` or `Stopped` result. Core alone owns batch updates, Scheduler/Epoch commit order and Checkpoint validity.

## Results boundary

Results inherit the ModelList matrix coordinates. One persisted result belongs to one logical model row and one weight-format column, identified by `model_id + WeightFormat`. The Project owns these assets under `results/<model_id>/<libtorch|onnx|tensorrt>/`. Running inference again for the same model and weight format replaces that directory. Different precision modes of the same format do not create result history; the user explicitly selects the precision mode and the current result is overwritten.

The Results workspace reuses the ModelList matrix as its selector so users can switch between LibTorch, ONNX, OpenVINO and TensorRT results of the same trained/imported model. The selected `(model, format)` result then exposes its sample list and detail view instead of flattening every model/result into one table.

Qt does not calculate task metrics. It consumes values produced by `DeepLearning` task Result modules. Classification Precision/Recall/F1 is aggregate over the images participating in that inference. Segmentation Precision/Recall/F1 is per image and is shown together with task-specific values such as mean confidence and predicted area. Prediction artifacts and a self-contained `result.json` are persisted inside the Project result directory; Export only copies the selected result outward.

## Annotation and visualization

Qt is responsible for editing and visualizing the backend structures. External annotation formats may use temporary parser or canvas representations, but committed project/task data is converted into the corresponding `DeepLearning` structures.

For example, a segmentation mask displayed as `QImage` is a UI/file representation; the backend spatial annotation is stored as `std::vector<Region>` in `AiMetaData`.

## Adding a task

A new task starts in `DeepLearning`, not in Qt.

1. Define any required task geometry or data structure in `DeepLearning`, normally under `common/geometry` and `common/meta`.
2. Add the task's backend Dataset, model, trainer, inference, loss, metric and result support as required.
3. After the backend structure exists, add the corresponding Qt editor, viewer and presentation logic.

Qt must not introduce a backend-style `BBox`, OCR target, polygon target, Region replacement or other task data structure merely to make the UI compile. If the task needs that data, the structure belongs to `DeepLearning`; Qt only manages its visual interaction and presentation.

The ordered extension points, persistence rules and code examples are in [adding-modules.md](adding-modules.md).
