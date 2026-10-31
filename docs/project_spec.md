# Project 设计规范

Project 是 Workflow 和 Node 资产的组织边界。根目录中的 project.json 保存项目身份、基本信息和 Workflow 索引。每个 Project 至少包含一个 Workflow。

Workflow 保存 Node、连接、位置和配置。TaskNode 拥有自己的 Dataset binding、Annotation、ModelList 和 Inference。ProjectManager 协调各模块的持久化。

## 1. 项目入口

| 字段 | 类型 | 定义与规则 |
| --- | --- | --- |
| format | string | 固定为 deep_learning_project。 |
| project_id | string | 项目身份。新建时生成不带花括号的 UUID。普通保存保持不变，Save As 生成新值。 |
| name | string | 去除首尾空白后的项目名称。不能为空。创建和另存为时遵循目标目录命名规则。 |
| description | string | 项目说明，允许为空。 |
| created_at | string | UTC ISO 8601 创建时间。普通保存和 Save As 沿用该值。 |
| modified_at | string | 每次保存更新的 UTC ISO 8601 时间。 |
| workflows | array | 至少一项。每项包含 id、name、file。id 在项目内唯一，file 使用项目相对路径。 |

## 2. 模块与存储

```text
project.json
workflows/<workflow_id>.json
runs/training_runs.json
nodes/<workflow_id>/<node_id>/
├── annotations/<encoded_sample_id>.regions.json
├── models/model_list.json
└── inference/<model_id>/<weight_id>/
    ├── result.json
    └── prediction artifacts
```

Workflow 文档保存 Node ID、连接、位置、配置和 next_node_id。Node ID 在 Workflow 内唯一。删除、重排和清空画布不回退分配序列。

DataNode 保存输入清单、稳定 sample_id 和 bucket_capacity。TaskDatasetBinding 保存 classes、split_settings、preprocessing 和 samples。每个 sample 包含完整有序 image_paths，以及自己的 Annotation、selection、split assignment 和 training-mask 关联。

Binding 的 bucket_capacity 与 sample 路径共同形成最后验证的 bucket snapshot。source_node_id 只由当前 edge 决定，不写入 binding 文档。没有 Dataset edge 时，其运行时值为 0。

CL 标注保存 class_id。SG 标注保存 Core Regions，并通过 region_file 引用所属 Node 的空间资产。Annotated 与空空间引用共同表达已确认的纯背景样本。

ModelListStorage 将每个 TaskNode 的 Core ModelList 写入 models/model_list.json。文件保存 models 和 next_number。模型编号在所属 Node 内分配。

Inference 属于 TaskNode。Workflow runtime 只保存 model_id、weight_id 引用。完整 typed Result 和预测文件保存在对应 branch 内。每个 branch 只保留一份 latest result。

TrainingRunStore 管理 Project 内统一的训练运行编号。runs/training_runs.json 保存 next_run_number。训练运行记录保存配置、模型关联、日志和执行结果。默认训练输出位于所属 Node 的 models/。

RecentProjectStore 将最近项目列表存入用户设置。ProjectStorage 提供文件读写、资源路径转换和目录复制。ProjectPathValidator 检查创建及另存为的名称与目标路径。

## 3. 保存与打开

ProjectManager 保存运行时根目录、活动 Workflow 索引和未保存标记。打开 Project 后选中第一个 Workflow。

项目内资源以 `project:<relative_path>` 保存。运行时按当前根目录解析这些路径。项目外资源使用绝对路径。相对资源引用的范围限定在项目根目录内。

普通保存先完成各 Workflow、Region、结果、ModelList、训练编号和项目入口的序列化及校验。随后写入 Node 资产和模块文档，最后写入项目入口。每个文件使用 QSaveFile 原子替换。准备失败不写文件；写入阶段的原子性以单个文件为范围。

**正常打开**：ProjectManager 恢复 Node 身份、连接、binding snapshot、ModelList 和 inference，创建新的运行时 task_id。模型任务类型与所属 TaskNode 一致，结果引用使用该 Node 的 model_id + weight_id。

**权重缺失**：ProjectManager 加载合法清单并保留 inference，向 Qt 报告缺失的 model、weight 和路径。全部 weight 文件缺失的模型列为完全失效模型。Project 进入可读状态。

**清单缺失**：ProjectManager 为该 TaskNode 创建空 ModelList，保留已加载的 inference 引用，并报告 model_list.json 缺失。

**加载错误**：ProjectManager 拒绝 JSON 损坏、字段非法、ID 冲突、生命周期元数据非法的清单，并报告错误。其他引用和资产继续遵循各自模块的校验规则。

Detached Task 保留 snapshot、Annotation、Inference 和 ModelList。它不依赖任何历史 DataNode。重连时先比较 sample_id 集合、完整有序 image_paths 和 bucket_capacity。一致后才连接并同步。Quality 检查在具备当前连接和运行时路径时执行。

## 4. 结果提交与清理

单个 inference branch 使用临时目录准备新文件。成功后提交 result.json、artifacts 和 Workflow 引用，再更新内存。推理或提交失败保留当前有效记录和文件。其他 branch 保持不变。

**模型删除**：ProjectManager 协调 ModelEntry 或单个 ModelWeight 的删除，保存 ModelList 和 Workflow 引用。TaskNode 的现有删除逻辑清理对应模型资产和 inference，Qt 更新 selection。

**缺失资产确认**：Project 打开成功后，Qt 汇总缺失项并说明模型资产与现有推理结果的依赖。用户确认后，应用复用模型删除逻辑：单个 weight 清理对应 branch，完全失效 model 清理整行，清单缺失清理该 TaskNode 的孤立 inference。

**取消清理**：Project 保留当前数据、文件和 inference 引用，Qt 根据文件可用性控制训练和推理入口。缺失清单仍关联孤立 inference 时，普通保存保留清单的缺失状态。

**职责**：TaskNode 持有 ModelList 和 Inference，ProjectManager 协调加载与持久化，Qt 负责提示和用户确认，Core 提供模型容器和计算能力。

DataNode 删除或替换需要用户确认。清理对象只取自当前 edge。先清除受影响 Task 的 samples 和 Inference 引用并成功保存，再删除其 annotations/、inference/。Classes、split_settings、preprocessing、其他 Task 配置、ModelList 和 weights 保留。文件删除失败报告具体路径。

替换写入新 Dataset 后执行普通同步。详细生命周期见 [inference-modelist-todo-guide.md](inference-modelist-todo-guide.md)。

## 5. Save As

Save As 使用父目录和新名称确定目标。目标必须不存在或为空，并位于源项目目录之外。

操作先把项目文件复制到目标父目录下的暂存目录。随后重定位内部资源路径，生成新 project_id，保存并重开验证，最后提交目标目录。失败时清理暂存目录。

Workflow、Node、sample、Model 和 Weight 身份保留。Detached binding 的图片路径与模型、结果路径一起重定位。训练编号文件随项目复制。外部资源沿用原引用。

身份规则见 [node_identity.md](node_identity.md)。标注存储见 [annotation-import-and-storage.md](annotation-import-and-storage.md)。Core 消费边界见 [ai_metadata.md](ai_metadata.md)。
