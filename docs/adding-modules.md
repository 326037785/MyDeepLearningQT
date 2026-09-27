# Adding Modules to DeepLearning Studio

This guide is the step-by-step companion to [project-overview.md](project-overview.md)
and [ai_metadata.md](ai_metadata.md). It explains the Qt extension points in
dependency order: which file to touch first, what becomes available
automatically, and what still needs an explicit branch.

The backend counterpart is `DeepLearning/docs/adding-modules.md`. The rule is
absolute: **a task starts in `DeepLearning`, never in Qt**. Qt consumes the
installed `DeepLearning::Core` package and adds presentation on top.

## 1. Dependency order (the only allowed direction)

```text
DeepLearning::Core package (installed, contains task/training/inference APIs)
             |
Layer 0  src/workflow.h                       (project domain data)
             |
Layer 1  src/task_module_catalog.h            (which task ids exist)
         src/model_spec_catalog.h             (which model specs are offered)
         src/model_presentation.h             (display names for scales)
             |
Layer 2  src/workflow.cpp                     (validation)
         src/workflow_document.cpp            (persistence, strict schema)
             |
Layer 3  src/task_dataset_binding.h/.cpp      (Dataset <-> Task sync)
         src/task_workspace_service.h/.cpp    (domain service, backend conversion)
             |
Layer 4  annotation editors + codecs
         src/segmentation_annotation.*, src/classification_annotation.*,
         src/annotation_region_edit_service.*, src/annotation_exchange.*
             |
Layer 5  src/task_workspace_pages.cpp         (training / inference / results UI)
             |
Layer 6  src/task_workspace.cpp               (compose pages + editors)
         src/main_window.cpp                  (bind workflows, templates, jobs)
             |
Layer 7  test/* + CMakeLists.txt              (explicit test registration)
```

Rules that follow from the diagram:

- `workflow.h` and `workflow_document.cpp` may never reference a backend task
  class. They store the current exact schema only; no migration branches.
- Only `task_workspace_service.cpp` converts Qt data into
  `jocelin::meta::AiMetaData` / `TrainingRequest` / `InferenceRequest`.
  Widgets never build backend requests themselves.
- Qt may keep presentation state (canvas tool state, viewport transform, drag
  handles, widget selection). It must not keep a second source of truth for
  sample data, splits, classes, or results.
- Strict parsing stays strict. New fields must be added to every exact-field
  list in `workflow_document.cpp`; do not add optional/legacy fallbacks.
- New `src/*.cpp` and `src/*.h` files are picked up automatically by
  `file(GLOB ... CONFIGURE_DEPENDS)` in `CMakeLists.txt`. New **tests** are
  not: they need explicit `studio_add_test(...)` registration.

## 2. Extension point map

| You want to change | File | What happens automatically |
| --- | --- | --- |
| Model variants offered by a task | `src/model_spec_catalog.h` | Node creation, workflow validation, training/inference combos |
| Available task modules | `src/task_module_catalog.h` | Module dialog, node title, properties editor, scene summary |
| Which task ids are legal | `src/workflow.cpp` `validate_for_execution()` | Node readiness, run buttons |
| Domain fields per task/sample | `src/workflow.h` + `workflow_document.cpp` | Nothing: serialization and readers must be updated by hand |
| Backend conversion | `src/task_workspace_service.cpp` | Training/inference requests, stats, results |
| Annotation editing | `src/task_workspace.cpp` `refresh_annotation()` + editor widget | Editor visibility |
| Training/inference/result presentation | `src/task_workspace_pages.cpp` | Page refresh, columns, canvases |
| File import/export formats | `src/annotation_exchange.h/.cpp` | Import/export menus |

Fast way to find every current dispatch point:

```powershell
rg -n '"sg"|"cls"|ToolType::|task\(\)\.task' C:\CppProj\DeepLearningQT\src
```

Every hit is either an existing task branch to extend or presentation-only
text to leave alone.

---

## Recipe A — Add a model variant to an existing task

Prerequisite: the backend factory and Core package already support the spec
(see `DeepLearning/docs/adding-modules.md` Recipe A).

