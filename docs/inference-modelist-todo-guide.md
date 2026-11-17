# ModelList、Inference 与 Dataset binding 使用规则

## 1. 定义与归属

TaskNode 是模型、标注和推理结果的持久工作单元。Project 通过 Workflow 组织 Node 和连接。

```text
TaskNode
├── Task 配置
├── Dataset binding
│   └── sample -> Annotation
├── ModelList
│   └── ModelEntry
│       └── ModelWeight
└── Inference
    └── model_id + weight_id -> latest result
```

一个 DataNode 可以连接多个 TaskNode。每个 TaskNode 分别拥有 Annotation、ModelList 和 Inference。同类型 TaskNode 也使用独立清单和目录。

`workflow_id + node_id` 定位 Node。`task_id` 标识运行时 Task 实例。`sample_id` 标识完整 sample bucket。模型和权重使用 Core 的 model_id、weight_id。完整身份规则见 [node_identity.md](node_identity.md)。

## 2. ModelList

TaskNodeConfig 持有 Core 的 `jocelin::model::ModelList`。Training、Inference 和 Results 通过当前 TaskNode 访问同一份清单。

**ModelEntry**：一行表示一次独立训练或外部导入形成的逻辑模型。New、Inherit、Transfer 分别创建模型行，Pause、Resume 延续同一模型行。模型行可以保留空权重集合。

**ModelWeight**：一个 branch 表示一份权重。权重转换向所属模型行添加 branch，并通过 parent_weight_id 记录来源。

**推理权重转换**使用 Inference 页 LibTorch 单元格当前选中的 `weight_id`。转换卡片按来源权重和 Core 能力启用 ONNX FP32 或 OpenVINO FP32。ONNX 资产可用于可用的 CPU 和 CUDA GPU，OpenVINO 卡片显示固定的 `CPU / FP32`。后台转换通过 Core 数值校验后，在原模型行登记 inference branch 并保存 Project。重复转换分配新的 `weight_id`。转换或分支登记失败时清理本次产物。分支登记成功但 Project 保存失败时保留分支和文件，并提示重试保存。

推理时，CPU 和 CUDA GPU 共用同一份 ONNX 权重。Qt 根据 Core 报告的 Provider 与设备能力显示可用组合，由 Core 按本次选择建立 Session。

仅勾选一个只支持 CPU 的分支时（例如 OpenVINO），Inference 的设备列表自动切到 CPU，本次 Infer 使用 CPU。取消该分支勾选后，设备列表恢复此前的设备选择。

OpenVINO 分支的 `path` 指向 XML，`associated_files` 在 Project 和 Studio 模型包中记录同目录的 BIN 文件名。旧记录缺少该字段时按 XML 文件名推导同名 BIN。Project 重开时报告具体缺失文件，推理要求文件对齐全。Save As 随模型目录复制文件对，模型包往返和分支删除也按文件对处理。包含完整模型和归一化元数据的 Studio 模型包可以导入仅含 OpenVINO 权重的模型行。

**归一化设置**随模型权重确定。LibTorch 从 checkpoint 恢复 `normalize` 和三通道 `mean/std`。转换生成的 ONNX 和 OpenVINO 分支将来源设置保存在 `ModelWeight.normalization` 中，随 `model_list.json` 和模型导出包保存。派生分支使用自己的快照独立推理。

| 环节 | 设置时机与来源 |
| --- | --- |
| 新训练、继承、迁移 | 创建训练请求时读取当前 Task 的 normalize；mean/std 使用 Core AiConfig 的默认值，Qt 不再重复定义。Core 将实际使用的值写进 checkpoint。 |
| Resume | 恢复原 checkpoint 的归一化设置，保持同一次训练的一致性。 |
| 导入 LibTorch | 有 checkpoint metadata 时保留其设置；普通 state dictionary 使用本次 Task 设置，并在生成项目内权重时写入 checkpoint。 |
| 转换 ONNX 或 OpenVINO | 读取选中的来源 checkpoint，将其归一化设置复制到新权重说明。 |
| 保存、重开、模型包导入导出 | 原样保存和恢复权重说明，不按当前 Task 设置重算。 |
| 推理 | 选定权重后、构建本次运行对象和处理图片前恢复一次；整次推理的所有图片共用这份设置。 |
| 删除来源权重 | 派生分支从自己的权重和归一化快照独立推理。 |

