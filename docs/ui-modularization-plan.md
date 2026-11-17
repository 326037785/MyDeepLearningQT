# Qt 主窗口模块化计划

创建日期：2026-10-29。最后更新：2026-10-30。

当前状态：首轮代码迁移、模块内部收尾及日志呈现调整均已完成。诊断日志写入文件，通过独立弹窗查看；模块化完整验收的 12 项测试通过，后续日志目录修正已构建并通过 3 项相关测试。验证记录见第 8 节。

## 1. 目标与范围

当前主窗口存在低内聚、高耦合问题：界面构造、业务流程、文件操作和状态更新混在一起，并通过大量捕获 `this` 的 lambda 依赖 MainWindow 成员。

目标是明确职责、状态归属和依赖方向，使功能可以独立理解、修改和测试；在这些边界上通过少量稳定挂载函数接入功能。缩短函数是结果，不是唯一验收指标。

| 范围 | 约定 |
| --- | --- |
| 修改边界 | 本计划限于 Qt 前端；不修改 DeepLearning Core，也不编译第二份 Core。 |
| 行为基线 | 保持现有项目操作、模型资产规则及训练/推理生命周期；本轮用户新增要求：日志使用文件或独立弹窗，移除工作区内的常驻日志显示。 |
| 数据所有权 | 沿用 ProjectManager、Workflow 和 TaskNode 的既有职责；不建立第二份项目、模型或结果数据。 |
| 持久化 | 沿用现有格式、路径和保存规则；UI 模块化本身不授权修改存储契约。 |
| 扩展顺序 | 先验证普通模块通过挂载函数接入；独立 DLL 装载作为后续可选阶段。 |
| 实施授权 | 用户已授权按当前原则推进重构、日志呈现调整及必要验证；普通阶段切换继续执行。其他新增产品功能与 DLL 独立部署不包含在本次重构内。 |

所属契约见 [项目总览](project-overview.md)、[模块规则](adding-modules.md)、[Project 存储](project_spec.md) 和 [稳定规则](../.ai-bridge/decisions.md)。这些文档和当前用户决策优先于本计划。

## 2. 现状基线

以下是 2026-10-29 实施前的调研快照，不把行数作为长期契约；实际结构见第 3 节。

| 位置 | 观察 | 拆分意义 |
| --- | --- | --- |
| [MainWindow::buildUi](../src/app/main_window.cpp) | 约 1050 行，混合窗口装配、视图构建和业务操作。 | 必须迁移职责及成员所有权，不能只增加 buildXxx 辅助函数。 |
| 模型操作回调 | 导入约 302 行；导出、删除、清空分别约 71、56、48 行。 | 优先移出交互、格式解析、Core 调用、资产写入和提交过程。 |
| MainWindow 构造函数 | 还包含场景事件和训练/推理 Job 回调。 | 与 buildUi 一起审查，避免复杂度转移到另一个大入口。 |
| 首选项命令 | SettingsModule 创建 Preferences... 菜单。 | Language 子菜单与 Auto-save annotations 勾选项并列，沿用既有设置存储。 |
| [NodeWorkspace](../src/modules/workflow/ui/node_workspace.h) | 已通过明确的请求回调连接操作。 | 可以保留现有边界，逐步更换接收方；不要求一次性改写所有回调。 |
| [任务目录](../src/modules/task/application/catalog/task_module_catalog.h)、[工作流类型](../src/modules/workflow/model/workflow.h)、[序列化](../src/modules/workflow/storage/workflow_document.cpp) | 存在固定任务目录、节点枚举和 SG/CL 分支。 | UI 注册接口不能自动解决任意新任务或节点的接入。 |

上一轮界面改造已有 11 项 CTest 全部通过的验证记录。该记录仅作历史基线，不代表后续重构已通过验证。

## 3. 目标结构

用户确认的总原则：

> MainWindow 只保留 application shell；每个业务模块在自己的代码附近创建 action、view 和交互逻辑，通过少量稳定挂载函数接入 shell。业务状态继续由已有 domain 对象持有，不新增 UI 状态中心；只有真实跨模块复用的流程才提取 service/coordinator。

