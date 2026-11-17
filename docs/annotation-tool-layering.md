# Qt 标注工具分层与迁移契约

状态：工具手势、通用变换计算和只读结果显示已按本文重组。SG 暂态 mask 编辑、撤销和提交仍由 SG/Task 边界负责。新增任务或调整标注工具前，先核对本文件、[Annotation 语义](annotation_spec.md)和[模块边界](adding-modules.md)。

## 1. 当前代码位置

| 位置 | 当前职责 | 判断 |
| --- | --- | --- |
| [`annotation_tool_interaction.h/.cpp`](../src/modules/task/ui/common/annotation_tool_interaction.h) | 独立定义工具种类，维护笔触、图形、多边形、平移和选区变换的交互状态与矩阵计算；输出轨迹或几何结果。 | 不包含 mask、类别、TaskNode、撤销栈或持久化。`TransformSelection` 是通用手势；选择哪个带标签 Region 由 SG 决定。 |
| [`segmentation_annotation.h/.cpp`](../src/modules/task/ui/annotation/sg/segmentation_annotation.h) 的 `MaskCanvas` | 接收工具手势，编辑 SG 暂态 mask，维护每张图的撤销历史、类别可视化和 Region 选择，并提供 SG 编辑视图。 | 是 SG target 编辑边界，不是通用工具定义。直接 PNG `load/save` 供局部画布使用；产品提交仍由 TaskWorkspaceService 处理。 |
| [`segmentation_annotation.cpp`](../src/modules/task/ui/annotation/sg/segmentation_annotation.cpp) 的 `SegmentationAnnotationWidget` | 组装 SG 工具、画布和样本切换，发起 Task 标注保存。 | Classes 面板与类别确认由 TaskWorkspace 组装，类别数据由 TaskNode 持有。 |
| [`image_viewport_transform.*`](../src/modules/task/ui/common/image_viewport_transform.h) | 独立维护缩放、平移和坐标转换。 | 已处于合适层级，迁移时直接复用。 |
| [`annotation_region_edit_service.*`](../src/modules/task/ui/annotation/sg/annotation_region_edit_service.h) | 选择带标签的连通 Region，并对该 Region 做变换。 | 已抽离画布，但标签值和 Core Region 仍属于 mask/SG 适配语义；不要将其整体误认作所有任务通用工具。 |
| [`segmentation_annotation_state.*`](../src/modules/task/application/annotation/segmentation_annotation_state.h)、[`classification_annotation_state.*`](../src/modules/task/application/annotation/classification_annotation_state.h) | 分别确定有效 target、两态和 Core 数据转换。 | 留在各自 Task；工具不得决定 `annotation_state`。 |
| [`classification_annotation.*`](../src/modules/task/ui/annotation/cls/classification_annotation.h) | CL 预览、选择类别并请求赋值；目前没有像素绘制工具。 | CL 不需要为了共用框架而获得笔刷或 mask。 |
| [`image_result_canvas.h/.cpp`](../src/modules/task/ui/common/image_result_canvas.h) | 推理和结果页显示输入图像及 prediction overlay，支持原有滚轮缩放与中键平移。 | 只读页面不再构造 `MaskCanvas` 或持有 mask 编辑历史。 |

## 2. 固定的三层依赖方向

1. **工具定义**：Qt 侧独立于任务地定义工具种类及交互参数，例如 Brush 的轨迹与宽度、Polygon 的顶点、Selection 的范围、Transform 的平移/缩放/旋转。定义不包含 `sg`、`cls`、`TaskNodeConfig`、`AnnotationClass`、`sample_id` 或标注两态。
2. **工具维护**：`AnnotationToolInteraction` 维护笔触、图形和多边形手势，`ImagePanInteraction` 维护平移手势，`SelectionTransformInteraction` 维护通用变换矩阵；视口继续使用现有 `ImageViewportTransform`。工具向 Task 交付点、轨迹段、图形、选区或变换参数。连续笔刷逐段交付以保持实时反馈。工具不持有或修改正式 target，也不维护 target 的撤销历史、选择业务类别或写 Project。这里的“管理层”是职责边界，不要求增加 `ToolManager`、`ToolController` 或每个工具一个对象。
3. **Task 组装与 target 编辑**：`SegmentationAnnotationWidget` 选择 SG 可用工具，创建按钮和快捷键，把工具结果交给 SG 的 mask 编辑边界。后者使用当前 `mask_value`、背景值 `0` 和类别规则修改暂态 mask，管理每张图的撤销/重做，再由既有 `TaskWorkspaceService` 与 `segmentation_annotation_state` 提交为 Core Regions 和有效状态。`ClassificationAnnotationWidget` 按 CL 的类别赋值语义组合交互；它当前不需要笔刷。未来任务只选择适用工具，并实现自己的 target 解释，不复制工具手势实现。

