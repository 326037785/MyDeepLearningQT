# DeepLearning Studio

DeepLearning Studio is the Qt 5 desktop frontend for `DeepLearning::Core`. The
application organizes one concrete vision workflow:

1. create or open a project;
2. connect a `DataNode` to one or more `TaskNode` branches;
3. open the Task workspace;
4. select data, annotate, split, train, infer, and inspect results.

The frontend imports the exported backend library from its existing build or an
explicit installation. It does not compile backend sources or invoke the CLI.

## Domain boundary

`Dataset` owns only the raw asset inventory and stable sample structure:

- source directory;
- imported item path, decode/file validity, error, and stable sample id;
- `bucket_capacity`.

`Task` owns every task-specific decision and artifact:

- task/index/scale model specification;
- training and inference settings;
- `TaskDatasetBinding` sample selection, class schema, annotations, split,
  preprocessing and derived runtime quality findings;
- result references and running/paused/stopped/completed/failed/imported model state.

**ModelList**: Each TaskNode owns one Core ModelList containing its models,
lineage and LibTorch/ONNX/OpenVINO/TensorRT weight branches. Training and Inference
use that list. The active training id and checkpoint refer to the same model row.

**Training completion**: Paused registers last.pt for Resume and Infer. Completed
registers best.pt for Infer. Stopped ends the run and registers an available best.pt
from completed epochs for Infer. Resume accepts Paused models with valid LibTorch
training checkpoints. Stopping before the first completed epoch leaves an empty
weight collection.

**Project loading**: ProjectManager rejects malformed structures and metadata.
Missing weight files produce diagnostics while the Project opens readably.
A missing model_list.json opens its TaskNode with an empty ModelList and retained
inference references. Qt asks before cleaning up missing dependencies.

## Binding and result ownership

Each TaskNode owns its Annotation, Core ModelList and Inference results. Within
that Node, model_id + weight_id locates one latest result. Annotation, Inference
and Results share the current Model/Weight selection.

Disconnect retains the Task's sample/bucket binding and assets. Reconnect compares
sample ids, ordered complete image paths and bucket capacity before adding an
edge. Confirmed DataNode deletion/replacement clears samples and Inference only
for currently connected Tasks. Task settings, classes, ModelList and weights
remain. References save successfully before Node files are removed.

The usage rules are in [ModelList and Inference guide](docs/inference-modelist-todo-guide.md).
Project and persistence definitions are in [project_spec.md](docs/project_spec.md).
AI bridge entry points are in [.ai-bridge/project-context.md](.ai-bridge/project-context.md).

## UI composition

`MainWindow` provides the application shell and mounts pages and Docks. Project,
workflow, model actions, execution, settings and activity logging live in their own modules.
Features create their own actions and views, use Qt menus/toolbars, and connect
through explicit dependencies. Business state remains in the existing domain
objects. See [module rules](docs/adding-modules.md) and the
[modularization plan](docs/ui-modularization-plan.md) for extension boundaries.

## Build

Use Visual Studio 2022, Qt 5.15 with Linguist tools, C++17 and Release | x64. Each checkout has one
approved Studio build directory. It defaults to `build`; override
`STUDIO_REQUIRED_BUILD_DIRECTORY` in a local preset to use another approved
location. The GPU checkout uses `MyDeepLearningQT` inside the repository, with
`DeepLearningStudio.sln` as its Visual Studio entrypoint.

Core must be built first. Set `DeepLearning_DIR` to the existing Core build
directory containing its exported `DeepLearningConfig.cmake`. Studio links that
library and inherits its dependencies through `DeepLearning::Core`. Studio uses
that Core build directly. An explicitly selected Core installation is supported
through its `lib/cmake/DeepLearning` directory.

Keep local Qt, Torch, OpenCV and CUDA paths in an untracked
`CMakeUserPresets.json`. Ignore the local preset and entire build directory through
`.git/info/exclude`. With the GPU machine's local presets configured:

```powershell
cmake --preset gpu-vs2022
cmake --build --preset gpu-vs2022-release
ctest --preset gpu-vs2022-release
& .\scripts\verify_release_clean.ps1 -ReleaseDirectory .\MyDeepLearningQT\Release
```