| 单元 | 职责与边界 |
| --- | --- |
| MainWindow | 提供页面和 Dock 挂载、窗口布局、状态栏、关闭与尺寸变化处理，以及功能模块的生命周期装配。 |
| 业务模块 | 各自创建 QAction、视图和命名操作，维护命令的可用条件；复杂流程不写入 shell 的 lambda。 |
| 既有 domain 对象 | ProjectManager、Workflow、TaskNode 继续持有业务状态；模块引用这些对象，不建立副本。 |
| 模块局部展示状态 | 视图保存控件、筛选和视口；执行模块持有既有 Job 与本次执行身份，不建立全局 UI 状态中心。 |
| 可复用流程 | 只有真实跨模块复用的流程才提取 service/coordinator；单次使用的复杂流程先保留为所属模块的命名方法。 |

不先增加 ActionRegistry、PanelRegistry、通用 AppContext、ServiceLocator 或插件基类。命令挂载优先使用 Qt 原生 QMenu/QToolBar 接口；页面和 Dock 使用 shell 的少量明确挂载函数。模块之间的短连接留在装配处。

实际模块与迁移结果：

| 当前内容 | 目标归属 |
| --- | --- |
| 新建/打开/保存/另存为、项目删除、首页及项目元数据 | [ProjectModule](../src/modules/project/ui/project_module.h)，引用已有 ProjectManager，创建项目命令和视图。 |
| 工作流标签、画布、节点库、属性、概览及 Dataset 变更确认 | [WorkflowModule](../src/modules/workflow/ui/workflow_module.h)，复用 WorkflowScene、PropertiesEditor 和 NodeWorkspace；对应视图及交互留在工作流模块。 |
| 模型导入/导出/删除/清空 | [ModelActions](../src/modules/model_management/application/model_actions.h) 内的命名操作，通过现有 NodeWorkspace 请求回调接入；复用既有 domain 和 Core 接口。 |
| 训练/推理启动、进度及完成处理 | [ExecutionModule](../src/modules/execution/application/execution_module.h)，迁入既有 Job 和执行身份字段，复用 TaskWorkspaceService。 |
| 首选项交互与语言设置 | [SettingsModule](../src/modules/settings/ui/settings_module.h) 创建 Preferences... 菜单、Language 子菜单和标注自动保存勾选项，工具栏复用同一菜单。 |
| 活动与诊断日志、训练日志查看 | [ActivityLog](../src/modules/diagnostics/activity_log.h)，写入会话文件，自行创建菜单 action 和独立弹窗；训练日志保留原 runs 路径。 |

MainWindow::buildUi 现为约 80 行的 shell 控件创建和模块装配；主窗口实现由约 2600 行收缩至约 240 行。业务操作 lambda 迁入上述模块的命名函数，装配处只保留短连接。

模块直接接收其需要的 domain 引用和输出回调。沿用的 JSON 写入小函数由模型导出和训练运行记录共同使用，放入 [json_file_writer](../src/common/io/json_file_writer.h)；没有引入新业务 service/coordinator。

本轮收尾沿用这些边界：WorkflowModule 分别构造画布、节点库和属性面板，标签切换、关闭与重命名共用命名的激活操作。ProjectModule 在项目操作入口核对运行限制，并同步首页、最近项目和共享 action 的可用性。两个模块通过 busy 回调查询 ExecutionModule；后者使用已有 Job 与运行身份覆盖启动、完成回调两个空档，完成处理清理运行身份后再恢复编辑。

用户后续明确要求日志落盘或弹窗查看：ActivityLog 替代首轮 OutputPanel，去掉底部 Output Dock 和训练页原始日志摘要/路径回显，移除不再需要的日志转发函数。主工作区保留操作状态、训练曲线和业务结果。用户进一步要求避免持续写入 C 盘：会话日志跟随 EXE，写入同级 `logs/`；当前工程为 `E:/C_Proj/MyDeepLearningQT/MyDeepLearningQT/Release/logs`，不再使用 AppData。训练日志保持在所属项目的 `runs/` 中。

## 4. 依赖与所有权规则

