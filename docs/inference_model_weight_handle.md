# 推理模型架构与权重转换规范

## 1. 范围与实施状态

本文定义 DeepLearning Core 的推理后端组织、网络拼接、权重转换，以及 DeepLearningQT 中模型分支的管理规则，作为后续实现与目录迁移的设计依据。

| 项目 | 当前状态 | 目标 |
| --- | --- | --- |
| LibTorch 训练与推理 | 已实现 | 沿用现有模型与 checkpoint 契约 |
| ONNX 转换与推理 | 已覆盖现有分类、分割模型 | 整合后端目录，保留已有行为 |
| OpenVINO、TensorRT | 已预留目录 | 完成原生网络、权重转换与推理接入 |
| 外部推理资产独立导入 | 本文规定的目标能力 | 支持仅含指定推理格式分支的模型行 |

OCR、目标检测等未来任务在明确输入输出与结果语义后，沿用本文的后端组织方式。

## 2. 术语与职责

| 术语 | 定义 |
| --- | --- |
| 模型规格 | `task + model_scale + model_index`，用于选择已注册模型实现 |
| ModelEntry | 一次训练或外部导入形成的逻辑模型，拥有模型规格、类别与权重分支 |
| ModelWeight | 一个分支的持久化说明，记录格式、精度、路径、来源和运行元数据 |
| 推理资产 | 推理所需的部署文件，例如 ONNX、OpenVINO IR 文件组、TensorRT Engine |
| 权重转换 | 从选定 LibTorch 分支恢复模型，使用其参数与 buffers 生成目标后端资产 |
| 网络拼接 | 通过后端原生操作和函数调用表达拓扑、连接、尺寸计算与输出 |
| 权重容器 | 转换期间持有实际参数数据及后端派生数据的内存对象 |

Core 负责模型构造、权重加载与转换、后端执行、任务预处理和结果计算。Qt 负责用户选择、后台任务调度、分支登记、Project 资产保存与呈现。

LibTorch 承担训练执行。Core 的 C++ 模型实现定义网络结构，checkpoint 保存参数及相应元数据。恢复时，Core 按模型规格构造网络，再加载权重。外部 `.pt`、`.pth` 文件按实际序列化格式和既有导入契约读取。

## 3. 推理格式与设备

下表定义本项目的设备支持范围。实际可用性由部署的 SDK、执行组件与设备能力决定。

| 格式 | 项目设备范围 | 资产与执行方式 |
| --- | --- | --- |
| LibTorch | CPU、可用的 CUDA GPU | 加载项目支持的 LibTorch 模型存档 |
| ONNX | CPU、可用的 CUDA GPU | ONNX Runtime 加载并执行 ONNX 图 |
| OpenVINO | 首期 CPU | 加载 OpenVINO IR 的 XML、BIN 文件组 |
| TensorRT | 支持的 NVIDIA GPU | 加载面向目标运行环境构建的 Engine |

Core 提供实际后端能力，Qt 据此启用选项。TensorRT 转换记录构建环境、目标设备相关信息和精度，执行器在加载时核对兼容性。具体 SDK 版本在实施准备阶段确定。

## 4. ModelList 与分支生命周期

### 4.1 行、格式列与分支

每行对应一个 ModelEntry。LibTorch、ONNX、OpenVINO、TensorRT 格式列组织各类权重，每个格式单元格通过现有 `weight_id` 选择具体分支；同一格式可以保存多个分支。

新训练、继承训练、迁移训练和外部导入按现有规则创建模型行。暂停与恢复沿用原训练行。停止训练保留该行，并登记已完成 epoch 产生的可用权重；首个 epoch 完成前停止时，权重集合可以为空。

模型规格选择网络实现，`model_id + weight_id` 定位所属 TaskNode 内的具体分支。完整生命周期见 [ModelList 与 Inference 使用规则](inference-modelist-todo-guide.md)。

### 4.2 转换与删除

常规转换流程：

