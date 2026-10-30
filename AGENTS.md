# Repository Instructions

- Before proposing product or architecture changes, read `docs/project-overview.md`, `docs/ai_metadata.md` and the owning module documentation, plus Core `docs/project-overview.md` and `docs/trainer-build-and-weight-loading.md` for backend contracts. Read `README.md` for build and test rules. If present, also consult `.ai-bridge/project-context.md`, `.ai-bridge/decisions.md` and `.ai-bridge/current-handoff.md`. Current user decisions and owning design documents take precedence over handoff summaries.

- For Qt annotation tool work, read `docs/annotation-tool-layering.md` before changing code. Tool definitions and gesture maintenance are task-independent; each Task composes its tools and owns target edits, undo history and validation. The document records the implemented boundary and behavior constraints.
- In changed C++ code, every `if` branch uses braces. Spell out simple data types such as `int`, `bool`, `QPoint`, and `QImage` instead of using `auto`.

- Use the single owner-approved Studio build directory from the local preset. Import `DeepLearning::Core` from its existing build directory or an explicitly selected installation; do not compile a second Core copy. Do not install Core or create `package` output unless the owner requests it.

- At the end of every task, remove non-persistent intermediate files left by the work, such as orphaned `_CL_*` compiler files, temporary validation output, test artifacts, and one-off cleanup scripts. Preserve canonical incremental-build caches and documented deliverables; verify that every deletion target is confined to the repository's build or designated temporary directories, and report what was removed.

- Propose new functions and obtain the project owner's confirmation before implementing them.
- Training is LibTorch-only. Qt selects CPU or a concrete GPU and shows Core device-fallback warnings. Inference filters unsupported weight/device combinations before execution; do not add cross-format or TensorRT-to-CPU fallback.
- External LibTorch/PyTorch import uses Core `model::import_model_weights()`. Missing task/capacity/index is specified by the user. Match registered names and shapes; unmatched values retain initialization and zero matches is valid. Each import owns one ModelList row; acceleration adds inference weights within that row.

## Application ownership contract

DeepLearningQT gives each TaskNode its own Core ModelList, Annotation and Inference. Result selection uses the owning Workflow/Node and model_id + weight_id. Dataset connection and cleanup rules are defined in [Studio's lifecycle guide](../MyDeepLearningQT/docs/inference-modelist-todo-guide.md).

Read [.ai-bridge/project-context.md](.ai-bridge/project-context.md) for the contract map and [.ai-bridge/decisions.md](.ai-bridge/decisions.md) for stable ownership rules. These files describe the current system.