Worked example: expose `sg-2-l` (backend index 2, scale `L`) in the UI.

### A.1 Add the option

`src/model_spec_catalog.h`:

```cpp
inline constexpr std::array<ModelSpecOption, 5> model_spec_options{{
    {"sg", 0, "s"},
    {"sg", 1, "m"},
    {"sg", 2, "l"},     // added
    {"cls", 1, "s"},
    {"cls", 2, "n"},
}};
```

### A.2 Nothing else is required

- `WorkflowGraph::add_task_node()` picks `first_model_spec(task)` and accepts
  the spec through `supports_model_spec()`.
- `WorkflowGraph::validate_for_execution()` already validates against the
  catalog, so "model spec is not registered" disappears for the new option.
- `TrainingWorkspace::populate_model_options()` and the inference page build
  their combos from the same array.

### A.3 Test

Extend `test/workflow_test.cpp` (or `test/task_workspace_service_test.cpp`):

```cpp
WorkflowGraph graph;
const int dataset = graph.add_node(NodeKind::DatasetInput);
const int task = graph.add_task_node({"sg"});
graph.connect(dataset, 0, task, 0);
auto& config = std::get<TaskNodeConfig>(graph.node(task).config);
config.model_index = 2;
config.model_scale = "l";
config.training.output_directory = "output";
// no "model spec is not registered" issue for the task node
```

Run:

```powershell
cmake --build C:\CppProj\DeepLearningQT\build --config Release
ctest --test-dir C:\CppProj\DeepLearningQT\build -C Release --output-on-failure
```

If the spec is offered in Qt but missing in the backend, training fails at
runtime with `unregistered sg model spec: sg-2-l`. That order is intentional.

---

## Recipe B — Add a field to an existing task binding

Worked example: add a per-sample `review_note` string. This is the most common
"small module" change and the one most likely to break persistence.

### B.1 Domain structure

`src/workflow.h` — add the field to the correct struct:

```cpp
struct TaskSampleBinding {
    // ... existing fields ...
    std::string review_note;   // added
};
```

Choose the narrowest owner: per-sample data belongs to `TaskSampleBinding`,
per-task settings to `TaskNodeConfig`/`TrainingSettings`, per-class data to
`AnnotationClass`.

### B.2 Persistence (writer + reader + exact lists)

`src/workflow_document.cpp` has three places that must change together:

1. `binding_to_json()` — write the field:

```cpp
{"review_note", QString::fromStdString(value.review_note)},
```

2. `binding_from_json()` — read it with the same type helper used by neighbors:

```cpp
sample.review_note =
    required_string(value, "review_note", "Task sample").toStdString();
```

3. The `require_exact_fields(...)` list for that object must include the new
   key. The parser rejects missing **and** unknown keys, so an out-of-date list
   makes every existing document fail to open.

If the field lives on `TaskNodeConfig`, the same three edits go to
`config_to_json()` / `config_from_json()` and its exact list.

### B.3 Sync and service behavior

`src/task_dataset_binding.cpp` `synchronize_task_dataset_binding()` copies an
existing `TaskSampleBinding` when a sample survives a dataset reimport
(`sample = std::move(old->second)`). New fields are preserved automatically.
When a field is derived rather than stored, compute it in that function or in
`TaskWorkspaceService::refresh_quality()` and mark `outcome.changed = true`.

If the field affects training/inference, map it in
`TaskWorkspaceService::build_training_request()` or
`build_inference_request()`. Never read the widget from there.

### B.4 UI + test

- Edit the owning view (`DatasetSelectionView`, `DatasetSplitView`, or the
  annotation editor) to display/edit the field.
- Add a round-trip assertion to `test/workflow_document_test.cpp`: serialize a
  graph containing the field, parse it, compare the value, and assert that
  removing the key from the JSON is rejected.

---

## Recipe C — Add a task module end to end

Worked example: expose object detection (`od`) after the backend task exists.
Steps are ordered; keep this order or the UI will not compile at intermediate
points.

### C.1 Verify the backend is published