任务无关不等于必须适用于所有数据形态：工具可以专门产生像素网格上的笔触或矩形框的几何操作，但不能因此持有或修改 SG/OD 的标注 target；它们也不应以 `sg` 或 `cls` 作为自身的所有权层级。

依赖只从 Task 组装层指向工具层。工具层不得反向调用 Task 服务、持久化或 Core 的任务结果逻辑。Core 仍负责 Region 几何、target 表示、推理后处理与指标；Task 的编辑边界负责暂态 target 和提交，TaskNode 仍是持久 Annotation 的唯一 owner。

Brush 产生图像坐标下的连续轨迹段和宽度。SG 将已选类的轨迹写入编辑 mask，未选类的轨迹先显示为几何预览，确认类别后写入编辑 mask。Eraser 写入背景 `0`。Save 或自动保存将编辑 mask 转换成 Regions。Pick 交付坐标，SG 读取该处 mask 值并映射到类别。

可复用的纯几何或栅格计算可以帮助生成受影响的坐标或像素集合；调用它的 Task 仍决定写入值、target、编辑事务和提交时机。不要为共享计算引入新的 target owner。

| 能力 | 工具层维护 | Task 编辑边界维护 |
| --- | --- | --- |
| Brush、Eraser、Polygon、Rectangle、Ellipse | 轨迹、顶点和图形手势；取消与预览 | 绘制值、目标 buffer、像素写入及提交 |
| Fill、Pick、ThresholdRoi | 起点/选区、阈值预览与用户选择 | 四邻域目标区域、读取类别值、阈值结果到 target 值的映射 |
| TransformRegion | 拖拽手柄与几何变换参数 | 按标签选连通域、修改 mask 和验证 target |
| Undo/Redo | 取消尚未完成的手势 | 已完成 target 编辑的历史及图片切换时的清空 |
| Pan/Zoom、叠加层 | 视口变换与显示 | 选择哪个 Task 结果和哪些叠加内容 |

## 3. 本轮迁移与后续维护

1. 工具 enum、笔触/图形/多边形手势、平移状态和选区变换矩阵已移出 SG 文件。SG 保留按 mask 值进行的像素修改、Region 选择、编辑撤销和类别有效性解释。
2. 推理与结果页改用 `ImageResultCanvas`。它只绘制输入图像及 prediction overlay，复用 `ImageViewportTransform` 和平移手势，不创建空 mask 或撤销栈。
3. 测试边界：业务语义是合同，界面呈现不是。
   - [`dataset_visual_test.cpp`](../test/dataset_visual_test.cpp) 验证笔刷、填充、图形与多边形、阈值、Region 变换、撤销重做和纯背景标注。[`main_window_test.cpp`](../test/main_window_test.cpp) 验证类别管理及绘制结束后的类别确认。
   - [`main_window_test.cpp`](../test/main_window_test.cpp) 的合同是归属与同步：Task 切换后各页面仍可操作；DataNode 删除/替换只处理当前连接的 Task；Training/Inference/Results/Annotation 的模型清单跟随所属 TaskNode；Annotation、Inference、Results 共享 model_id + weight_id，分支无结果时清空 overlay；外部权重导入保留匹配结果、报告和独立 Node 资产。
   - 以下只是界面探针，可随设计修改：splitter 宽度和句柄收放、`responsiveMode` 等响应式属性、控件层级/父子关系/顺序、页面索引和数量、按钮文案/工具提示/图标文字、空状态由哪些具体控件组成。这些不再由常规 CTest 断言，测试也不再通过环境变量截图；视觉检查改为手动运行应用查看页面。
4. 若新增 Task 需要现有手势，直接组合已有工具状态并在自己的编辑边界解释结果。只在发现第二个真实消费者及重复行为时再提取额外共享能力；不引入 ToolRegistry、插件基类、通用 Builder 或第二份 target 状态。

## 4. 不得随迁移改变的语义

- SG 编辑中的 `QImage::Format_Grayscale8` 是暂态 mask；`0` 是背景，前景像素写当前 `AnnotationClass::mask_value`。提交时沿既有 `commit_segmentation_mask()` 转成 Core Regions；普通空白提交与用户确认的纯背景仍是不同状态。
- 保持现有像素边界、笔触形状、四邻域填充、图形绘制、阈值区间及内外选择、带同标签连通域的变换结果。Polygon 的右键、Enter、双击完成及 Esc 取消行为不变。一次编辑的撤销/重做和切换图片时清空历史的边界不变。
- 保持 TaskNode 的 sample、类别、推理结果和 Project 路径归属；CL 的类别赋值、`Unannotated`/`Annotated` 判定不经过 mask 工具。推理/结果页继续只读，所选 model_id + weight_id 的叠加层显示和清空规则不变。
- SG 组装点显式设置画布初始叠加透明度 `0.34`，标注页滑块初始显示 `42%`；推理/结果页显式使用 `0.42`。这是原有的不一致，重组保留了实际显示行为；显示与实际值的对齐应作为单独的行为修正处理。