1. 功能模块创建自己的 action 和 view，宿主负责挂载；功能模块不访问 MainWindow 私有成员，也不依靠 findChild 查找其他模块的内部控件。
2. View 报告用户意图、展示数据；命名操作负责流程；业务服务不调用窗口或弹出对话框。
3. lambda 用于短连接和参数适配。包含业务分支、文件操作和提交过程的逻辑迁入命名函数，不用统一行数阈值机械判断。
4. 命令可用性归所属功能维护；需要运行状态时查询执行模块的既有 Job 和本次运行身份，覆盖启动及完成回调空档，不新增共享 UI 状态副本。
5. 异步执行固定其所属 workflow_id、node_id 及模型/权重身份；完成处理不能重新把当前选择解释为原操作目标。
6. 复用 ProjectHome、NodeWorkspace、TaskWorkspace、PropertiesEditor、ProjectManager 和 TaskWorkspaceService；不增加同功能的并行 Manager。
7. 仅在真实存在多种实现时提取策略接口；不提前建立通用服务定位器、事件总线或每种控件的 SPI。

## 5. 插件扩展边界

| 扩展类型 | 计划接入方式 | 限制 |
| --- | --- | --- |
| 新操作、报告导出 | 模块创建 QAction 及命名方法，挂到 QMenu/QToolBar。 | 使用既有项目和结果契约；有真实复用再提取服务。 |
| 新监控面板、报告页、设置页 | 模块创建 QWidget，通过 mount_page/mount_dock 接入。 | Dock 使用稳定 objectName；依赖显式传入，Qt 父对象持有挂载视图。 |
| 新模型资产格式 | 有真实需求后增加格式处理接口。 | 识别实际内容并核对后端能力，不能只判断文件后缀。 |
| 新节点或算法任务 | 另行审查类型、存储和执行契约。 | 不能仅通过 UI 注册完成；若需要 Core 变更，超出本计划范围。 |

内置功能使用 Qt 原生 action 挂载接口和 shell 的页面/Dock 挂载函数。新增内置模块通常只需增加功能文件及装配连接；只有既有挂载能力覆盖的扩展，才能要求 shell 挂载代码零修改。

MainWindow 持有内置模块生命周期并在 domain 之前释放模块；模块创建自己的命令，由模块或菜单父对象持有，Qt 父对象树持有挂载视图。当前接口用于源码模块接入，不承诺动态卸载视图或 DLL 插件。

DLL 阶段候选机制为 Qt 接口与 QPluginLoader。实施前再确定接口版本、构建配置及装载规则；首版候选生命周期为启动时装载、退出时释放，不默认承诺运行中热卸载。

## 6. 阶段与验收

状态只使用“未开始 / 进行中 / 已完成”。每阶段完成时填写实际改动、验证结果和遗留问题；类名或顺序调整先更新本表。

| 阶段 | 状态 | 工作范围 | 完成标准 |
| --- | --- | --- | --- |
| P1 移出业务流程 | 已完成 | 模型操作和首选项迁入所属模块的命名方法；保留现有操作语义。 | buildUi 不包含这些业务流程；取消、失败、成功路径有对应验证，原行为保持。 |
| P2 迁移视图与运行状态 | 已完成 | 提取 WorkflowModule 和 OutputPanel；迁移对应成员、刷新和连接；Job 回调进入 ExecutionModule。 | 各视图拥有内部控件；执行状态有明确所有者；保留运行期间的切换限制和原完成处理目标。 |
| P3 收缩 shell 与挂载入口 | 已完成 | 模块在本地创建 action/view，通过 Qt 接口和少量 shell 函数挂载；显式传入 domain 依赖。 | 菜单、工具栏、快捷键状态一致；shell 只做装配和窗口行为；无 UI 状态中心。 |
| P4 验证挂载能力 | 已完成 | 用临时测试组件验证命令、Dock 和页面挂载；不新增产品功能。 | 测试组件通过公开接口接入，验证页面切换、Dock 显隐及命令随模块释放。 |
| P4 补充收尾 | 已完成 | 按画布、节点库、属性面板拆分 WorkflowModule 的视图构造；标签切换/关闭/重命名进入命名方法；补齐项目命令的运行限制。 | 不增加 UI 状态副本；所有项目入口遵守同一 busy 查询；刷新不解除运行限制；标签操作与现有测试通过。 |
| P4 日志呈现调整 | 已完成 | 根据用户新增要求，以 ActivityLog 文件和弹窗替代工作区常驻日志。 | 日志实际落盘、弹窗按需读取；训练曲线和业务结果保留；相关集成测试通过。 |
| P5 可选 DLL 装载 | 未开始 | 只有在 P4 证明接口可用且确认独立部署需求后实施。 | 内置模块和 DLL 共享挂载契约；接口版本、构建和装载验证通过。 |