Qt 创建推理请求时不覆盖模型的归一化设置。公共 preprocess 根据本次运行配置处理每张图片，网络层和后处理不再次 normalize。用户修改 Task 设置只影响后续新训练或缺少 metadata 的新导入，不改变已有权重。

Project 的 ONNX 权重说明要求 `normalization`。OpenVINO 推理和 Studio 模型包导入也要求这份快照。模型包使用 `weights` 数组，各分支明确提供 `role`。直接调用 Core 的外部 ONNX 消费者可以使用 `use_checkpoint_config=false`，明确提供自己的预处理参数。

外部 `.pt/.pth` 直接导入是独立的正式入口，用于接入经典网络的预训练权重。带 checkpoint metadata 时保留来源设置；普通 state dictionary 由用户指定模型规格，Core 按注册名称和形状匹配参数与 buffer，未匹配项保留初始化值，并生成项目内完整 LibTorch 权重。该入口不依赖模型包格式。

**Pipeline 与扩展边界**按训练或导入确定设置 → checkpoint 固化 → 转换复制到派生权重说明 → Project 或模型包保存 → 重开恢复 → 推理创建运行对象前读取一次 → preprocess 每图执行一次的顺序运行。64 对齐由公共 preprocess 处理。转换中的网络对象随该次工作结束，后续推理独立加载资产。ONNX 和 OpenVINO 共用权重说明、后台任务、保存、预处理和结果处理，各自用 Core 的原生实现构图和映射权重。

Core 的 `pipeline_contract` 覆盖来源设置恢复、缺失信息报错和显式请求参数。Qt 的 `weight_conversion_contract` 用分类和分割 checkpoint 验证 ONNX 与 OpenVINO 导出、登记和重开，并在 CUDA 可用时对照 ONNX 的 CPU 和 GPU 结果。OpenVINO 用例还覆盖 BIN 占名、缺失诊断、Save As、模型包往返和成对删除。项目存储与模型包共用归一化读写规则，推理启动时从选中的权重恢复设置。

**Paused**：Core 在完整 epoch 结束并保存 checkpoint 后返回。Qt 登记 last.pt，用户可以 Resume 和 Infer。

**Completed**：Core 完成训练。Qt 登记 best.pt，用户可以 Infer。

**Stopped**：Core 在 batch boundary 终止本次训练，以完整 epoch 为 checkpoint 边界。完成过完整 epoch 时，Core 整理 best.pt 和 last.pt，Qt 登记有效 best.pt 供 Infer 使用；首个 epoch 完成前停止时，模型保持 Stopped，权重集合保持为空。

**继承与迁移来源**：Training 行内勾选支持 Completed、Imported，以及持有有效 LibTorch training weight 的 Stopped 模型。无可用训练权重的行不能作为训练来源。

**Resume**：用户选择 Paused 模型及其有效 LibTorch training checkpoint。Core 恢复训练状态，从下一个 epoch 继续。Stopped 表示本次训练终止，后续操作使用已有权重执行 Infer。

**训练历史**：模型资产目录下的 `training_history.csv` 每个已完成 epoch 保存一行，包含训练和验证 loss、主指标名称及两组指标值。CL 使用 Accuracy，SG 使用 Mean IoU。没有验证集时，验证字段为空。Resume 延续同一模型的 CSV，新的 checkpoint 和执行日志保存在模型目录下本次执行的子目录。Training 页选中模型行时读取 CSV 并显示趋势。缺少 CSV 的模型显示空曲线。

TaskNode 的任务类型决定清单范围。各行可以有不同的 model_index、model_scale、输入尺寸和类别 schema。具体操作检查所选模型的兼容性。

