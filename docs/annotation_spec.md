# Annotation 定义与规则

Annotation 是 TaskNode 对一个 Dataset sample 的任务标注。sample_id 标识完整 sample bucket。不同 TaskNode 对同一 sample 保存独立 Annotation。

具体任务定义 target、validity 和编辑操作。Node 提供持久化归属。运行时 task_id 标识当前执行实例。

## 1. 身份与任务 target

| 内容 | 定义 |
| --- | --- |
| workflow_id + node_id + sample_id | Annotation 在 Project 中的定位坐标。 |
| Task 类型 | 由 TaskNode 配置决定。cls 对应 classification，sg 对应 semantic_segmentation。 |
| annotation_state | 标注有效性。JSON 值为 unannotated 或 annotated。 |
| Classification target | 引用稳定 class_id。 |
| Semantic Segmentation target | Core 的 `vector<Region>`，Region 保存前景 runs。 |

## 2. 标注两态

Unannotated 表示尚无该任务认可的有效监督。Annotated 表示已有有效监督。状态由任务自己的操作和有效性判断维护。

Classification 需要已分配且有效的类别。消费时按稳定 class_id 排序生成连续 class_label，范围为 1..C。class_names[class_label - 1] 给出名称。未分配的 target 使用 -1，并保持 Unannotated。模型按自身保存的类别顺序解释预测 label。

SG 的有效前景 Regions 构成 Annotated target。用户确认纯背景后，允许 Annotated 与空 Regions 同时成立。未完成的空 target 保持 Unannotated。

SG 根据区域边界、类别和输入尺寸检查 target。不成立的 target 改为 Unannotated，并给出诊断。Detached Task 保留最后验证的 target；重新绑定后执行当前输入的 quality 检查。

## 3. 编辑操作

Task/service 更新 Annotation。界面从 TaskNode 刷新展示。SG 画布中的像素是编辑表示，应用编辑时转换成 Core Regions。

Classification 的 Set as empty 不可用。SG 的 Set as empty 要求用户确认。确认后清空前景 Regions，并设为 Annotated。取消保留已有 target 和状态。

普通空白编辑提交保持 Unannotated。未修改的、已确认纯背景 target 保留 Annotated。

## 4. 持久化与生命周期

Workflow 的 TaskNode 配置保存 sample 和 Annotation。CL 保存 class_id。SG 通过 region_file 引用 `nodes/<workflow_id>/<node_id>/annotations/` 内的 Region 文件。空 Regions 使用空引用，由 annotation_state 区分纯背景和未完成 target。

每个 sample 同时保存完整有序 image_paths。binding 保存 bucket_capacity。它们构成最后验证的 bucket snapshot。source_node_id 只由当前 edge 决定。

Disconnect 保留 snapshot 和 Annotation。重连必须先通过 sample/bucket 比较。失败时不修改原标注。

确认删除或替换 DataNode 后，只清除当前连接 Task 的 samples 及其 Annotation、selection、split assignment 和 training-mask 关联。Classes、split_settings、preprocessing 和其他 Task 配置保留。持久引用成功移除后才删除对应 annotations/。

规则入口见 [node_identity.md](node_identity.md)。存储和导入见 [annotation-import-and-storage.md](annotation-import-and-storage.md)。完整 Dataset 生命周期见 [inference-modelist-todo-guide.md](inference-modelist-todo-guide.md)。