For manual configuration, use `cmake -S . -B <approved-build-directory>` and set
`STUDIO_REQUIRED_BUILD_DIRECTORY`, `Qt5_DIR`, `DeepLearning_DIR`, `Torch_DIR` and
`OpenCV_DIR` for the selected environment. Do not replace shared defaults with
the paths from a specific workstation.

The product executable and deployed runtime files live under `<build>/Release`.
Static libraries stay under `<build>/.build-artifacts`; test executables and their
runtime files stay under `<build>/.test-artifacts/Release`. Test programs and
`Qt5Test.dll` must not enter the product directory.

The root CMake file owns dependencies, common build settings, runtime deployment
and CTest. Application targets live in `src/CMakeLists.txt`; the 13 test targets
live in `test/CMakeLists.txt`. Their compiler and Qt autogen intermediates stay
under `<build>/src` and `<build>/test`, while the output paths above remain fixed.

## Tests

The existing test targets are part of the normal development build. Run them
through the local test preset, or `ctest --test-dir <build> -C Release
--output-on-failure`. Keep test executables separate from product output.

UI contract tests verify behavior, data ownership and result synchronization;
layout proportions, control hierarchy and captions are not contracts. Tests do
not write screenshots. Visual inspection is manual: run the application and
inspect the pages at the target window size. See
[annotation-tool-layering.md](docs/annotation-tool-layering.md).

## Startup and language

The File menu and home toolbar share the project actions. Existing workflow
templates are available under File > New from Template. Recent projects can be
opened from the menu or by double-clicking a row on the home page.

Choose English or Simplified Chinese from the home toolbar or Settings >
Preferences... > Language. The selection is saved and takes effect after
restarting. Chinese
currently covers the home page, menus, and project dialogs; task workspaces
remain in English. The Qt Linguist catalog is
`resources/translations/studio_zh_CN.ts`; the build compiles and embeds it.

**Preferences**: Settings > Preferences... contains a Language submenu and a
peer Auto-save annotations checkable item. The toolbar Settings button opens
the same menu. Auto-save defaults to off, takes effect immediately and remembers
the user's choice. SG commits the current annotation through its existing Save
action when the user switches samples. Project saving writes the committed data
through the existing persistence flow.

Diagnostics are written as UTF-8 session files under `logs/` beside the executable
(`MyDeepLearningQT/Release/logs` in the current build). Activity logs follow the
application directory and do not use AppData. View > Activity Log opens the
file in a separate window; logs do not occupy a workspace Dock or training-page
summary row. Training keeps its existing `runs/<run_id>/training.log`; the
training page's View training log button opens that file. Progress, curves and
business results remain in their workspaces.

## Runtime dependencies

- Core's exported targets select the matching LibTorch, OpenCV, ONNX Runtime and
  MSVC OpenMP dependencies. Do not maintain a second Core link-library list here.
- Deploy the selected LibTorch SDK's runtime DLLs, including its CUDA/cuDNN,
  `uv.dll` when present, and required `libiomp5md.dll`. These bundled DLLs take
  precedence over a different CUDA Toolkit runtime during deployment.
- Deploy the selected OpenCV runtime DLLs, including its video I/O plugin, and
  Qt's platform and image-format plugins alongside the product.
- VTK, oneTBB and libuv SDK packages are not direct Studio dependencies. Older
  distributions may still need their DLLs: `VTK_RUNTIME_DIR`, `TBB_RUNTIME_DIR`
  and `LIBUV_RUNTIME_DIR` remain optional deployment hints. Their package
  location variables can still supply those defaults. When those configuration
  files exist, CMake also loads their imported target descriptions for older
  OpenCV exports that name those dependencies.
- SVG icons are compiled into Qt resources and rendered through QtSvg.

## Documentation

- [Qt UI modularization plan](docs/ui-modularization-plan.md): implemented boundaries, validation records, and remaining extension options.
- `docs/node_identity.md`: persistent Node identity, runtime Task identity, sample identity and ownership.
- `docs/annotation_spec.md`: Node-wise Annotation persistence and task-wise target rules.
- `docs/annotation-import-and-storage.md`: import boundaries, Node assets and recovery.

- `docs/project-overview.md`: elementary Qt/backend ownership and module boundaries.
- `docs/ai_metadata.md`: Qt consumption rules for `DeepLearning` sample metadata.
- `docs/adding-modules.md`: dependency-ordered guide for adding model variants, task modules and persisted fields, with examples.
