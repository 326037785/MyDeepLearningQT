# Annotation 定义与规则

Annotation 是 TaskNode 对一个 Dataset sample 的任务标注。sample_id 标识完整 sample bucket。不同 TaskNode 对同一 sample 保存独立 Annotation。

具体任务定义 target、validity 和编辑操作。Node 提供持久化归属。运行时 task_id 标识当前执行实例。

Qt 标注工具的定义、维护与 Task 组装遵循[工具分层与迁移契约](annotation-tool-layering.md)；工具不拥有 target 或标注两态。

## 1. 身份与任务 target

| 内容 | 定义 |
| --- | --- |
| workflow_id + node_id + sample_id | Annotation 在 Project 中的定位坐标。 |
| Task 类型 | 由 TaskNode 配置决定。cls 对应 classification，sg 对应 semantic_segmentation，od 对应 object_detection。 |
| annotation_state | 标注有效性。JSON 值为 unannotated 或 annotated。 |
| Classification target | 引用稳定 class_id。 |
| Semantic Segmentation target | Core 的 `vector<Region>`，Region 保存前景 runs。 |
| Object Detection target | Core 的 `vector<BboxComp>`，每个框引用稳定 class_id，并保存原图像素坐标下的中心、宽、高。 |

## 2. 标注两态

Unannotated 表示尚无该任务认可的有效监督。Annotated 表示已有有效监督。状态由任务自己的操作和有效性判断维护。

Classification 需要已分配且有效的类别。消费时按稳定 class_id 排序生成连续 class_label，范围为 1..C。class_names[class_label - 1] 给出名称。未分配的 target 使用 -1，并保持 Unannotated。模型按自身保存的类别顺序解释预测 label。

SG 的有效前景 Regions 构成 Annotated target。用户确认纯背景后，允许 Annotated 与空 Regions 同时成立。未完成的空 target 保持 Unannotated。

SG 根据区域边界、类别和输入尺寸检查 target。不成立的 target 改为 Unannotated，并给出诊断。Detached Task 保留最后验证的 target；重新绑定后执行当前输入的 quality 检查。

OD 当前要求一个 sample 只有一张输入图片。标注框为有限数值、正宽高的水平框，不携带推理置信度。保存非空框集构成 Annotated；删除最后一个框并普通保存回到 Unannotated。用户显式选择“Mark as reviewed with no objects”后，空框集可以保持 Annotated，作为已审核的负样本。

## 3. 编辑操作

Task/service 更新 Annotation。界面从 TaskNode 刷新展示。SG 画布中的像素是编辑表示，应用编辑时转换成 Core Regions。

SG 右侧 Classes 管理 TaskNode 的类别，当前样本及其标注状态独立。列表通过 `class_id` 选择类别，`No class selected` 清空选择。重命名、排序、显隐和锁定只修改类别属性。Region 的 `run.label` 对应类别的 `mask_value`。

删除无引用类别直接生效。有样本引用时先确认影响范围，然后删除类别及其 runs。同一 Region 中的其他标签保留。受影响样本失去全部前景后回到 Unannotated。其余类别的身份和 mask 值保持原值。

选定类别时完成绘制即可赋类。未选类别时，绘制完成后通过 Board 选择已有类别或创建类别。确认后将带标签的像素编辑加入画布撤销历史，由 Save 或自动保存转换为 Task Regions。取消会丢弃本次几何，类别表、标注和撤销历史保持原状。

Classification 的 Set as empty 不可用。SG 的 Set as empty 要求用户确认。确认后清空前景 Regions，并设为 Annotated。取消保留已有 target 和状态。

SG/OD 共用 Classes 面板的列表、按钮、命名、删除确认和错误提示。SG 适配器负责像素、锁定、排序和显隐；OD 适配器负责框编辑及删除类别后的框清理。删除 OD 类别会删除各样本对该类别的框引用，保留其他类别及其框。类别数据始终由所属 TaskNode 持有。

普通空白编辑提交保持 Unannotated。未修改的、已确认纯背景 target 保留 Annotated。

**标注自动保存**：Settings → Preferences... 的设置与 Task 类型无关，默认关闭、持久记忆并立即生效。存在未提交编辑时，开启设置直接调用编辑器的保存入口；未开启则提供保存、丢弃、取消。保存失败或取消阻止切换；丢弃恢复当前样本的已提交数据。SG 与 OD 共用组件级确认策略，分别提供编辑状态、保存和丢弃操作。样本切换、离开标注页、节点切换、Workflow 切换及项目切换/关闭遵循同一策略。CL 类别赋值直接提交 Task 数据，没有额外的暂存编辑。Project 保存继续只序列化已提交数据，不在底层持久化中弹编辑确认。

**阈值参数记忆**：双阈值分割的 Keep thresholds and direction 勾选项保存低阈值、高阈值和区间内外选择。同一标注组件在后续 sample 中复用最近一次 Apply 确认的参数。取消弹窗保留已确认参数；取消勾选并 Apply 后，下次使用 64、192 和区间内选择。

**阈值帮助**：弹窗右上角的问号图标通过悬停提示说明像素赋值规则。选中像素使用当前类别，ROI 内其他像素使用背景；Cancel 保留当前 mask。

## 4. 持久化与生命周期

Workflow 的 TaskNode 配置保存 sample 和 Annotation。CL 保存 class_id。SG 通过 region_file 引用 `nodes/<workflow_id>/<node_id>/annotations/` 内的 Region 文件。空 Regions 使用空引用，由 annotation_state 区分纯背景和未完成 target。

OD 的 `bbox_comps` 直接保存于所属 Workflow 的 sample JSON，字段为 class_id、x_center、y_center、width、height。不借用 SG Region 文件。

每个 sample 同时保存完整有序 image_paths。binding 保存 bucket_capacity。它们构成最后验证的 bucket snapshot。source_node_id 只由当前 edge 决定。

Disconnect 保留 snapshot 和 Annotation。重连必须先通过 sample/bucket 比较。失败时不修改原标注。

确认删除或替换 DataNode 后，只清除当前连接 Task 的 samples 及其 Annotation、selection、split assignment 和 training-mask 关联。Classes、split_settings、preprocessing 和其他 Task 配置保留。持久引用成功移除后才删除对应 annotations/。

规则入口见 [node_identity.md](node_identity.md)。存储和导入见 [annotation-import-and-storage.md](annotation-import-and-storage.md)。完整 Dataset 生命周期见 [inference-modelist-todo-guide.md](inference-modelist-todo-guide.md)。
