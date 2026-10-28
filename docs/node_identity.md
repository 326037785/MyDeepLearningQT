# Node 身份与 ownership

Node 是 Workflow 中的持久工作单元。Project 通过 workflow_id + node_id 定位 Node，并以相同坐标保存其资产。

## 1. 身份

| 身份 | 定义 | 保存与恢复 |
| --- | --- | --- |
| workflow_id | Project 内一个 Workflow 的身份 | 普通保存和 Save As 保留。初始 Workflow 使用 workflow_1，新增 Workflow 使用 `workflow_<UUID>`。 |
| node_id | Workflow 内唯一的正整数 | Workflow 保存 Node id 和 next_node_id。删除、重排和清空画布不回退分配序列。 |
| task_id | 运行时 Task 实例的身份 | 创建 TaskNode 或重开 Project 时生成。普通保存沿用当前实例。 |
| sample_id | Dataset 中完整 sample bucket 的身份 | TaskNode 用它关联自己的 sample binding 和 Annotation。 |
| model_id | ModelList 中逻辑模型的身份 | 在所属 TaskNode 清单内解释。 |
| weight_id | ModelEntry 中权重 branch 的身份 | 在所属 Model 内解释。 |

连接端点使用 node_id。source_node_id、target_node_id 等字段表示 Node 在某个关系中的角色。Node 的类型由配置表达。

## 2. DataNode 与 TaskNode

DataNode 拥有输入资源清单、sample bucket 和稳定 sample_id。一个 DataNode 可以连接多个 TaskNode。

TaskNode 拥有 Task 配置、Dataset binding、Annotation、ModelList 和 Inference。同一个 sample 在不同 TaskNode 中具有独立标注。同类型 TaskNode 也具有独立模型清单。

TaskDatasetBinding 保存每个 sample 的完整有序 image_paths，以及 binding 的 bucket_capacity。它们是最后一次验证的 bucket snapshot。Annotation target、selection、split assignment 和 training-mask 关联保存在同一 TaskNode 的 samples 中。

source_node_id 只表示当前连接，由 Workflow edge 决定。断开后为 0。持久化保存 edge 和 binding snapshot，不保存历史来源 Node。

## 3. Binding 生命周期

Fresh binding 的 samples 为空，bucket_capacity 为 0。连接后执行普通同步。

Detached Task 没有 Dataset edge。它保留 binding snapshot、Annotation、Inference、ModelList 和 weights。删除任何 DataNode 都不会通过历史来源找回并清理 detached Task。

重连先比较 sample_id 集合、每个 sample 的完整有序 image_paths 和 bucket_capacity。路径按 Project/runtime 规则规范化。一致后才建立 edge 并同步；不一致时保留原数据。输入可用性和任务兼容性仍由 quality 流程判断。

删除或替换 DataNode 要求用户确认。它只清除当前 edge 连接 Task 的 samples 和 Inference。Classes、split_settings、preprocessing、其他 Task 配置、ModelList 和 weights 保留。先成功移除持久引用，再清理受影响 TaskNode 的 annotations/、inference/。

## 4. ModelList 与 Inference

TaskNodeConfig 直接持有 Core ModelList。ModelEntry 表示训练或导入形成的逻辑模型，ModelWeight 表示该模型的权重分支。

Training、Inference 和 Results 使用当前 TaskNode 的同一份清单。training_model_id 引用该 Node 当前控制的模型行。

Inference 属于 TaskNode。完整结果坐标为 workflow_id + node_id + model_id + weight_id。每个 branch 保存一份 latest result。

```text
nodes/<workflow_id>/<node_id>/models/model_list.json
nodes/<workflow_id>/<node_id>/inference/<model_id>/<weight_id>/result.json
```

Workflow 只保存结果的 model_id、weight_id 引用。sample records 使用 source_sample_id 和 Core typed Result。Annotation、Inference 和 Results 共享当前 Model/Weight 选择。

ProjectManager 协调各 Node 的保存与恢复。空 ModelList 和 detached Task 的资产同样保存。其他定义见 [project-overview.md](project-overview.md)、[annotation-import-and-storage.md](annotation-import-and-storage.md) 和 [inference-modelist-todo-guide.md](inference-modelist-todo-guide.md)。
