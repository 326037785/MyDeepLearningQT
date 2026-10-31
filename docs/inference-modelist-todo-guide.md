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

**Paused**：Core 在完整 epoch 结束并保存 checkpoint 后返回。Qt 登记 last.pt，用户可以 Resume 和 Infer。

**Completed**：Core 完成训练。Qt 登记 best.pt，用户可以 Infer。

**Stopped**：Core 在 batch boundary 终止本次训练，以完整 epoch 为 checkpoint 边界。完成过完整 epoch 时，Core 整理 best.pt 和 last.pt，Qt 登记有效 best.pt 供 Infer 使用；首个 epoch 完成前停止时，模型保持 Stopped，权重集合保持为空。

**Resume**：用户选择 Paused 模型及其有效 LibTorch training checkpoint。Core 恢复训练状态，从下一个 epoch 继续。Stopped 表示本次训练终止，后续操作使用已有权重执行 Infer。

TaskNode 的任务类型决定清单范围。各行可以有不同的 model_index、model_scale、输入尺寸和类别 schema。具体操作检查所选模型的兼容性。

浏览器中的 Capacity 表示 model_scale。Dataset 的 bucket_capacity 表示单个 sample 的输入图片数。

## 3. 推理结果与 UI 选择

Inference 属于 TaskNode。当前 Node 内的 `model_id + weight_id` 定位一份 latest result。同格式的多个 weight branch 分别保留结果。

ModelList 是结果选择入口：

- Model row 对应 model_id。
- Weight branch 对应 weight_id。
- Annotation、Inference、Results 共用 TaskWorkspaceService 的选择状态。
- 切换 Model 或 Weight 后，立即刷新该坐标对应的结果。
- 所选 branch 无结果时，显示空结果并清除 inference overlay。

格式列用于分组。结果定位不依赖 weight_format、最近运行顺序或目录扫描。查看已保存结果不要求推理 executor 可用；执行仍受设备和 runtime 约束。

ResultReference 保存 source_sample_id 和 Core typed Result。一个 bucket 内的图片记录由 Core image_path 区分。

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

**删除 ModelEntry**：应用删除模型行、模型资产、该 model_id 的全部 inference records 和 inference/<model_id>/，并清除对应 training_model_id。

**删除 ModelWeight**：应用删除该 weight branch、对应资产、model_id + weight_id 的 inference records 和 inference/<model_id>/<weight_id>/。其他 branch 及其结果继续保留。应用清空直接子 branch 的 parent_weight_id，保留模型原训练状态；删完全部权重后，模型行保留空权重集合。

**清空 ModelList**：应用删除当前 TaskNode 的全部模型行、模型资产、inference records 和 inference/，并清除 training_model_id。

**缺失资产清理**：缺失单个 weight 时，应用执行单个 ModelWeight 删除；模型的全部 weight 文件缺失时，应用删除整个 ModelEntry；model_list.json 缺失时，应用保持空 ModelList，并清理该 TaskNode 下失去模型引用的全部 inference。

**删除收尾**：ProjectManager 协调当前 TaskNode 的删除与保存，Qt 更新当前 Model/Weight selection。ModelList 和 Workflow 引用共同记录删除结果，其他 TaskNode 保留各自资产。

相关定义见 [project-overview.md](project-overview.md)。Annotation 的任务语义见 [annotation_spec.md](annotation_spec.md)。