```powershell
cmake --install C:\CppProj\DeepLearning\build --config Release --prefix C:\CppProj\DeepLearning\build\package
```

Confirm that `DeepLearning/build/package/include/DeepLearning/detection/...`
and the od result headers exist. If not, finish
`DeepLearning/docs/adding-modules.md` Recipe D first.

### C.2 Register the task module

`src/task_module_catalog.h`:

```cpp
inline constexpr std::array<TaskModuleDefinition, 3> task_modules{{
    {"sg", "Semantic Segmentation", "Train and run pixel-level semantic segmentation models.", true, ""},
    {"cls", "Classification", "Train and run image classification models.", true, ""},
    {"od", "Object Detection", "Train and run object detection models.", true, ""},
}};
```

This makes the module appear in `SelectTaskModuleDialog`, gives the node its
title in `workflow_scene.cpp`, and lets `properties_editor.cpp` resolve it.
Keep `available=false` plus a reason while you build the remaining steps; the
dialog then disables the entry with a visible explanation instead of crashing.

### C.3 Register model specs

`src/model_spec_catalog.h`:

```cpp
{"od", 0, "s"},
```

`WorkflowGraph::add_task_node({"od"})` now succeeds and
`validate_for_execution()` accepts the spec.

### C.4 Allow the task id in validation

`src/workflow.cpp` currently hard-codes the legal task ids:

```cpp
if (config.task != "sg" && config.task != "cls") issues.push_back({id, -1, "task type must be sg or cls"});
if (config.model_index < 0) issues.push_back({id, -1, "model index must be non-negative"});
if (!supported_scale(config.model_scale)) issues.push_back({id, -1, "model scale must be n, s, m, l, or x"});
else if ((config.task == "sg" || config.task == "cls") &&
         !supports_model_spec(config.task, config.model_index, config.model_scale))
    issues.push_back({id, -1, "model spec is not registered for the selected task"});
```

Replace the **first** and the **last `else if`** block with one catalog-driven
check so every registered module stays legal without editing this function
again. Keep the `model_index` and `supported_scale` checks in between:

```cpp
const auto* module = find_task_module(config.task);
if (!module)
    issues.push_back({id, -1, "task type is not registered"});
else if (!module->available)
    issues.push_back({id, -1, std::string(module->unavailable_reason)});
else if (!supports_model_spec(config.task, config.model_index, config.model_scale))
    issues.push_back({id, -1, "model spec is not registered for the selected task"});
```

Keep the remaining `config.training.*` and dataset-binding checks unchanged.

### C.5 Domain conversion in the workspace service

`src/task_workspace_service.cpp` contains the task branches. Extend each one:

**Split stratification** — `split_stratum()`: add an `od` branch that groups by
the task's real target classification (the class reference), not by mask pixels:

```cpp
if (task.task == "od")
    return sample.class_id.empty() ? "background-only" : "class:" + sample.class_id;
```

**Quality checks** — `refresh_quality()` currently sets
`const bool segmentation = task().task == "sg";`. Add detection-specific
validation in the same loop: boxes outside the image, empty label, zero-area
box, missing class reference. Push findings into `sample.quality_issues` so
malformed samples are excluded from training with a visible reason.

**Training request** — `build_training_request()` converts `TaskSampleBinding`
into `jocelin::meta::AiMetaData`. Add the od branch next to the sg/cls branches:

```cpp
if (tool_type == jocelin::config::ToolType::Od &&
    sample_binding.annotation_state == "unannotated")
    continue;   // dense/detection targets need real annotation
// ...
} else if (tool_type == jocelin::config::ToolType::Od) {
    sample.boxes = detection_target_boxes(sample_binding, binding().classes);
    if (sample.boxes.empty()) continue;
}
```

The adapter (`detection_target_boxes`) is Qt-owned and may read a file format,
but its output is the backend `std::vector<jocelin::geo::DetBox>`. Do not add a
Qt box type to `AiMetaData`.

**Inference request** — `build_inference_request()` fills `InferenceTarget`
per image. Add the od target mapping there (for example the ground-truth boxes
used by the metric).

