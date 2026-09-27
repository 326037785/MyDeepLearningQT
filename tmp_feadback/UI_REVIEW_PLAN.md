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

The repository and training-source compatibility are different layers.

Repository:

- each task type owns one Project Model repository;
- same-task models remain in that repository even when capacity/network differs;
- Import / Export / Delete operate on repository assets;
- import rejects a different task type but accepts another capacity.

Training:

- only models with a compatible task/capacity/weight family can be chosen as the training source;
- source selection is exclusive;
- no selected source model means train a new model;
- one selected source model means initialize from that model;
- same class schema means continue/resume training;
- different class schema means transfer learning;
- Resume additionally requires compatible input shape, optimizer state and epoch state;
- do not show separate `Transfer` or `Continue` buttons.

Inference:

- any model in the current task repository can be selected;
- the selected model/checkpoint supplies inference capacity, network/model index and input dimensions;
- filesystem checkpoint browsing remains a secondary external-model path;
- do not add a separate textual inference mode selector.

A Project Model that is no longer useful can be deleted from the project inventory.

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
- Pause / Resume continues the same run;
- Stop is terminal.

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
