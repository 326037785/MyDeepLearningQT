# Repository Instructions

- Before proposing product or architecture changes, read `docs/project-overview.md`, `docs/ai_metadata.md` and the owning module documentation, plus Core `docs/project-overview.md` and `docs/trainer-build-and-weight-loading.md` for backend contracts. Read `README.md` for build and test rules. If present, also consult `.ai-bridge/project-context.md`, `.ai-bridge/decisions.md` and `.ai-bridge/current-handoff.md`. Current user decisions and owning design documents take precedence over handoff summaries.

- At the end of every task, remove non-persistent intermediate files left by the work, such as orphaned `_CL_*` compiler files, temporary validation output, test artifacts, and one-off cleanup scripts. Preserve canonical incremental-build caches and documented deliverables; verify that every deletion target is confined to the repository's build or designated temporary directories, and report what was removed.

- Propose new functions and obtain the project owner's confirmation before implementing them.
- Training is LibTorch-only. Qt selects CPU or a concrete GPU and shows Core device-fallback warnings. Inference filters unsupported weight/device combinations before execution; do not add cross-format or TensorRT-to-CPU fallback.
- External LibTorch/PyTorch import uses Core `model::import_model_weights()`. Missing task/capacity/index is specified by the user. Match registered names and shapes; unmatched values retain initialization and zero matches is valid. Each import owns one ModelList row; acceleration adds inference weights within that row.