**Result storage** — `record_inference_result()` visits the
`InferenceReport` variant. Add the od branch:

```cpp
} else if (const auto* od =
               std::get_if<jocelin::result::od::OdInferenceResult>(&report)) {
    for (const auto& record : od->samples) {
        ResultReference stored;
        stored.result_id = result_identifier(record);
        stored.source_sample_id = sample_id_for(record.image_path);
        stored.source_image_path = record.image_path;
        stored.confidence = record.mean_confidence;
        stored.predicted_area = record.box_count;   // reuse the numeric slot
        stored.visualization_path = record.visualization_path;
        stored.metadata_path = record.metadata_path;
        set.samples.push_back(std::move(stored));
    }
}
```

If a new numeric/semantic field is needed in the persisted result, add it to
`ResultReference` in `workflow.h` and update the three persistence places
(Recipe B.2) plus every reader.

### C.6 Annotation editor

Create a widget (for example `src/detection_annotation.h/.cpp`) that edits the
task's real object. It must:

- draw on the existing image viewport transform;
- keep only temporary handles/drag state as widget members;
- commit the result by calling `TaskWorkspaceService` mutators or by writing
  the same `TaskSampleBinding` fields the backend adapter reads.

Wire it in `TaskWorkspace::refresh_annotation()` in `src/task_workspace.cpp`,
which currently assumes a binary sg/cls split:

```cpp
const QString task_id = QString::fromStdString(task.task);
const bool segmentation = task_id == "sg";
const bool classification = task_id == "cls";
segmentation_->setVisible(segmentation);
classification_->setVisible(classification);
detection_->setVisible(task_id == "od");
if (task_id == "od") detection_->set_task_dataset(/* same inputs as sg */);
```

Add the member and its setup/clear calls wherever `segmentation_` and
`classification_` are constructed and cleared.

### C.7 Workspace pages

`src/task_workspace_pages.cpp` renders training, inference and results. Extend:

- **Inference canvas** — `InferenceWorkspace::refresh_canvas()` chooses
  `classification_canvas_` vs `segmentation_canvas_`. Add
  `detection_canvas_` to `canvas_stack_` and select it for `od`; the overlay
  toggle label becomes "Boxes" (or similar) instead of "Heatmap"/"Inference".
- **Results table** — `ResultsWorkspace::refresh_table()` writes columns
  (sample, prediction, confidence, area). For od, replace the "area" value with
  box count or mean IoU, and update the per-sample detail text in
  `refresh_sample_detail()`.
- **Aggregate metrics** — classification shows scope-level P/R/F1, sg shows
  per-image values. Decide which is true for od, and read it from the od
  `ResultReference`/`InferenceResultSet`; never recompute it in Qt.
- **Canvas classes** — reuse the shared class-color helper so od overlays use
  the same `AnnotationClass` colors as sg.

### C.8 File exchange (optional)

If od annotations must import/export a file format, extend
`src/annotation_exchange.h/.cpp` with a codec that produces the backend
geometry. Follow the existing format-id pattern (`StudioNative`,
`ILabelledSegmentation`): unknown formats fail explicitly, and the committed
data is always `TaskSampleBinding` + backend structures.

### C.9 Tests

Register new tests explicitly in `CMakeLists.txt`:

```cmake
studio_add_test(
    detection_workspace_test detection_workspace_domain
    test/detection_workspace_test.cpp)
```

Cover, in order:

1. `workflow_test.cpp` — an `od` task node validates with its registered spec
   and rejects an unregistered spec.
2. `task_workspace_service_test.cpp` — `build_training_request()` converts a
   bound detection sample into `AiMetaData::boxes`, excludes malformed samples,
   and `record_inference_result()` stores od records.
3. `workflow_document_test.cpp` — new persisted fields round-trip and unknown
   keys are rejected.
4. A widget/canvas test if the editor has interaction logic worth protecting.

### C.10 Optional: project template

`src/project_home.cpp` and `MainWindow::newProjectFromTemplate()` create
`Dataset -> Task` templates. Add an od quick-start action only after the
module is fully usable; an unavailable template should not create a broken
project.