```text
选定 LibTorch 分支
    → Core 生成目标资产并完成数值核对
    → 返回 ModelWeight
    → Qt 在原模型行登记分支并保存 Project
```

转换分支通过 `parent_weight_id` 记录来源，并保存自身运行所需的元数据。用户选择具体分支执行推理。Qt 删除分支时清理其资产和关联结果，更新持久化引用。剩余有效源分支可再次用于转换。

ModelList 的分支集合表达当前资产，现有格式、来源和身份字段表达分支关系。操作入口直接查询这些信息。

### 4.3 独立导入推理资产

用户可以导入外部推理资产，创建仅含该格式分支的模型行。例如，导入 ONNX 后，该行仅包含 ONNX 分支，来源分支字段为空。

导入流程读取资产可提供的元数据，由用户补齐任务、类别和预处理等必要信息。Core 核对输入输出契约、资产完整性与后端兼容性；Qt 在成功后登记模型行。

推理使用选定分支自身的资产与配置。训练及本文定义的原生权重转换使用有效的 LibTorch 源分支。

## 5. Core 目录与命名空间

### 5.1 后端目录

以下路径以 Core 的 `infer_network/` 为根：

```text
infer_network/
├── onnx/
│   ├── onnx_session.h/.cpp
│   ├── cls/cls_onnx_network.h/.cpp
│   └── sg/sg_onnx_network.h/.cpp
├── ov/
│   ├── common/
│   ├── cls/
│   │   ├── cls_small_01_network.h
│   │   └── cls_nano_02_network.h
│   └── sg/
│       ├── sg_small_00_network.h
│       └── sg_medium_01_network.h
├── trt/
│   ├── common/
│   ├── cls/
│   │   ├── cls_small_01_network.h
│   │   └── cls_nano_02_network.h
│   └── sg/
│       ├── sg_small_00_network.h
│       └── sg_medium_01_network.h
└── conversion/onnx/
```

`common/` 的文件按实际函数职责划分。加载、编译、序列化和执行资源管理的实现文件归入所属后端目录。

迁移时，将现有 `task/cls/onnx` 和 `task/sg/onnx` 的行为分别整合到 `onnx/cls` 和 `onnx/sg`。更新调用、包含路径、CMake 与相关文档后，删除原 `infer_network/task` 目录。

现有 `conversion/onnx` 继续承担 ONNX 原生导出和转换校验。

### 5.2 命名空间

| 模块 | 命名空间 |
| --- | --- |
| OpenVINO 公共函数与网络函数 | `jocelin::Ov` |
| TensorRT 公共函数与网络函数 | `jocelin::Trt` |
| ONNX 共享执行代码 | `jocelin::infer_network::onnx` |
| ONNX 任务代码 | 上述 ONNX 命名空间下的 `cls`、`sg` |

OpenVINO SDK 类型使用其自身的 `::ov` 命名空间。各后端函数直接使用各自 SDK 的原生类型与操作。

## 6. 公共函数与 header-only 网络

### 6.1 公共函数

OpenVINO、TensorRT 提供各自的原生算子与构图 API。项目通过后端内部的 `common/` 封装重复出现的操作组合和定制计算，例如卷积与归一化组合、残差计算和注意力计算。

公共函数接收原生图对象或张量句柄，返回后续拼接需要的输出。模型专用函数保存在对应网络头文件中；多个模型实际复用的函数归入所属后端的 `common/`。

### 6.2 网络文件命名

网络头文件采用 `<task>_<capacity>_<index>_network.h`：

- `task` 使用项目任务标识，例如 `cls`、`sg`。
- `capacity` 对应 `model_scale`，当前使用 `nano`、`small`、`medium` 表达 N、S、M。
- `index` 对应 `model_index`，使用至少两位的十进制编号。

