# DeepLearning Studio

DeepLearning Studio is the Qt 5 desktop frontend for `DeepLearning::Core`. The
application organizes one concrete vision workflow:

1. create or open a project;
2. connect one `Dataset` node to one `Task` node;
3. open the Task workspace;
4. select data, annotate, split, train, infer, and inspect results.

The frontend links the packaged backend library. It does not compile backend
sources and does not invoke the CLI.

## Domain boundary

`Dataset` owns only the raw asset inventory and stable sample structure:

- source directory;
- imported item path, decode/file validity, error, and stable sample id;
- `bucket_capacity`.

`Task` owns every task-specific decision and artifact:

- task/index/scale model specification;
- training and inference settings;
- `TaskDatasetBinding` sample selection, class schema, annotations, split,
  quality findings, and preprocessing;
- result references and active/paused/completed run state.

Completed and imported models, their lineage, and LibTorch/ONNX/OpenVINO/TensorRT
weight branches are owned by Core `ModelList`. A Task only stores the id of the
ModelList row it controls; pause/resume checkpoint ownership stays on that row
instead of becoming a second inventory or user-facing raw checkpoint catalog.

There is one current project structure. Loading is strict: unsupported or
malformed structures are rejected instead of guessed, converted, or routed
through alternate representations.

## Build

**Build rules:** The only supported Core CMake build is `C:\CppProj\DeepLearning\build`, published to `C:\CppProj\DeepLearning\build\package`; the only Studio CMake build is this repository's `build`. Do not create alternative `out`, `build-*`, nested build trees, duplicate solutions, or another Core/Studio project without the project owner's explicit instruction. Rebuild and install Core to its existing package before rebuilding Studio; do not point Studio at a Core build-tree library or stale package.

The supported local toolchain is Visual Studio 2022, Qt 5.15, CMake, and the
Release configuration.

```powershell
cmake -S C:\CppProj\DeepLearningQT -B C:\CppProj\DeepLearningQT\build `
  -G "Visual Studio 17 2022" -A x64 `
  -DQt5_DIR=C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5 `
  -DVTK_DIR=C:\CLib\vtkQt\lib\cmake\vtk-9.2 `
  -DTBB_DIR=C:\CLib\TBB\lib\cmake\tbb `
  -Dlibuv_DIR=C:\CLib\libuv\lib\cmake\libuv `
  "-DCMAKE_PREFIX_PATH=C:\CppProj\DeepLearning\build\package;C:\CLib\libtorchCpu\libtorch" `
  -DOpenCV_DIR=C:\CLib\opencv\install\x64\vc17\lib

cmake --build C:\CppProj\DeepLearningQT\build `
  --config Release --target DeepLearningStudio
& C:\CppProj\DeepLearningQT\scripts\verify_release_clean.ps1
```

The product executable and deployed runtime files are written to:

```text
C:\CppProj\DeepLearningQT\build\Release
```

Static libraries are isolated under `build\.build-artifacts`. Test executables
never share the product directory.

## Tests

Tests are part of the normal development build. Test executables are isolated
from the product output:

```powershell
cmake --build C:\CppProj\DeepLearningQT\build --config Release
ctest --test-dir C:\CppProj\DeepLearningQT\build `
  -C Release --output-on-failure
```

Test executables are written to:

```text
C:\CppProj\DeepLearningQT\build\.test-artifacts\Release
```

Obsolete or duplicate tests should be removed from the source tree and CMake;
they are not hidden by disabling all test targets.

## Runtime dependencies

- The Qt-enabled VTK install is required by the selected OpenCV build. CMake
  derives `VTK_RUNTIME_DIR` from `VTK_DIR` and keeps that runtime dependency
  available to the product and debugger environment.
- oneTBB is required by the selected OpenCV build, and libuv is required by the
  selected LibTorch build. They remain explicit dependencies even though Studio
  code does not call them directly.
- The validated libuv package is the official `v1.52.1` Release install.
- The selected LibTorch package requires `libiomp5md.dll`; CMake validates and
  deploys it explicitly.
- SVG icons are compiled into the executable through Qt resources and rendered
  with QtSvg, so the UI does not depend on an external icon theme.

## Documentation

- `docs/project-overview.md`: elementary Qt/backend ownership and module boundaries.
- `docs/ai_metadata.md`: Qt consumption rules for `DeepLearning` sample metadata.
- `docs/adding-modules.md`: dependency-ordered guide for adding model variants, task modules and persisted fields, with examples.
