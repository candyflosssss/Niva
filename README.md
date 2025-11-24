# 插件总览（按作用分类）

本项目包含多个自研与第三方插件。以下按“作用/功能大类”进行归类，并给出各插件的主要功能与核心类/对象一览。每个插件目录下亦提供单独的 README 说明更详尽的信息。

- 输入与设备 / 数据采集
  - CustomInputController
    - 主要功能：自定义输入设备接入、音频采集与流式传输（HTTP/WebSocket/UDP）、手部数据监听等。
    - 关键类/对象：UAudioStreamHttpWsSubsystem、UAudioStreamHttpWsComponent、UMicAudioCaptureComponent、UMicAudioCaptureSubsystem、UMicAudioCaptureDebugWidget、UHandDataListenerComponent、UCustomInputKey、UHandRelRotBPLibrary、UUDPHandler（私有）。
    - 文档：./CustomInputController/README.md

- 网络通信 / 协议
  - NetworkCorePlugin
    - 主要功能：网络核心能力封装（内置 Web/HTTP/可能含 WS 支持）、统一的网络子系统、工具属性模型，便于游戏内与外部系统交互。
    - 关键类/对象：UNetworkCoreSubsystem、UMCPToolProperty 及其派生（UMCPToolPropertyString 等）、配置 NivaNetworkCoreSettings、内置第三方 civetweb。
    - 文档：./NetworkCorePlugin/README.md
  - SocketHelper
    - 主要功能：TCP/Socket 工具与蓝图函数库，提供客户端连接与常用 Socket 操作的封装。
    - 关键类/对象：USocketUtility（蓝图函数库）、FTCPClient、FTCPClientHandler、FSocketConnection 等。
    - 文档：./SocketHelper/README.md

- 音频 / 语音 / 口型同步
  - OVRLipSync
    - 主要功能：基于 Oculus OVR LipSync 的口型同步，支持实时/离线（播放）两种组件。
    - 关键类/对象：UOVRLipSyncActorComponentBase、UOVRLipSyncLiveActorComponent、UOVRLipSyncPlaybackActorComponent、FOVRLipSyncContextWrapper、CookFrameSequenceAsync。
    - 文档：./OVRLipSync/README.md
  - SpeakerDialogue
    - 主要功能：对白/播报相关的辅助逻辑（如说话者/对白控制等）。
    - 关键类/对象：模块入口类（SpeakerDialogue 模块），以及与对白控制相关的组件/工具（详见插件自述）。
    - 文档：./SpeakerDialogue/README.md

- 任务编排 / AI 行为
  - TaskWeaver
    - 主要功能：任务系统与编排，支持延迟、队列、组件化管理任务等。
    - 关键类/对象：UTaskManagerComponent、UTaskBase、UDelayTask 等。
    - 文档：./TaskWeaver/README.md

- 渲染 / 可视化 / 预览
  - LightFieldPreview
    - 主要功能：光场/体渲染相关的预览与调试辅助。
    - 关键类/对象：模块入口类（LightFieldPreview 模块）及相关预览工具（详见插件自述）。
    - 文档：./LightFieldPreview/README.md

- 物理模拟
  - KawaiiPhysics
    - 主要功能：次级物理（抖动/摆动）模拟，常用于角色头发、配饰等的物理效果。
    - 关键类/对象：KawaiiPhysics 插件提供的物理节点/组件（详见插件自述）。
    - 文档：./KawaiiPhysics/README.md

- 内容资源
  - ArtContent
    - 主要功能：内容（资源）型插件，集中存放项目通用的美术资源与数据资产。
    - 关键类/对象：资源与数据资产（无运行时代码）。
    - 文档：./ArtContent/README.md

提示：若需快速了解具体 API 与使用方式，请进入各插件目录阅读对应 README（列出了主要类/对象与作用、典型使用方式与交互关系）。