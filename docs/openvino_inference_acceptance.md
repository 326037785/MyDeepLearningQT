# OpenVINO CPU FP32 推理验收标准

## 1. 分支与输入

每个模型规格各训练一次。用同一份 LibTorch checkpoint 作为 CPU FP32 基准和 OpenVINO 转换来源。转换产物为同目录、同文件名主体的 XML/BIN。保存后用 OpenVINO CPU FP32 重新加载，按 `weight_id` 选择派生分支走 Core 的 `inference::infer()` 入口。移走源 checkpoint 后，派生分支仍能从自己的 IR 和归一化快照完成推理。

两条分支使用相同的留出图片、通道顺序、尺寸规则、归一化、类别顺序和后处理参数。比较网络输出时用相同的预处理输入。验收逐模型、逐图判定，报告每项最差值及失败样本名。阈值取等号时通过。

所有网络输出都必须形状一致、数值有限，并逐元素满足以下公式。`reference` 为 LibTorch 的对应元素。

```text
abs(openvino - reference) <= 1e-4 + 1e-3 * abs(reference)
```

## 2. 第一阶段 SG 分割

覆盖 `sg-0-s`（ResNet18）和 `sg-1-m`（ViT）。两个网络输出 `logits` 和 `features`。留出样本覆盖非方形原图、空前景结果和同类多连通域。

| 检查项 | 判定标准 |
| --- | --- |
| 像素置信度 | 对每张图在 `logits` 尺寸上沿类别轴做 softmax，比较所有像素、所有类别的绝对误差。P99 ≤ `1e-4`，最大值 ≤ `1e-3`。P99 取升序误差中第 `ceil(0.99 × N)` 项。 |
| 连通域拓扑 | 使用 `SgResult` 在原图坐标下得到的最终过滤 mask，按前景类别比较 8 连通域。每类数量一致，并按像素重叠一一配对。新增、丢失、拆分或合并均失败。空前景图要求两个分支都为空。 |
| 连通域形状 | 基准面积 ≥ 100 像素时，每对区域的掩膜 IoU ≥ `0.99`，面积差 ≤ 基准面积的 `1%`。基准面积 < 100 像素时，每对区域的像素对称差 ≤ `1`。两种情况下外接框每条边的偏差均 ≤ `1` 像素。 |
| 连通域置信度 | 对每对区域比较 `SgResult` 计算的平均类别置信度。绝对差 ≤ `1e-3`。 |

### 复现与实测

使用启用了 OpenVINO 的 Core preset，在 Core 仓库根目录执行。

```powershell
cmake --build --preset gpu-vs2022-release --target openvino_sg_acceptance_test
ctest --preset gpu-vs2022-release -R '^openvino_sg_acceptance$' -V
```

验收程序使用仓库的合成数据生成器，固定 Torch 初始化种子为 `173`，用 32 张图训练、8 张图选取 best checkpoint，每个模型训练 8 个 epoch。另留出 8 张合成图和 1 张纯背景图用于分支验收。网络输入为 `H=192, W=256`，原图为 `H=197, W=253`。ViT 使用 Core 中注册的固定模型结构，并覆盖非方形位置编码插值。后处理置信度阈值为 `0.5`，关闭形态学处理。

| 模型 | 样本 / 配对区域 | 空前景 / 同类多区域样本 | `logits` / `features` 最大绝对误差 | 像素误差 P99 / 最大值 | 区域最低 IoU | 最大面积差 / 最大框边差 | 区域置信度最大绝对误差 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `sg-0-s` | 9 / 16 | 1 / 6 | `5.24521e-6` / `2.86102e-5` | `8.34465e-7` / `1.2517e-6` | `1.0` | `0` / `0` | `1.97742e-7` |
| `sg-1-m` | 9 / 19 | 1 / 8 | `4.05312e-6` / `1.66893e-6` | `2.38419e-7` / `7.15256e-7` | `1.0` | `0` / `0` | `6.04676e-8` |

两种模型均通过。实测使用 OpenVINO 2025.4.0，路径覆盖含中文文件名的 XML/BIN。验收程序和临时数据生命周期见 [Core SG 验收程序](../../MyLibtorchLearning/test/test_openvino_sg_acceptance.cpp)。

## 3. 第二阶段 CL 分类

覆盖 `cls-1-s`（ResNet18）和 `cls-2-n`（MobileNetV1）。分别实现 `cls_small_01_network.h` 和 `cls_nano_02_network.h`。按第 1 节完成源 checkpoint、原生 IR 导出、重新加载和分支推理。校验 `logits`、`features`、`classifier_weights` 三个网络输出。

| 检查项 | 判定标准 |
| --- | --- |
| 分类类别 | 逐图比较 softmax top-1 类别，并在相同 `cls_confidence_threshold` 下核对 `ClsResult.predicted_class`。触发阈值拒绝时，两条分支都返回 `-1`。 |
| 逐类别置信度 | 按类别顺序逐项比较 `ClsResult.probabilities`。每张图的最大绝对误差 ≤ `1e-3` 即通过，≤ `1e-4` 记为达到更严格精度。 |
| 验收记录 | 每个模型记录样本数、最差置信度绝对误差及对应图片和类别、top-1 或阈值判定不一致的样本、两个精度门槛的通过情况。 |

### 复现与实测

使用启用了 OpenVINO 的 Core preset，在 Core 仓库根目录执行。

```powershell
cmake --build --preset gpu-vs2022-release --target openvino_cls_acceptance_test
ctest --preset gpu-vs2022-release -R '^openvino_cls_acceptance$' -V
```

验收程序复用仓库的合成图生成器，每图生成一个前景区域并用区域标签作为图片类别。两个模型各以种子 `173` 初始化，在 CPU 上用 36 张图训练 6 个 epoch，用 9 张验证图选取 best checkpoint，另留出 9 张图验收，覆盖 3 个类别。网络输入为 `H=64, W=128`，原图为 `H=79, W=119`。阈值 `0.0` 时逐图对比 top-1 和各类别概率，阈值 `1.0` 时核对拒绝行为。

| 模型 | Top-1 一致 | 阈值拒绝一致 | 置信度最大绝对误差 | 最差样本 / 类别 | `logits` / `features` / `classifier_weights` 最大绝对误差 |
| --- | ---: | ---: | ---: | --- | --- |
| `cls-1-s` | 9/9 | 9/9 | `2.68221e-7` | `image_0049.png` / 1 | `2.86102e-6` / `3.24249e-5` / `0` |
| `cls-2-n` | 9/9 | 9/9 | `2.98023e-8` | `image_0046.png` / 3 | `8.9407e-8` / `3.12924e-7` / `0` |

两个模型均通过三个网络输出的数值检查和 `1e-4` 的置信度精度门槛。实测使用 OpenVINO 2025.4.0，覆盖含中文文件名的 XML/BIN、源 checkpoint 移走后的独立推理，以及原图尺寸的分类热力图。见 [Core CL 验收程序](../../MyLibtorchLearning/test/test_openvino_cls_acceptance.cpp)。

实现边界见 [推理模型架构与权重转换规范](inference_model_weight_handle.md)。

<!-- 迁移说明：本文档已按 Core/Qt 所有权边界清理不应由 Qt 对外声明的 Core 内部设计规格；当前 Qt 源码尚未同步调整。代码迁移应以本文档更新后的契约为准，不要因暂时存在的实现差异恢复旧规格。对应代码完成迁移后，删除本注释。 -->