各阶段沿用 README 中的批准构建目录和测试方式，使用现有 Core 构建。验证与改动范围匹配：

- 业务服务验证参数、解析与执行结果；协调器验证取消、失败、提交及通知顺序。
- 视图组件验证交互与刷新；主窗口保留菜单、布局、翻译、页面切换和运行状态的集成验证。
- 阶段完成前运行适用的现有测试；完整重构验收重跑全部测试和发布目录检查，记录实际结果，不沿用历史通过记录。
- GUI 测试使用隔离设置及临时项目；清理非持久中间文件，保留增量构建缓存。
- 不改任务数据所有权、Project 存储契约或 Core；若发现必须改变这些契约，先记录并确认新的范围。

## 7. 待确定事项

| 问题 | 当前建议 | 确定时点 |
| --- | --- | --- |
| 后续模块拆分粒度 | 当前文件及职责见第 3 节；有真实独立消费者时再拆分。 | 后续功能开发时。 |
| 后续真实功能验证 | 本轮先通过测试组件验证挂载；真实产品扩展在有需求后加入。 | 新功能提出时。 |
| 是否需要 DLL 独立分发 | 当前未确定；普通模块挂载先行。 | 本轮验证后按实际需求决定。 |

## 8. 更新规则与进度记录

每次推进在本节追加一条记录，并更新对应阶段状态、验收证据和待确定事项。记录文件或提交、实际运行的检查和结果；没有运行的验证明确标注。目标发生变化时更新正文，不只在日志中描述偏差。

实现结构与验收记录同步到 README、模块规则及 AI bridge 的实现地图；domain 总览和存储契约保持既有定义。

| 日期 | 阶段 | 本次结果 | 验证与后续 |
| --- | --- | --- | --- |
| 2026-10-29 | 计划 | 根据代码调研及审核讨论建立初版计划。 | 历史记录：当时仅文档落盘。 |
| 2026-10-29 | P1–P3 | 用户确认 shell + 业务模块本地 action/view/交互方案，授权实施；移除注册中心和 UI 状态中心设计，开始迁移模块。 | 代码迁移及编译检查进行中，测试结果待补充。 |
| 2026-10-29 | P1–P4 验收 | 落地 ProjectModule、WorkflowModule、ModelActions、ExecutionModule、SettingsModule、OutputPanel。MainWindow 实现 2616 → 239 行，buildUi 为 81 行；同步模块规则、README 和实现地图。 | Release 全量构建成功；最后一轮 CTest 12/12 通过（96.71 秒）；发布目录白名单检查、git diff --check、121 条翻译占位符及模块文档链接检查通过。 |
| 2026-10-30 | P4 补充收尾 | 根据当前代码复查继续收敛模块内部职责和命令可用性；不进入可选 DLL 阶段。 | 改动前 main_window_integration、shell_ui_contract、feature_module_contract 共 3/3 通过（55.12 秒）；改动后结果待补充。 |
| 2026-10-30 | P4 日志呈现调整 | 用户明确要求日志采用落盘或弹窗方式；新增 ActivityLog，移除 Output Dock 和训练页原始日志摘要。 | 增加日志落盘、UTF-8 弹窗读取、训练日志查看及无常驻日志控件的验证；等待最终构建与全套测试。 |
| 2026-10-30 | P4 收尾验收 | 视图构造和标签操作已归入所属模块的命名方法；补齐直接项目入口、标签操作及异步完成期间的运行限制；活动日志落盘并按需弹窗查看。 | `cmake --build --preset gpu-vs2022-release` 成功，0 错误；`ctest --preset gpu-vs2022-release --timeout 180` 12/12 通过（86.41 秒）；Release 白名单、git diff --check、126 条翻译占位符检查通过。 |
| 2026-10-30 | 日志目录修正 | 用户指出 AppData 位于 C 盘；会话日志改到 EXE 同级的 logs/，当前构建位于 E 盘。Release 检查仅额外允许该目录中符合会话命名的日志文件；增加目录断言。 | 主程序及 3 个相关测试目标构建成功，0 错误；main_window_integration、shell_ui_contract、feature_module_contract 共 3/3 通过（42.32 秒）；Release 检查和 git diff --check 通过；原 AppData 日志目录仍不存在。 |