浏览器中的 Capacity 表示 model_scale。Dataset 的 bucket_capacity 表示单个 sample 的输入图片数。

## 3. 推理结果与 UI 选择

Inference 属于 TaskNode。当前 Node 内的 `model_id + weight_id` 定位一份 latest result。同格式的多个 weight branch 分别保留结果。

Results 和 Annotation 的 ModelList 用于查看已保存结果。Inference 的 ModelList 用于勾选执行和删除目标。

- Model row 对应 model_id。
- Weight branch 对应 weight_id。
- Annotation、Inference、Results 共用 TaskWorkspaceService 的选择状态。
- Annotation 的 Models 页签可通过格式单元格或下拉框选择当前模型的权重。Annotation 的 Results 页签随之显示当前图片的结果。
- 在 Results 或 Annotation 选择 branch，或在 Inference 勾选至仅剩一份 branch 时，刷新该 branch 的结果。查看结果时保留 Inference 的勾选状态。
- 所选 branch 无结果时，显示空结果并清除 inference overlay。

Annotation 右侧使用 Classes、Models、Results 三个 Tab 组合现有视图。类别仍由任务标注组件维护，Models 使用当前 TaskNode 的 ModelList；Results 仅检查当前样本的推理结果，完整结果管理仍在第六阶段。Models 的格式单元格显示已有权重：没有权重显示 `—`，单个权重显示精度，多个权重在单元格内选择。选择只切换已有 `weight_id`，不转换精度、不创建模型或训练记录。

格式列用于分组。结果定位不依赖 weight_format、最近运行顺序或目录扫描。查看已保存结果不要求推理 executor 可用；执行仍受设备和 runtime 约束。

Inference 中，每个格式单元格的勾选框对应下拉框当前显示的 `weight_id`，切换下拉框时保留原 branch 的勾选。行首勾选框批量勾选或取消该模型行的所有 branch，包括下拉框里未显示的 branch。部分勾选时行首显示半选。只有勾选恰好一份 branch，且有输入图片、所需资产齐全、当前设备和 runtime 支持时，Infer 才可用。缺少资产或不支持当前设备的 branch 仍可勾选删除。

ResultReference 保存 source_sample_id 和 Core typed Result。一个 bucket 内的图片记录由 Core image_path 区分。

Inference 的样本列表可按已分配的 split 和样本名称、路径或 ID 筛选。Ctrl/Shift 选择时只推理筛选结果中的选中图片；没有选中项时推理全部匹配图片。筛选不会修改 Task 的样本 Use 或 split。SG 推理保存 mask、color mask、heatmap 和结果数据，原图与结果的叠加在显示时生成，不额外保存 overlay 图片。

### Inference Filter

Filter 的 Task 类型由当前 Node 确定。配置保存在该 TaskNode 的 Core `AiConfig` 中，切换 Node 时读取对应 Node 已确认的配置。Reset 只将编辑值恢复为 Core 默认值；Apply 将编辑值写回当前 Node，用于下一次 Infer，不重新处理已有结果。

| Task | 控件 | `AiConfig` 字段 |
| --- | --- | --- |
| SG | Pixel confidence | `confidence_threshold` |
| SG | Region confidence | `min_component_confidence` |
| SG | Region area | `min_region_area`、`max_region_area` |
| SG | Aspect ratio | `aspect_ratio_range` |
| SG | Morphology、Iterations | `morphological_refine`、`morph_iterations` |
| CL | Confidence | `cls_confidence_threshold` |

单值范围由控件约束，SG 的 Area 和 Aspect ratio 最小值不得大于最大值。关闭 Morphology 时禁用 Iterations。Qt 将当前配置传入 `InferenceRequest::config`；Core 的 `SgResult::process()` 或 `ClsResult::process()` 负责参数语义和过滤计算。

Workflow 分别以 `sg_filter` 和 `cls_filter` 保存对应 Task 的字段；未保存过滤设置时使用 Core 默认值。SG 的 `max_region_area` 使用 int64 字符串保存，`aspect_ratio_range` 保存为最小值和最大值的数组。

