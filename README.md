# 插件总览（按作用分类）

本项目当前在 `Plugins/` 目录下包含 8 个插件。以下按“作用/功能大类”归类，并给出主要功能与入口对象。若某插件目录下提供了单独的 README，可继续进入对应目录查看细节。

- 输入与设备 / 数据采集
  - `CustomInputController`
    - 主要功能：自定义输入设备接入、UDP 手部数据监听、音频采集/推流与 WebSocket 音频链路。
    - 关键类/对象：`FCustomInputControllerModule`、`FUDPInputDevice`、`UUDPHandler`、`UInputPlusSubsystem`、`UHandDataListenerComponent`、`UAudioStreamHttpWsSubsystem`、`UAudioStreamHttpWsComponent`、`UNetMicWsSubsystem`、`UNetMicWsComponent`、`UFunASRSubsystem`、`UHandKinematicsBPLibrary`。
    - 文档：`./CustomInputController/README.md`

- 网络通信 / 协议
  - `NetworkCorePlugin`
    - 主要功能：网络核心能力封装，提供内置 HTTP 路由、MCP/SSE 传输、工具属性模型，以及 LLM/TTS 相关异步能力。
    - 关键类/对象：`UNetworkCoreSubsystem`、`UMCPTransportSubsystem`、`UMCPToolProperty` 及其派生类、`UNivaNetworkCoreSettings`、`URefreshMCPClientAsyncAction`。
    - 文档：`./NetworkCorePlugin/README.md`
  - `CoreManager`
    - 主要功能：项目级核心管理与日志能力，提供统一日志子系统和调试 UI 支撑。
    - 关键类/对象：`UCoreLogSubsystem`、`FCoreLogHelpers`、`UCoreLogListView`。

- 音频 / 语音 / 对白
  - `SpeakerDialogue`
    - 主要功能：对白/播报相关的同步与分发逻辑，提供说话流的开始、分片推送、结束广播等能力。
    - 关键类/对象：`UDialogueTalkComponent`、`FSpeakerDialogueModule`。
    - 文档：`./SpeakerDialogue/README.md`

- 任务编排 / AI 行为
  - `TaskWeaver`
    - 主要功能：任务系统与组件化编排，支持延迟任务、示例任务与任务管理组件。
    - 关键类/对象：`UTaskManagerComponent`、`UTaskBase`、`UDelayTask`、`UHeartbeatTestTask`。
    - 文档：`./TaskWeaver/README.md`

- 渲染 / 可视化 / 编辑器扩展
  - `LightFieldPreview`
    - 主要功能：光场/可视化预览相关插件入口，当前以模块与内容资源承载为主。
    - 关键类/对象：`FLightFieldPreviewModule`。
    - 文档：`./LightFieldPreview/README.md`
  - `NodeSource`
    - 主要功能：Blueprint 节点来源标记的编辑器扩展，用于在图表中显示函数/节点的来源信息。
    - 关键类/对象：`FNodeSourceModule`、`UNodeSourceSettings`。

- 内容资源
  - `ArtContent`
    - 主要功能：内容（资源）型插件，集中存放项目通用的美术资源与数据资产。
    - 关键对象：资源与数据资产（无运行时代码模块）。
    - 文档：`./ArtContent/README.md`

提示：若需快速了解具体 API 与使用方式，请优先阅读各插件目录中的 README；若插件未提供单独 README，则以 `Source/` 下公开头文件与 `.uplugin` 描述文件为准。