**模块验证**：shell_ui_contract 用测试组件挂载命令、页面和 Dock，并验证命令随所属对象释放。feature_module_contract 在独立宿主中验证首选项菜单挂载、模型导入取消、非法 manifest 拒绝以及模型导出文件和元数据。

本轮进一步验证运行期间的直接首页操作、最近项目命令、标签切换/关闭/重命名和刷新限制，以及限制解除后的正常操作。执行模块使用非法权重夹具触发真实 CPU 推理失败，核对启动期间的 busy 查询、重复启动拒绝、失败后的状态恢复和单次通知。测试权重是拷贝夹具；没有额外运行真实模型的成功 GPU 训练/推理任务。

用户截图中的 `invalid project-relative resource path` 来自新增测试夹具：Dataset 目录指向项目根，保存时产生不被现有存储契约接受的 `project:.`。夹具已改用项目内的 `dataset/` 子目录；保存测试增加非预期弹窗检查，并通过重新打开项目核对实际保存内容。产品的路径校验和存储契约未改变。

日志验证覆盖 EXE 同级目录、立即落盘、UTF-8 内容、菜单按需打开独立弹窗和既有训练日志读取；主窗口测试确认没有 Output Dock 和训练日志摘要控件。独立日志夹具在验证完成后删除自身日志；窗口集成测试日志位于 E 盘测试可执行目录内。设置继续通过隔离的 INI 目录处理。

首轮已检查中英文首页及窄窗口截图。本轮再次核对用户偏好文件和 CMakeUserPresets.json 的 SHA256 均与改动前一致；Core 工作区无变更，沿用已有 Core 构建。DLL 装载仍未实施。

清理记录：首轮验收当时，自动审批拒绝删除三个系统临时测试夹具和构建目录内的 ui-refactor-work 迁移中间文件（blocked by policy）；该项为历史记录。本轮已删除重命名后失效的单个 `src/DeepLearningStudioLib.dir/Release/output_panel.obj`（57,568 字节），保留全部活动目标目录、增量构建缓存及批准构建输出。检查确认 ui-refactor-work 已不存在，相关测试进程已退出。

日志目录修正的清理记录：已确认 C 盘原 AppData 日志目录不存在。自动审批拒绝删除批准构建目录 `.test-artifacts/Release/logs` 内的 6 个测试日志（共 1,574 字节），理由为 `blocked by policy`；这些文件暂时保留，没有通过其他方式重试。

## 9. 调研依据

- 本项目的项目总览、模块规则、稳定规则以及 main_window、node_workspace、任务目录和序列化实现。
- [Qt QAction](https://doc.qt.io/qt-6/qaction.html)：复用菜单、工具栏与快捷键命令。
- [Qt 信号槽](https://doc.qt.io/qt-6/signalsandslots.html)：对象间的明确事件连接。
- [Qt 应用插件](https://doc.qt.io/qt-6/plugins-howto.html)及[部署约束](https://doc.qt.io/qt-6/deployment-plugins.html)：接口声明、装载与构建配置。

上述在线文档为 Qt 6 现版；前次调研已用本机 Qt 5.15.2 官方 Plug & Paint 示例核对所需基础插件机制。实施仍以项目使用的 Qt 5.15 API 为准。
