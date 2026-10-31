# UI Review Plan

Continue from the current uncommitted DeepLearningQT work.
Do not reset the existing semantic fixes for Mask, ProjectModel, inference, or annotation interaction.

Read the current git diff and the screenshots under `tmp_feadback/` before changing UI structure.

## 1. Sample Browser

The shared Sample Browser is already implemented for semantic-segmentation Annotation and Dataset Split.
Preserve these rules:

- Annotation and Split reuse the same thumbnail-based component.
- It remains visible at normal desktop working sizes.
- Items show thumbnail, display name, annotation state and split state.
- Standard Windows click / Ctrl+click / Shift+click selection applies.
- Right-click operates on the current selection.
- Common actions belong to the shared browser; task-specific actions are added through page callbacks.
- Segmentation class selection remains a pixel-annotation operation.

Remaining integration work may add inference from the selected samples when a compatible Project Model is selected.

## 2. Project Model Browser

Build one reusable `ProjectModelBrowser` for Annotation, Training and Inference.

The model list is navigation:

- each row shows the model name only;
- metadata is shown in a separate detail card / child panel;
- do not flatten task/network/capacity/metric/time into list rows;
- Import / Export / Delete belong to the browser asset command area.

### Repository and model selection

**ModelList ownership**: Each TaskNode owns one Core ModelList. The current
Workflow and Node locate that list; the TaskNode's task type constrains its rows.
Rows may use different capacities and model indices. Import creates a ModelEntry,
and conversion adds a ModelWeight branch to that entry.

**Training source**: The user chooses New, Inherit, Transfer or Resume. New,
Inherit and Transfer create a model row. Resume continues a Paused row with a
valid LibTorch training checkpoint and compatible model, class and training state.

**Inference selection**: The user selects a ModelEntry and weight_id in the current
TaskNode. The selected model supplies its capacity, model index and input dimensions.
Qt checks the weight file, device and runtime before execution.

**Model deletion**: The application removes the selected ModelEntry, its assets
and all inference for its model_id. Weight deletion removes one branch, its assets
and inference for its model_id + weight_id. Other branches and results remain.
Qt refreshes selection and ProjectManager saves the ModelList and Workflow references.

**Missing model assets**: ProjectManager opens the Project readably and returns
diagnostics. A missing model_list.json gives its TaskNode an empty ModelList and
retained inference references. Qt asks before cleaning up missing weights, fully
unavailable models or orphaned inference. Cancel preserves Project data and files.
The loader reports corrupt JSON and invalid metadata as errors.

## 3. Inference layout

- Long hints, paths, runtime status and Project Model metadata wrap or elide inside their allocated widgets.
- Splitter panels and flexible form fields may shrink instead of allowing text to paint under adjacent controls.
- The current-image text keeps the real source path available through its tooltip.
- The page-level `Infer` button remains the single execution command.
- Keep the page usable at the application's 760x480 minimum window size and at normal desktop sizes.

## 4. Training layout

Use the desktop hierarchy:

```text
Training Configuration | Training Monitor | Project Models
```

- Configuration remains compact.
- Advanced settings remain collapsed by default.
- Training Monitor receives the largest area.
- Current epoch, loss, task-specific primary metric, progress and runtime state belong in a compact realtime dashboard.
- Generic UI must not hard-code `mIoU`; semantic segmentation may expose Mean IoU while other tasks provide their own metric.
- Use one primary trend chart rather than permanently stacking one chart per metric.
- The chart grows its epoch data range, not its widget/page height.
- At approximately 1920x1080 the active monitor should not require an outer vertical page scroll.

Training runtime actions must reflect the real worker lifecycle:

- starting a run cannot launch a second run;
- while running, only valid runtime actions remain available;
**Paused**: Core saves a complete epoch checkpoint. Qt registers last.pt for
Resume and Infer.

**Completed**: Qt registers best.pt for Infer.

**Stopped**: Core ends the run at a batch boundary and retains completed-epoch
checkpoints. Qt registers an available best.pt for Infer and keeps the Stopped
status. An early Stop leaves an empty weight collection. Resume accepts Paused
models with valid LibTorch training checkpoints.

## 5. Results

Results is an engineering report surface.

- Replace oversized metric cards with a compact summary row.
- Keep the result table as the primary content.
- Keep `Open output folder` and `Export results` as compact actions.
- Reserve accent color for selection, current state, primary actions and warnings.

## 6. Semantic rules that must not regress

- `mask_path` is the segmentation annotation target.
- `training_mask_path` is the training exclusion mask.
- Prediction never populates the training Mask layer.
- Annotation and Prediction use transparent background for label 0.
- Heatmap remains an independent dense layer.
- Segmentation drawing may extend slightly outside the image and is clipped/intersected back to the image-sized mask when applied.
- Pick and Fill still start from a real image pixel.

## Verification

Before this UI refactor is accepted:

1. build Release;
2. run the complete existing CTest suite;
3. inspect Annotation, Split, Training, Inference and Results at approximately 1920x1080;
4. preserve the semantic rules above;
5. do not commit `tmp_feadback/`.