## 5. 新任务与新工具的放置检查

- 新工具先分离输入手势与 target 修改：前者在工具层定义和维护，后者由使用它的 Task 解释并执行。任务专有的 target 转换、撤销历史与有效性检查留在 Task 编辑边界。
- 一个 Task 需要某个工具，不等于该工具属于这个 Task；其他 Task 暂时不用，也不构成把笔刷手势或视口操作塞进 SG 文件的理由。已提交 target 编辑的撤销历史仍属于 Task。
- 不新增平行的 Annotation 数据模型、工具配置中心或第二套持久状态。确需新增共享抽象时，先给出第二个真实消费者和重复行为的证据。

## 6. 类别颜色与结果属性交互

类别颜色选择是通用界面能力：类别列表的颜色块打开颜色选择器，确认后更新所属 TaskNode 的 `AnnotationClass.color`，取消时不修改。类别数据和 Project 保存仍由原有 TaskNode 持有；颜色选择器不依赖 Task 类型。

SG 推理预览按所选模型的类别 ID 对应当前 TaskNode 的类别颜色；Annotation、Inference 和 Results 显示相同配色。模型类别不再存在于当前 TaskNode 时使用 Core 原有颜色，避免把复用的 mask 数值当作同一类别。此处只改变界面叠加显示，不改写已经保存的推理结果或导出资产。

结果属性查看与具体 Task 解耦。展示契约如下：

- Task 适配层消费选中 model_id + weight_id 的完整 Core Result，产生临时展示项。结果和类别名称仍以该模型及其 Result 为准；展示项不持久化、不替代 Core Result。
- 一个 `ResultPresentationItem` 包含可选的显示文本、可选命中规则和任意数量的有序「属性名、属性值」条目。查看器只接收展示项、完成命中并显示条目，不检查 Task 类型，也不预设类别、面积、置信度等字段。
- 对空间结果，命中区域使用原图坐标。缩放和平移由画布坐标变换处理；不能用包围盒代替实际不规则分割区域。文本结果由显示文本的控件触发该展示项，无需虚构空间区域。
- SG 适配器只提供被接受的连通区域，显示模型类别、该区域的面积和置信度。CL 适配器在类别文本上提供置信度。将来的实例分割、检测等任务可提供实例编号、坐标、宽高或其他字段，查看器无需修改。
- 注释层隐藏时，不得选择、拖动或修改隐藏的标注；已有选区需清除。推理结果只读，选择结果不会产生 Annotation 编辑或撤销记录。切换图片、结果分支或隐藏推理层时清除对应的结果选择。

函数边界：

1. `present_result(...)` 位于各 Task 的适配代码。SG 输入 `SgResult` 和模型的标签到名称映射，输出展示项列表；CL 输入 `ClsResult` 和模型类别名称，输出一个文本展示项。每个 Task 决定属性、可见结果及命中规则。
2. `result_item_at(items, position)` 位于通用查看器：输入展示项列表和调用方坐标，按从上到下顺序调用命中规则，输出下标或 `-1`。图像画布传原图坐标；无空间范围的文本控件可直接激活展示项。
3. `show_result_properties(parent, global_position, item)` 位于通用查看器：输入父控件、屏幕位置和一个展示项，输出只读属性菜单；没有属性时返回空指针。菜单由 Qt 父控件管理，关闭时销毁。

展示项及其命中规则只随当前图片和结果分支存在。任务适配器必须让命中规则拥有所需几何数据，不能捕获会先于展示项销毁的临时 Result 引用。SG 当前按值保存 Core Region；切换结果或图片时替换展示项并关闭菜单，隐藏推理层时关闭菜单并暂停命中。Core Result、TaskNode 和 Project 仍是原有数据所有者，展示项不持久化。

该契约适用于能够把结果表示为文本属性并把交互映射到当前视图坐标的 Task。检测框、实例区域和关键点可以提供各自命中规则；CL 文本不需要空间命中。三维或其他非图像任务可将交互投影到自己的视图坐标；需要图片、曲线等非文本属性时应扩展展示项，而不是修改查看器加入 Task 判断。