| 任务 | model_scale | model_index | 文件名 | 当前实现 |
| --- | --- | --- | --- | --- |
| CL | S | 1 | `cls_small_01_network.h` | ResNet18 分类 |
| CL | N | 2 | `cls_nano_02_network.h` | MobileNetV1 分类 |
| SG | S | 0 | `sg_small_00_network.h` | ResNet18 分割 |
| SG | M | 1 | `sg_medium_01_network.h` | ViT 分割 |

文件名依据模型规格保持稳定。修改该规格对应的网络时，转换流程继续核对源权重名称、形状和适用的版本信息。

### 6.3 拼接规则

每个网络头文件通过过程式函数调用表达完整拓扑：输入 → 主干 → 子结构 → 任务头 → 输出。构建函数接收配置、后端构图上下文和权重容器，明确组织连接、分支合并、尺寸计算与输出命名。

嵌套函数表达子网络组合。头文件内定义的普通函数使用 `inline` 等符合 C++ 多翻译单元规则的形式。

网络头文件负责拼接。后端实现文件负责 checkpoint 接入、编译、资产读写和执行资源管理。任务预处理与结果计算沿用现有 Core 任务模块。

### 6.4 后端权重数据结构与实现指导

本节记录已确认的设计：OpenVINO 使用原生 `ov::Tensor`，TensorRT 使用自有 `jocelin::Trt::Weight`。两个后端分别定义权重容器及访问接口，LibTorch 在转换入口提供源参数。

#### 6.4.1 共同数据约定

每份权重包含实际数据、数据类型和张量形状。转移模块在进入构图接口前将数据整理为连续主机内存。两个后端的 WeightMap 均按完整注册名称索引一个参数张量或 buffer，同一层的多个条目通过名称前缀关联。

```text
layer1.0.conv1.weight       → 权重形状 [64, 64, 3, 3]
layer1.0.bn1.weight         → 权重形状 [64]
layer1.0.bn1.bias           → 权重形状 [64]
layer1.0.bn1.running_mean   → 权重形状 [64]
layer1.0.bn1.running_var    → 权重形状 [64]
```

以上名称与尺寸用于说明结构，实际值以源模型为准。网络头文件定义操作名称、连接、调用顺序和输入输出尺寸计算；权重容器提供参数名称、参数数据及其形状。后端原生张量表达构图过程中的中间输出。

后端独立持有转移后的数据，完成转移后允许释放源 LibTorch 模型。数值校验需要源模型时，由转换流程保留至校验结束。实际复制次数由所有权和转换路径决定。

#### 6.4.2 OpenVINO：原生 Tensor

参考类型定义位于 `ov/common`：

```cpp
namespace jocelin::Ov {

using Weight = ::ov::Tensor;
using WeightMap = std::unordered_map<std::string, Weight>;

}
```

| 内容 | 表达方式 |
| --- | --- |
| 实际数据 | `ov::Tensor` 自身分配并持有的连续主机存储 |
| 数据类型 | `ov::element::Type`，通过 `get_element_type()` 查询 |
| 张量形状 | `ov::Shape`，通过 `get_shape()` 查询 |
| 元素与字节数量 | 通过 `get_size()`、`get_byte_size()` 查询 |
| 参数名称 | WeightMap 的 key |
| 图中常量 | 构图时从权重创建 `ov::op::v0::Constant` |

使用流程：

```text
LibTorch 参数 / buffer
    → 核对类型与形状，准备连续数据
    → 使用分配自身存储的构造方式创建 ov::Tensor
    → 复制数据并登记到 Ov::WeightMap
    → 创建 Constant 并连接原生操作
    → 编译、序列化目标模型
```

构图函数通过 `const WeightMap&` 读取权重，调用方保持 Tensor 内容只读。Tensor 负责权重数据，Constant 负责图中常量表达。Constant 与 Tensor 的数据共享采用所选 SDK 明确支持的构造方式；构图上下文至少持有权重容器至编译、序列化完成。继续使用原生图时，其常量数据也须保持有效。

转移默认选择拥有存储的 Tensor。包装外部指针的 Tensor 构造方式适用于明确管理外部所有者的场景，后续引入时应同时说明其生命周期。