`InferenceWorkspace` 根据当前 Task 从 [Task Filter 组合表](../src/modules/task/ui/filters/task_inference_filter.cpp)取得对应的 `InferenceFilterContent`，并绑定当前 Node 的配置。各 Task content 只负责自己的控件和字段映射；[InferenceFilterPanel](../src/modules/task/ui/filters/inference_filter_panel.h)负责折叠、Apply、Reset、错误显示和内容挂载。content 创建时由 `std::unique_ptr` 持有，加入 Qt 控件树后由 Qt 父对象管理。新增 Task Filter 时提供自己的 content 并加入组合表。

## 4. 单个 branch 的替换

再次推理只更新所选 model_id + weight_id 的 latest result。

```text
保存普通 Project 状态
    -> 新推理写入 branch 旁的临时目录
    -> 准备 result.json、artifact 路径和 Workflow 引用
    -> 提交所选 branch
    -> 更新内存并清理临时目录
```

提交前，当前有效记录和文件保持可访问。推理失败保留该结果。提交失败时恢复其目录。其他 branch 和其他 Node 的结果保持不变。临时目录只服务这次文件替换。

## 5. 存储与恢复

```text
nodes/<workflow_id>/<node_id>/
├── annotations/<encoded_sample_id>.regions.json
├── models/model_list.json
└── inference/<model_id>/<weight_id>/
    ├── result.json
    └── prediction artifacts
```

model_list.json 保存 models 和 next_number。Workflow 保存 binding，以及结果的 model_id、weight_id 引用。完整结果写入对应 branch 的 result.json。

**正常加载**：ProjectManager 读取合法 model_list.json，核对当前 TaskNode 的 Model、Weight 和 inference 引用。结果目录和输出 artifact 位于对应 branch 内。UI 按当前选择加载结果。

**权重缺失**：ProjectManager 保留 ModelEntry、ModelWeight 和 inference 引用，向 Qt 返回缺失的 model_id、weight_id 和路径。模型的全部 weight 文件缺失时，诊断将该模型标为完全失效。Project 进入可读状态，Qt 按权重文件可用性开放训练和推理操作。

**清单缺失**：ProjectManager 为该 TaskNode 创建空 ModelList，保留 inference 引用，并向 Qt 报告 model_list.json 缺失。

**清单非法**：ProjectManager 拒绝加载 JSON 损坏、字段非法、ID 冲突或生命周期元数据非法的 model_list.json，并报告具体错误。

**清理确认**：Project 打开成功后，Qt 汇总缺失项并提示“模型资产已经不存在，现有推理结果依赖这些模型”。用户确认后，Qt 按第 9 节的删除规则清理并保存；用户取消后，Project 保留当前数据和文件。

**缺失清单的保存**：用户保留孤立 inference 引用时，普通保存保留 model_list.json 的缺失状态；用户确认清理后，Project 保存空 ModelList。

Save As 保留 Workflow、Node、sample、Model 和 Weight 身份，重定位内部资源路径。外部资源沿用外部引用。保存与路径规则见 [project_spec.md](project_spec.md)。

## 6. Binding 与 detached Task

TaskDatasetBinding 的 samples 保存 sample_id 和完整有序 image_paths。binding 的 bucket_capacity 保存最后验证的 bucket 配置。这些字段共同构成 binding snapshot。

source_node_id 只表示当前 Dataset edge。没有 edge 时为 0，不作为历史来源持久化。

Fresh binding 的 samples 为空，bucket_capacity 为 0。它可以直接 connect，再普通 synchronize。验证过的空 Dataset 仍保留正数 bucket_capacity。

Detached Task 没有 Dataset edge，保留最后验证的 binding snapshot。Annotation、Inference、ModelList 和 weights 也保留。之后删除原 DataNode 不影响该 Task。

## 7. 重连

```text
候选 Dataset + detached binding snapshot
    -> 比较 sample_id 集合、每个 bucket 的完整有序 image_paths、bucket_capacity
        -> 一致：connect -> synchronize / refresh_quality
        -> 不一致：拒绝 edge，保留 binding、Annotation、Inference
```