---

## Recipe D — Add a new node kind (rare)

Task modules do **not** need a new node kind. Only add one when the workflow
needs a genuinely new processing stage (for example a "Dataset Merge" node):

1. `src/workflow.h`: add the `NodeKind` enumerator and its config struct; add
   it to the `NodeConfig` variant and `WorkflowGraph::input_ports/output_ports`.
2. `src/node_registry.h/.cpp`: add the `NodeDescriptor` (title, badge, accent,
   position).
3. `src/workflow_scene.cpp`: the scene renders ports/summary from the registry;
   add the summary text branch.
4. `src/properties_editor.cpp`: add the editor page for the new config.
5. `src/workflow.cpp`: validation rules for the new kind.
6. `src/workflow_document.cpp`: serialize/parse the config and update exact
   field lists.
7. `src/main_window.cpp`: add it to the Node Library menu.
8. Tests: workflow domain + document round-trip + scene interaction.

---

## 3. Definition of done

- [ ] The backend module exists, is installed, and its headers are visible in
      `build/package/include/DeepLearning/...`.
- [ ] The task/spec appears in `task_module_catalog.h` / `model_spec_catalog.h`.
- [ ] `workflow.cpp` validation accepts the new task through the catalog, not
      through another hard-coded id.
- [ ] Every new persisted field is written, read, and listed in the exact-field
      checks; strict rejection still works.
- [ ] `task_workspace_service.cpp` converts Qt data into backend structures;
      no widget builds a `TrainingRequest`/`InferenceRequest` directly.
- [ ] No Qt-only duplicate of a backend sample, class, split, model, or result
      structure was introduced.
- [ ] The editor commits into `TaskSampleBinding`/`AiMetaData`, not into a
      parallel canvas model.
- [ ] New tests are registered in `CMakeLists.txt` with unique ctest names.
- [ ] Release build + full `ctest` pass + `scripts/verify_release_clean.ps1`.

## 4. Pitfalls seen in this repository

- **Patch the JSON writer only.** The reader then fails on its exact-field
  list and every old project stops opening. Update both sides in one edit.
- **Business logic in a widget.** Widgets should call
  `TaskWorkspaceService`; they must not build backend metadata or compute
  metrics.
- **Second result cache.** Results are persisted per `model_id + WeightFormat`
  inside the Project. Do not add a parallel list in a page widget.
- **`QImage` as canonical annotation.** A segmentation mask shown as `QImage`
  is a view/file adapter; committed data is `std::vector<Region>` (or the new
  backend geometry type) in `AiMetaData`.
- **Task availability flag ignored.** A module marked `available=false` must be
  rejected by `add_task_node`, not just greyed out in the dialog.
- **CMake glob expectations.** New `src` files are automatic; new tests, new
  resources, and new runtime dependencies are not.
- **Keyword macros.** The Studio library defines `QT_NO_KEYWORDS`; use
  `Q_SIGNALS`, `Q_SLOTS`, `Q_EMIT`, not `signals`, `slots`, `emit`.

## 5. Verification

```powershell
cmake -S C:\CppProj\DeepLearningQT -B C:\CppProj\DeepLearningQT\build `
  -G "Visual Studio 17 2022" -A x64 `
  -DQt5_DIR=C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5 `
  -DVTK_DIR=C:\CLib\vtkQt\lib\cmake\vtk-9.2 `
  -DTBB_DIR=C:\CLib\TBB\lib\cmake\tbb `
  -Dlibuv_DIR=C:\CLib\libuv\lib\cmake\libuv `
  "-DCMAKE_PREFIX_PATH=C:\CppProj\DeepLearning\build\package;C:\CLib\libtorchCpu\libtorch" `
  -DOpenCV_DIR=C:\CLib\opencv\install\x64\vc17\lib

cmake --build C:\CppProj\DeepLearningQT\build --config Release
ctest --test-dir C:\CppProj\DeepLearningQT\build -C Release --output-on-failure
& C:\CppProj\DeepLearningQT\scripts\verify_release_clean.ps1
```