#### 6.4.3 TensorRT：自有 Weight

在 `trt/common` 定义独立的 `jocelin::Trt::Weight`。该对象持有连续数据，记录原生类型与形状，并在调用后端 API 时生成 `nvinfer1::Weights` 视图。

| 内容 | 设计 |
| --- | --- |
| 实际数据 | 自有、连续、满足支持类型对齐要求的主机存储 |
| 数据类型 | `nvinfer1::DataType` |
| 张量形状 | `nvinfer1::Dims`，保存确定的参数维度 |
| 元素数量 | 根据 shape 计算 |
| 原生描述符 | `as_weights()` 返回类型、数据指针和元素数量 |
| 内存管理 | RAII 自动释放，支持移动，显式复制执行深拷贝 |
| 参数名称 | `Trt::WeightMap` 的 key |

以下代码规定接口意图；构造参数、内部存储与分配器在实现阶段按本节契约落实：

```cpp
namespace jocelin::Trt {

class Weight {
public:
    nvinfer1::DataType type() const;
    const nvinfer1::Dims& shape() const;

    const void* data() const;
    std::size_t byte_size() const;
    int64_t count() const;

    nvinfer1::Weights as_weights() const;

    // 构造时接收类型、形状和源数据长度，
    // 校验后复制到自身拥有的存储。
};

using WeightMap = std::unordered_map<std::string, Weight>;

}
```

构造和使用规则：

1. **建立有效对象**：构造过程同时确定 dtype、shape 与实际数据。字段由 Weight 封装，访问接口提供只读信息。
2. **形状与长度**：检查维度数量、负维度、元素数量和字节数溢出，核对源数据长度。零阶标量的元素数量为 1，包含零长度维度时元素数量为 0；具体操作消费空权重时执行相应规则。
3. **类型支持**：首期支持 FP32、FP16，并显式完成源类型到目标类型的映射。后续扩展类型时同步定义存储规则；INT4 等子字节类型需要专门计算打包长度。
4. **存储与对齐**：内部存储满足所支持类型的对齐要求。字节缓冲是一种实现选择，最终分配方式应明确保障对齐和自动释放。
5. **原生视图**：`as_weights()` 按当前存储生成 `type`、`values`、`count`，空权重使用空指针和零数量。对象根据自身状态生成视图，降低存储指针副本的维护成本。
6. **生命周期**：所有原始与派生 Weight 保持有效，至少覆盖 Engine 构建完成。原生视图被使用期间，底层存储地址保持稳定，数据保持只读。
7. **复制与移动**：支持移动所有权，禁止隐式深拷贝；需要独立副本时通过显式深拷贝操作创建。构建上下文统一持有原始和派生权重。

使用流程：

```text
LibTorch 参数 / buffer
    → 核对类型与形状，准备连续数据
    → 构造拥有数据的 Trt::Weight
    → 登记到 Trt::WeightMap
    → 构图时调用 as_weights() 并提供所需 shape
    → Engine 构建完成后释放构建期权重
```

#### 6.4.4 查找、派生权重与模块边界

- 转移函数遍历源模型的 `named_parameters(true)` 和 `named_buffers(true)`，按消费需求提取参数与 buffers，并检查名称唯一性。
- 网络函数分别接收所属后端的 WeightMap，使用 `at()` 或带名称诊断的查找函数取值，并核对操作要求的形状与类型。
- `unordered_map` 负责名称查找；拼接函数确定网络连接顺序。需要稳定诊断输出时，先对 key 排序。
- 转置、布局变换和 BN 折叠的结果分别保存为 `Ov::Weight` 或 `Trt::Weight`，由本次构建上下文持有。派生名称与源参数名称保持可区分，已有源权重保持只读。
- LibTorch Tensor 服务于源模型读取和转移期间的计算。完成转移后，网络拼接接口消费所属后端的数据结构。
- 两个后端共用完整名称约定，各自拥有数据容器和转换函数。实现首先落实这些具体类型；实际出现相同提取逻辑时，再按第 7.1 节抽取小型辅助函数。