路径按现有 Project/runtime 规则规范化。sample 的排列顺序不影响集合比较；同一 bucket 内图片的数量和顺序必须一致。

Rebind validation 只判断绑定是否相同。文件缺失、损坏输入和任务兼容性由既有 quality 流程处理。断线期间，同步和界面刷新保留已有标注与划分，不重新解释其有效性。

## 8. 删除和替换 DataNode

操作只影响当前 edge 连接的 TaskNode。

```text
列出受影响 TaskNode -> 用户确认
    -> 清除 sample binding 和 Inference 引用并成功保存
    -> 清理这些 Node 的 annotations/、inference/
    -> 删除 DataNode，或写入新 Dataset 并普通 synchronize
    -> 保存并刷新界面
```

取消后，topology、内存和文件保持不变。清理的内容和保留的内容如下：

| 内容 | 行为 |
| --- | --- |
| Sample binding、Annotation、sample selection、split assignment、training-mask 关联 | 清除 |
| Binding bucket_capacity | 置为 0，使 binding 成为 fresh |
| Inference results、状态、输入和输出路径关联 | 清除 |
| Classes、split_settings、preprocessing、其他 Task 配置 | 保留 |
| ModelList、weights | 保留 |
| Detached Task 的资产 | 保留 |

文件操作限定在当前 Workflow 内受影响 TaskNode 的 annotations/ 和 inference/。原始图片和外部 training masks 保留。文件清理失败时报告具体路径。

Annotation package import 显式提供接收 Task 的新类别和标注。它保留该 Task 的 split_settings 和 preprocessing。其他当前连接的 Task 清除旧 samples 和 Inference，保留自己的类别配置，再同步新 samples。

## 9. 模型与权重删除

**勾选删除**：Inference 的删除按钮按勾选的 `model_id + weight_id` 清理 branch 及对应结果。使用行首勾选后删除，保留该模型行。Training 使用自己的模型来源选择，删除按钮按勾选的格式单元格当前显示的 branch 操作。

**顶部全选**：Training 和 Inference 的顶部“全选”作用于当前 TaskNode 的整个 ModelList。全选后点击删除并确认，会清除所有模型行（含空行）、全部 branch 及相关推理结果。修改单个 branch 的勾选后退出整表全选。Inference 修改行首勾选后也退出整表全选。

**右键删除**：模型名称和权重单元格均可打开右键菜单。删除单元格清理当前选中的 branch；删除整行清理该 ModelEntry 的全部 branch，有权重和无权重的模型行均支持整行删除。删除操作先确认，再复用既有资产和 inference 清理流程。

**删除 ModelEntry**：应用删除模型行、模型资产、该 model_id 的全部 inference records 和 inference/<model_id>/，并清除对应 training_model_id。

**删除 ModelWeight**：应用删除该 weight branch、对应资产、model_id + weight_id 的 inference records 和 inference/<model_id>/<weight_id>/。其他 branch 及其结果继续保留。应用清空直接子 branch 的 parent_weight_id，保留模型原训练状态；删完全部权重后，模型行保留空权重集合。

**清空 ModelList**：应用删除当前 TaskNode 的全部模型行、模型资产、inference records 和 inference/，并清除 training_model_id。

**缺失资产清理**：缺失单个 weight 时，应用执行单个 ModelWeight 删除；模型的全部 weight 文件缺失时，应用删除整个 ModelEntry；model_list.json 缺失时，应用保持空 ModelList，并清理该 TaskNode 下失去模型引用的全部 inference。

**删除收尾**：ProjectManager 协调当前 TaskNode 的删除与保存，Qt 更新当前 Model/Weight selection。ModelList 和 Workflow 引用共同记录删除结果，其他 TaskNode 保留各自资产。

相关定义见 [project-overview.md](project-overview.md)。Annotation 的任务语义见 [annotation_spec.md](annotation_spec.md)。