#### 6.4.5 已核查的官方定义与参考实现

以下资料在 2026-11-11 的设计讨论中已核查。后续 session 可直接按表中位置读取。GitHub 分支链接会随仓库更新，具体接口以最终选定 SDK 为准。

| 参考 | 阅读位置与已确认事实 | 本项目采用方式 |
| --- | --- | --- |
| [TensorRT 官方头文件](https://github.com/NVIDIA/TensorRT/blob/main/include/NvInferRuntime.h) | `class Weights`：包含 type、values、count；调用方维持引用数据直到 Engine 构建完成 | 自有 Weight 持有数据与 shape，按需生成原生视图 |
| [OpenVINO Tensor C++ 接口](https://docs.openvino.ai/2025/api/c_cpp_api/classov_1_1_tensor.html) | 自行分配存储和包装外部 host pointer 的构造方式；形状、类型、数量访问接口 | 首期使用拥有存储的原生 Tensor 管理权重 |
| [OpenVINO Constant C++ 接口](https://docs.openvino.ai/2025/api/c_cpp_api/classov_1_1op_1_1v0_1_1_constant.html) | 常量构造及共享内存所有者接口 | 构图阶段将权重接入 Constant，按构造方式落实所有权 |
| [tensorrtx：ResNet18](https://github.com/wang-xinyu/tensorrtx/blob/master/resnet/resnet18.cpp) | `loadWeights`、`addBatchNorm2d`、`basicBlock`、`createEngine`；使用 map 索引原生 Weights，函数拼接网络，构建结束后释放数据 | 参考具名查找与过程式拼接，自有 Weight 通过 RAII 管理内存 |
| [OpenVINO 官方构图示例](https://github.com/openvinotoolkit/openvino/blob/master/samples/cpp/model_creation_sample/main.cpp) | `read_weights`、`create_model`；读取权重并以 shape 和数据创建 Constant，连接原生操作 | 参考常量创建与构图流程，以具名 Tensor 对应多个模型 |
| [OpenVINO 示例说明](https://github.com/openvinotoolkit/openvino/blob/master/samples/cpp/model_creation_sample/README.md) | 示例目标及 C++ API 列表 | 定位原生模型创建、编译和执行入口 |

tensorrtx 示例采用较早的 TensorRT API 和手工内存管理；实现时采用选定 SDK 的接口及本节所有权规则。OpenVINO 示例采用固定 LeNet 权重布局；本项目用名称映射组织权重。上述参考支持设计方法，具体容器与接口以本节已确认方案为准。

## 7. 权重提取与后端转换

### 7.1 转换流程

```text
读取 checkpoint 元数据
    → 按任务与规格构造 LibTorch 源模型
    → 严格加载权重并切换到推理状态
    → 提取具名 parameters 与 buffers
    → 生成后端权重表示
    → 调用 header-only 网络函数
    → 编译、保存并核对目标资产
    → 返回 ModelWeight
```

`ov/common` 和 `trt/common` 分别组织权重承载与转换函数。两个实现中出现行为完全一致的提取逻辑时，再抽取小型共享辅助函数。

转换按名称与形状核对网络所需参数，缺失或不兼容时报告具体错误。BatchNorm 运行均值、运行方差等推理所需 buffers 随参数一起提取；训练计数等值按实际消费情况处理。

### 7.2 内存所有权

权重转移按第 6.4 节建立后端拥有的数据：OpenVINO 使用自身分配存储的 ov::Tensor，TensorRT 使用 jocelin::Trt::Weight。两者按完整注册名称组织参数，持有连续主机数据并提供 shape 与 dtype。源 LibTorch Tensor 的生命周期由读取、转移和源模型数值校验流程管理。

各后端持有自身生成的派生数据，例如转置结果、布局变换结果或归一化折叠参数。转换函数按原生 API 要求提供视图或常量。

TensorRT 权重视图引用的原始数据和派生数据在整个构图与 Engine 构建使用期间保持有效。OpenVINO 常量构造采用明确的数据所有权策略。转换结束后释放临时容器，推理执行器从部署资产加载运行内容。

`ModelWeight` 持久化资产说明；转换期权重容器持有实际参数内存。

## 8. 输入输出、尺寸与精度

### 8.1 任务契约

| 任务 | 网络输出 |
| --- | --- |
| CL | `logits`、`features`、`classifier_weights` |
| SG | `logits`、`features` |

任务适配代码将后端输出整理成现有结果处理需要的 CPU Tensor。外部资产导入按同一任务契约核对。

派生分支保存自身的归一化快照，推理启动时按配置恢复。任务预处理执行尺寸规范化和图像变换，结果模块继续负责原图尺寸恢复与后处理。

### 8.2 首期范围

首期使用 Batch=1，H/W 采用现有任务规则规范化后的配置尺寸。OpenVINO 首先完成 CPU FP32 链路，TensorRT 首先完成 GPU FP32 链路，再扩展 FP16。精度元数据描述实际构建策略。

实现时逐项对齐归一化 epsilon、Resize 坐标语义、卷积分组、Linear 权重方向，以及 ViT 的 QKV、LayerNorm、GELU 和位置编码处理。

动态尺寸、动态 Batch 和 INT8 在后续需求明确时扩展，同时补齐配置、资产元数据和执行约束。

### 8.3 ONNX 构图与执行

当前 LibTorch → ONNX 原生导出在 C++ 中构建并保存 ONNX 图。ONNX 推理通过 Session 加载已保存图并执行。目录迁移保留两者已有职责和数值校验行为。

## 9. 资产与执行生命周期

OpenVINO 分支同时管理 XML 和 BIN。`ModelWeight.path` 保留主入口含义；关联文件通过分支资产清单记录，以主入口目录为相对路径基准。单文件资产沿用现有主路径。

Qt 在分支登记、Project 重开、Save As、对外导出、缺失诊断和删除操作中统一处理完整资产集合。转换流程先准备并核对资产，成功后登记分支；失败处理回收本次临时文件，保留已有分支和结果。

每次推理调用拥有本次执行器、执行上下文和临时缓冲。调用内的图像共享执行器；调用结束时，各后端按自身资源所有权释放运行对象。返回的任务结果持有独立 CPU 数据。

## 10. 实施顺序与完成条件

| 阶段 | 工作内容 | 完成条件 |
| --- | --- | --- |
| 1 | 整合 ONNX 目录 | 原 task 目录删除，引用迁移完成，已有契约保留 |
| 2 | 确定一个模型的函数与权重接口 | 公共函数、网络拼接和数据所有权明确 |
| 3 | 完成 OpenVINO 首条链路 | 一个模型可转换、保存、加载并产生完整结果 |
| 4 | 完成 TensorRT 首条链路 | 同一模型在目标 GPU 完成对应流程 |
| 5 | 覆盖现有四种模型 | 每种声明支持的规格具有对应网络和转换能力 |
| 6 | 完成 Qt 接入与独立导入 | 分支选择、保存、重开、导出和删除行为完整 |

后续实施按相同预处理输入核对全部网络输出，并检查派生分支独立推理、OpenVINO 关联文件生命周期和后端资源释放。能力入口仅启用已经实现的模型、格式、精度与设备组合。

## 11. 相关定义

- [Studio 项目概览](project-overview.md)
- [ModelList 与 Inference 使用规则](inference-modelist-todo-guide.md)
- [Core 推理网络说明](../../MyLibtorchLearning/infer_network/README.md)
- [Core 模型与权重结构](../../MyLibtorchLearning/common/model/model_list.h)
- [Core 训练构建与权重加载](../../MyLibtorchLearning/docs/trainer-build-and-weight-loading.md)
