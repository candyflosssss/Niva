﻿# CustomInputController（CIC）插件详细文档

`CIC` 是 `CustomInputController` 插件的简称。它是一个面向 Unreal Engine 运行时交互链路的综合插件，聚焦以下几类能力：

- **自定义输入接入**：将外部 UDP 数据转换为 UE 输入轴/按键事件。
- **手部追踪数据接入**：接收左右手 21 点关键点数据，并提供蓝图友好的监听、平滑、过滤和运动学工具。
- **注视追踪接入**：监听外部 gaze 数据流，提供最新视线数据、时间戳和延迟估计。
- **音频流接入与分发**：支持组件级 HTTP / WebSocket 控制，子系统级 UDP 中继、UUID 路由、统计与音频包分发。
- **网络麦克风与语音识别**：支持网麦 WebSocket 会话、设备切换、录音控制，以及接入 FunASR 实时识别。

本文档基于当前插件源码整理，适合作为 `Plugins/CustomInputController` 的主使用说明。

---

## 1. 插件概览

### 1.1 基础信息

- 插件名称：`CustomInputController`
- 模块名：`CustomInputController`
- 模块类型：`Runtime`
- 加载阶段：`Default`
- 支持平台：`Win64`、`Android`
- 内容类型：`CanContainContent = true`
- 当前状态：`Beta`

### 1.2 插件依赖

`CustomInputController.uplugin` 中声明了以下插件依赖：

- `AudioCapture`：已启用，供本地音频采集链路使用。
- `CoreManager`：可选依赖，若工程中存在则启用更统一的日志输出。

### 1.3 模块依赖（编译层）

插件核心依赖包括：

- 输入：`InputCore`、`InputDevice`、`ApplicationCore`
- 网络：`Networking`、`Sockets`、`HTTP`、`HTTPServer`、`WebSockets`
- 音频：`AudioCaptureCore`、`AudioCapture`、`AudioExtensions`、`AudioMixer`、`libOpus`
- 引擎/框架：`Core`、`CoreUObject`、`Engine`、`UMG`
- 配置与工具：`DeveloperSettings`、`Json`、`JsonUtilities`、`Projects`

---

## 2. 功能矩阵

| 功能域 | 主要能力 | 主要入口 |
| --- | --- | --- |
| 自定义输入 | UDP 文本转 UE 输入轴 | `FCustomInputControllerModule`、`FUDPInputDevice` |
| 通用 UDP | 文本 / 二进制接收 | `UUDPHandler` |
| 手部追踪 | 左右手 21 点解析、缓存、广播 | `UInputPlusSubsystem` |
| 手部消费 | 平滑、离群点过滤、旋转计算 | `UHandDataListenerComponent` |
| 手部运动学 | 相对旋转、腕朝向、Offset 标定 | `UHandKinematicsBPLibrary` |
| 注视追踪 | UDP gaze 监听、最新数据与延迟 | `UCICGazeTrackingSubsystem` |
| 音频流组件 | HTTP/WS 会话控制、本地播放、Viseme | `UAudioStreamHttpWsComponent` |
| 音频流子系统 | UUID 注册、UDP 路由、统计、Socket Server/Client | `UAudioStreamHttpWsSubsystem` |
| 网络麦克风 | WebSocket 控制、自动重连、设备切换 | `UNetMicWsComponent` |
| 网麦兼容层 | 保留旧版 Subsystem API | `UNetMicWsSubsystem` |
| 实时 ASR | FunASR 连接、任务管理、结果分发 | `UFunASRSubsystem` |
| 本地麦克风采集 | 采集本机音频并送入 ASR | `UFunASRMicComponent` |
| WebSocket 传输层 | 通用连接/断开/文本/二进制收发 | `FCICWebSocketSession` |

---

## 3. 架构与职责边界

### 3.1 整体链路

- **输入注入链**  
  `FCustomInputControllerModule` → `FUDPInputDevice` → `UUDPHandler` → UE 输入系统

- **手部数据链**  
  `UInputPlusSubsystem` → `UHandDataListenerComponent` → 蓝图 / 角色骨骼 / 动作逻辑

- **注视数据链**  
  UDP Sender → `UCICGazeTrackingSubsystem` → 蓝图查询 / 事件订阅

- **音频流链**  
  `UAudioStreamHttpWsComponent` ↔ `UAudioStreamHttpWsSubsystem` ↔ HTTP / WebSocket / UDP 服务端

- **网麦 + 语音识别链**  
  `UNetMicWsComponent` / `UFunASRMicComponent` → `UFunASRSubsystem` → 识别事件回调

### 3.2 当前源码中的职责划分

这是实际使用时最重要的边界说明：

#### 传输层：`FCICWebSocketSession`

只负责：

- 建立 WebSocket 连接
- 关闭连接
- 发送文本与二进制消息
- 组装二进制分片
- 派发连接、关闭、文本、二进制等基础事件

**它不负责业务协议状态。**

#### 会话层：`UNetMicWsComponent`、`UFunASRSubsystem`、`UAudioStreamHttpWsComponent`

负责：

- 业务命令
- 重连策略
- 当前任务状态
- 消息解析
- 与蓝图/游戏逻辑协作

#### 兼容层：`UNetMicWsSubsystem`

该类已经降级为**兼容门面**：

- 仅保留旧蓝图 API
- 不再持有实时 WebSocket 会话
- 会把旧调用转发给已注册的 `UNetMicWsComponent`
- 保留一个被动音频缓存供旧逻辑兼容读取

#### 音频流子系统：`UAudioStreamHttpWsSubsystem`

当前版本中，它的职责更偏向**路由/注册/统计/UDP 中继**，而不是组件级会话控制：

- 组件注册与 UUID 管理
- Socket Server / Client
- 音频包封装、转发、统计
- 根据 UUID 将音频/文本/Viseme 路由给对应组件

真正的 HTTP / WebSocket 会话控制在 `UAudioStreamHttpWsComponent` 中。

---

## 4. 安装与启用方式

### 4.1 安装位置

当前插件位于：

- `Plugins/CustomInputController`

### 4.2 启用插件

在 Unreal Editor 中：

1. 打开 **Edit / Plugins**
2. 搜索 `CustomInputController`
3. 确认启用插件
4. 重启编辑器

### 4.3 工程注意事项

- 需要保证项目允许使用网络相关模块（UDP / HTTP / WebSocket）。
- Windows 下如本地调试收不到 UDP，请检查防火墙放行。
- 如果工程没有 `CoreManager` 插件，CIC 仍可工作，只是不会使用该插件提供的统一日志输出。

---

## 5. 项目设置与默认配置

插件当前有三组常用配置入口：

- `CIC Runtime Settings`
- `Audio Stream Settings`
- `Fun ASR Settings`

其中 `CIC Runtime Settings` 已在插件默认 ini 中提供默认值。

### 5.1 CIC Runtime Settings

配置来源：`Plugins/CustomInputController/Config/DefaultCustomInputController.ini`

| 配置项 | 默认值 | 说明 |
| --- | ---: | --- |
| `InputDeviceUdpPort` | `8091` | 自定义输入设备 UDP 监听端口 |
| `bAutoStartInputDeviceUdp` | `True` | 输入设备创建后是否自动启动监听 |
| `HandTrackingUdpPort` | `8092` | 手部数据 UDP 监听端口 |
| `bAutoStartHandTrackingUdp` | `True` | 手部子系统初始化时是否自动启动 |
| `GazeUdpPort` | `8888` | 注视追踪 UDP 监听端口 |
| `bAutoStartGazeListener` | `True` | 注视子系统初始化时是否自动启动 |
| `AudioSocketServerPortMin` | `19001` | 音频 Socket Server 自动选端口下限 |
| `AudioSocketServerPortMax` | `19010` | 音频 Socket Server 自动选端口上限 |

### 5.2 Audio Stream Settings

`UAudioStreamSettings` 中提供的重点配置包括：

- 协议模式：`LegacyHttpWs` / `PureWebSocket`
- 默认服务地址：
  - `LegacyWsHost`、`LegacyWsPathPrefix`
  - `PureWebSocketHost`、`PureWebSocketPath`
- 默认采样率与声道数：`DefaultSampleRate`、`DefaultChannels`
- 播放缓冲参数：
  - `DefaultJitterBufferThreshold`
  - `DefaultTargetBufferedTime`
  - `DefaultMinStartDuration`
- HTTP 文本流控参数：
  - `DefaultMinStreamRequestIntervalSeconds`
  - `DefaultStreamTextCoalesceWindowSeconds`
  - `DefaultMaxPendingStreamTextItems`
- Opus 设置：
  - `bEnableOpus`
  - `OpusBitrate`
  - `OpusComplexity`
  - `bOpusUseFEC`
  - `OpusPacketLossPct`
- NetMic 默认自动连接地址：`DefaultNetMicWsUrl`

### 5.3 Fun ASR Settings

`UFunASRSettings` 提供的关键项包括：

- `WebSocketUrl`
- `ApiKey`
- `WorkspaceId`
- `Model`
- `Format`
- `SampleRate`
- `bSemanticPunctuation`
- `MaxSentenceSilence`
- `LanguageHints`
- `bAutoReconnect`
- `ReconnectInterval`
- `MaxReconnectAttempts`

> 说明：当前插件默认配置文件中**没有内置完整的 ASR 服务参数**，通常需要在项目设置中由使用者按实际服务端填写。

---

## 6. 快速开始

下面按最常见的使用场景给出建议接入方式。

### 6.1 场景一：把 UDP 数据映射为 UE 输入轴

适合外部设备、眼动设备、控制器模拟器等把位置信息发给 Unreal。

#### 使用步骤

1. 启用插件并重启编辑器。
2. 确认 `CIC Runtime Settings` 中 `InputDeviceUdpPort` 为期望端口（默认 `8091`）。
3. 在项目的输入映射中绑定以下自定义键：
   - `Gaze_X`
   - `Gaze_Y`
   - `Gaze_Z`
   - `UDPButton1`
   - `UDPButton2`
4. 运行游戏，向目标端口发送 UDP 文本。
5. 在输入映射或蓝图事件中读取轴值。

#### 默认输入键

插件在启动时注册以下按键：

- 3D 向量相关：
  - `GazeXYZ`
  - `Gaze_X`
  - `Gaze_Y`
  - `Gaze_Z`
- 按钮相关：
  - `UDPButton1`
  - `UDPButton2`

#### 期望数据格式

`FUDPInputDevice::ParseTextToXYZ()` 当前采用的解析方式是：

- 用 `/` 拆分整条字符串
- 取**最后一段**作为 `X,Y,Z`
- 再按 `,` 拆分为 3 个浮点数

例如：

```text
38284/524.631,165.715,-0.674/493.470,171.578,-10.267
```

上例中实际生效的是最后一段：

```text
493.470,171.578,-10.267
```

#### 适用说明

- 当前实现主要是**轴输入注入**，并未在 `SendControllerEvents()` 中主动注入按钮按下/松开逻辑。
- 如果你需要按钮语义，建议在发送端扩展协议，并在 `FUDPInputDevice` 中补充解析与按键事件派发。

---

### 6.2 场景二：接入手部 21 点追踪数据

适合外部手势识别程序、视觉算法、手部骨骼驱动等场景。

#### 运行链路

- `UInputPlusSubsystem`：负责接收、解析、缓存和广播手部数据
- `UHandDataListenerComponent`：负责在 Actor 侧消费数据，支持平滑、滤波、旋转计算
- `UHandKinematicsBPLibrary`：负责进一步做手部旋转计算、标定和骨骼映射

#### 使用步骤

1. 确认 `HandTrackingUdpPort`（默认 `8092`）。
2. 保证 `UInputPlusSubsystem` 在运行时已初始化。
3. 在目标 Actor 上添加 `UHandDataListenerComponent`。
4. 在蓝图中绑定：
   - `OnBothHands`
   - 或调用 `GetLatestHandData()` 轮询
5. 如需驱动骨骼，再配合 `UHandKinematicsBPLibrary` 计算相对旋转。

#### 当前支持的数据格式

`UInputPlusSubsystem::ParseHandLandmarkData()` 当前支持以下 `Parts[2]` 类型：

- `left`
- `right`
- `both`

示例：

```text
protocol/version/left/x1,y1,z1/x2,y2,z2/.../x21,y21,z21
```

```text
protocol/version/right/x1,y1,z1/x2,y2,z2/.../x21,y21,z21
```

```text
protocol/version/both/left_1/left_2/.../left_21/right_1/right_2/.../right_21
```

#### 21 个关键点名称顺序

`UInputPlusSubsystem` 中当前固定顺序如下：

1. `WRIST`
2. `THUMB_CMC`
3. `THUMB_MCP`
4. `THUMB_IP`
5. `THUMB_TIP`
6. `INDEX_FINGER_MCP`
7. `INDEX_FINGER_PIP`
8. `INDEX_FINGER_DIP`
9. `INDEX_FINGER_TIP`
10. `MIDDLE_FINGER_MCP`
11. `MIDDLE_FINGER_PIP`
12. `MIDDLE_FINGER_DIP`
13. `MIDDLE_FINGER_TIP`
14. `RING_FINGER_MCP`
15. `RING_FINGER_PIP`
16. `RING_FINGER_DIP`
17. `RING_FINGER_TIP`
18. `PINKY_MCP`
19. `PINKY_PIP`
20. `PINKY_DIP`
21. `PINKY_TIP`

#### 组件侧增强能力

`UHandDataListenerComponent` 提供了大量可调参数，主要分为两类：

- **平滑**
  - `bEnableSmoothing`
  - `SmoothingAlpha`

- **过滤 / 稳定性增强**
  - `bEnableOutlierFilter`
  - `OutlierJumpScale`
  - `DropFrameBadPointRatio`
  - `bDropFrameOnTooManyOutliers`
  - `WarmupAcceptedFrames`
  - `bAdaptiveJumpThreshold`
  - `TargetFrameRate`
  - `MaxAdaptiveThresholdScale`
  - `bVelocityClampEnabled`
  - `MaxSpeedScalePerSecond`
  - `bTreatReacquireAsBaseline`

如果手部数据出现跳点、漂移、间歇失踪，优先从这些参数入手调节。

---

### 6.3 场景三：接入注视追踪数据

#### 使用步骤

1. 确认 `GazeUdpPort`（默认 `8888`）。
2. 获取 `UCICGazeTrackingSubsystem`。
3. 监听 `OnGazeDataReceived` 或主动查询：
   - `GetLatestGazeData()`
   - `GetLatestUnixTimestamp()`
   - `GetLatestLatencyMs()`

#### 期望 UDP 文本格式

当前实现中，`UCICGazeTrackingSubsystem::HandleDataReceived()` 要求格式为：

```text
Timestamp/<coordSpace>/LeftEyeX,LeftEyeY,LeftEyeZ/RightEyeX,RightEyeY,RightEyeZ
```

示例：

```text
1769162996041/<pupilWorld>/139.476,3.823,-2.119/139.476,0.439,-8.247
```

#### 当前行为特征

- 只接受**新于当前缓存时间戳**的数据，旧数据会被忽略。
- 会在内部合并广播，避免短时间内排队过多 GameThread 任务。
- `GetLatestLatencyMs()` 使用收到的 Unix 毫秒时间戳估算端到端延迟。

---

### 6.4 场景四：使用音频流组件进行推流、拉流与播放

#### 核心认识

- `UAudioStreamHttpWsComponent` 是**真正的组件级会话控制入口**。
- `UAudioStreamHttpWsSubsystem` 是**子系统级注册与路由中枢**。
- 推荐把组件挂在需要独立音频会话的 Actor 上。

#### 推荐接入步骤

1. 在 Actor 上添加 `UAudioStreamHttpWsComponent`。
2. 根据项目设置选择协议模式：
   - `Legacy HTTP + WS`
   - `Pure WebSocket`
3. 如果需要固定路由，可设置：
   - `PreferredKey`
   - 或读取 `RegisteredUuid`
4. 调用 `StartRunAndConnect()` 发起会话。
5. 后续调用：
   - `PostStreamText()`：发送文本流内容
   - `PostEndStream()`：结束流
   - `CloseWebSocket()`：关闭连接
6. 如果服务端通过 Socket 返回带 UUID 的音频、文本或 Viseme 数据，子系统会尝试路由到对应组件。

#### 重要属性

- 网络与节流：
  - `ProtocolMode`
  - `MinStreamRequestIntervalSeconds`
  - `StreamTextCoalesceWindowSeconds`
  - `MaxPendingStreamTextItems`
  - `StreamTextFlushIntervalSeconds`
  - `StreamTextMaxBatchChars`
  - `StreamFailureCooldownBaseSeconds`
  - `StreamFailureCooldownMaxSeconds`

- 播放缓冲：
  - `JitterBufferThreshold`
  - `TargetBufferedTime`
  - `MinStartDuration`

- Viseme：
  - `VisemeStepMs`
  - `VisemeKeyframeIntervalMs`
  - `OnViseme`
  - `OnVisemeArrayUpdated`

#### 子系统的 Socket Server 说明

`UAudioStreamHttpWsSubsystem` 可启动 UDP Socket Server，用于服务端 / 客户端之间转发带包头的数据包。端口由 `CIC Runtime Settings` 中的范围自动挑选。

音频包头类型当前定义为：

- `Text = 1`
- `Audio = 2`
- `Image = 3`
- `Control = 4`
- `Viseme = 5`

如果包头携带 UUID，子系统会尝试把消息路由给对应的 `UAudioStreamHttpWsComponent`。

#### 当前版本的关键注意点

- 组件拥有 HTTP / WS 会话逻辑，子系统不再统一代管这部分流程。
- 音频发送时会按 `FrameDurationMs` 做 PCM 分帧。
- 如果启用了 `Opus`，子系统会尝试进行编码；失败时会回退到 PCM。

---

### 6.5 场景五：使用 NetMic 网络麦克风

#### 推荐做法

优先使用 `UNetMicWsComponent`，不要再以 `UNetMicWsSubsystem` 作为主入口。

#### 使用步骤

1. 在角色、控制器或管理 Actor 上添加 `UNetMicWsComponent`。
2. 配置以下其一：
   - 组件属性 `AutoConnectWsUrl`
   - 或项目设置 `Audio Stream Settings -> DefaultNetMicWsUrl`
3. 连接后可调用：
   - `RequestDeviceList()`：请求设备列表
   - `SetDeviceIndex()`：切换设备
   - `StartRecording()`：发送 `<start>` 并自动通知 ASR 开始任务
   - `StopRecording()`：发送 `<end>` 并自动通知 ASR 停止任务
4. 通过事件接收状态和数据：
   - `OnConnected`
   - `OnDisconnected`
   - `OnError`
   - `OnServerMessage`
   - `OnAudioFrame`

#### 当前文本控制协议

组件会向服务端发送如下控制命令：

- `<start>`
- `<end>`
- `<list_devices>`
- `<set_device:N>`

#### 自动重连与状态恢复

`UNetMicWsComponent` 支持指数退避重连，并会尽量恢复以下状态：

- 已选择的设备索引
- 期望的录音状态

如果 `bForwardToASR = true`，收到的二进制音频帧还会自动转发给 `UFunASRSubsystem`。

---

### 6.6 场景六：接入 FunASR 实时语音识别

#### 主要入口

- `UFunASRSubsystem`：管理 WebSocket 连接、任务与结果
- `UFunASRMicComponent`：直接采集本地麦克风并把音频送入 `UFunASRSubsystem`

#### 子系统使用步骤

1. 在 **Project Settings** 中填写 `Fun ASR Settings`。
2. 获取 `UFunASRSubsystem`。
3. 调用 `StartASR()`。
4. 在任务开始后持续调用 `SendAudioFrame()` 送入 PCM 数据。
5. 接收以下事件：
   - `OnTaskStarted`
   - `OnResultReceived`
   - `OnTaskFinished`
   - `OnTaskCompletedWithFullText`
   - `OnError`
6. 调用 `StopASR()` 结束识别。

#### 当前任务协议行为

插件当前会向服务端发送：

- `run-task`
- `finish-task`

并处理以下服务端事件：

- `task-started`
- `result-generated`
- `task-finished`
- `task-failed`

#### 音频缓存行为

当调用 `StartASR()` 后，如果任务还未真正进入 `task-started` 状态：

- 子系统会先缓存一部分音频
- 缓存上限约为 **2 秒 PCM16 音频**（按当前采样率估算）
- 在收到 `task-started` 后自动冲刷缓存

#### 本地麦克风组件使用

`UFunASRMicComponent` 适合需要直接采集本地系统麦克风时使用：

- `Start()`：启动本地采集并配合 ASR 使用
- `Stop()`：停止采集

---

## 7. 蓝图与 C++ 常用入口

### 7.1 子系统入口

常见运行时子系统：

- `UInputPlusSubsystem`
- `UCICGazeTrackingSubsystem`
- `UAudioStreamHttpWsSubsystem`
- `UNetMicWsSubsystem`（兼容层）
- `UFunASRSubsystem`

### 7.2 推荐蓝图使用习惯

- **输入轴类需求**：直接在输入映射里绑定 `Gaze_X/Y/Z`
- **手部追踪类需求**：在 Actor 上挂 `UHandDataListenerComponent`
- **音频会话类需求**：在 Actor 上挂 `UAudioStreamHttpWsComponent`
- **网麦类需求**：在 Actor 上挂 `UNetMicWsComponent`
- **本地语音识别类需求**：使用 `UFunASRMicComponent` + `UFunASRSubsystem`

---

## 8. 关键数据格式与协议摘要

### 8.1 输入设备 UDP 数据

格式特征：

- 用 `/` 分段
- 最后一段应为 `X,Y,Z`

示例：

```text
tag/anything/1.0,2.0,3.0
```

### 8.2 手部数据

格式特征：

- 前两段通常是协议头
- 第三段是 `left` / `right` / `both`
- 后续为 21 个或 42 个坐标点

单手示例：

```text
proto/v1/left/0,0,0/1,1,1/.../20,20,20
```

### 8.3 注视数据

格式特征：

- Unix 毫秒时间戳
- 坐标系标签
- 左眼坐标
- 右眼坐标

示例：

```text
1769162996041/<pupilWorld>/139.476,3.823,-2.119/139.476,0.439,-8.247
```

### 8.4 NetMic 控制命令

```text
<start>
<end>
<list_devices>
<set_device:N>
```

### 8.5 AudioStream UDP 包头

`AudioStreamPacket::FHeader` 当前包含：

- `Type`：包类型
- `Flags`：标志位（是否带 UUID）
- `Seq`：递增序号
- `Timestamp`：毫秒级时间戳
- `Uuid`：可选，16 字节 GUID

---

## 9. 兼容性与已迁移约定

这部分非常重要，能帮助你避免按旧文档误用接口。

### 9.1 `UNetMicWsSubsystem` 已降级为兼容层

- 旧项目还能继续调用
- 新项目建议全部改用 `UNetMicWsComponent`

### 9.2 `UAudioStreamHttpWsSubsystem` 不再统一持有组件级 HTTP/WS 会话

- 组件自身负责 `StartRunAndConnect()`、HTTP 队列和 WebSocket 生命周期
- 子系统负责注册、路由、统计与 UDP Socket 转发

### 9.3 `UUDPHandler::StopUDPReceiver()` 会清理委托

停止接收器时会清空：

- `OnDataReceived`
- `OnBinaryReceived`
- `OnDataReceivedDynamic`

如果你的业务需要“停止后再次启动”，应确保在重启监听前重新绑定委托。

---

## 10. 常见问题与排错建议

### 10.1 收不到 UDP 数据

检查以下项：

1. 端口是否正确
2. `bAutoStart...` 是否启用
3. Windows 防火墙是否拦截
4. 是否有其他程序占用端口
5. 发送端是否与插件期望格式一致

### 10.2 输入轴没有反应

检查：

1. 插件是否成功启用
2. `FMyCustomInputKeys::AddKeys()` 是否生效
3. 项目输入映射里是否真的绑定了 `Gaze_X / Gaze_Y / Gaze_Z`
4. 外部 UDP 文本最后一段是否是合法的 `X,Y,Z`

### 10.3 手部组件没有收到事件

检查：

1. `UInputPlusSubsystem` 是否已初始化
2. `HandTrackingUdpPort` 是否正确
3. `UHandDataListenerComponent` 是否成功挂载到 Actor
4. 输入数据是否满足 `left` / `right` / `both` 格式要求
5. 是否真的传满了 21 个点

### 10.4 注视延迟异常或无数据

检查：

1. 时间戳是否为 Unix 毫秒
2. 数据顺序是否为：时间戳 / 坐标系 / 左眼 / 右眼
3. 发送的数据是否总是比上一帧更新

### 10.5 NetMic 能连接但没有识别结果

检查：

1. `UNetMicWsComponent` 是否开启 `bForwardToASR`
2. `Fun ASR Settings` 是否填写正确
3. 录音开始时是否真的调用了 `StartRecording()`
4. 服务端返回的二进制是否确实为 ASR 所需音频格式

### 10.6 ASR 已连接但无结果

检查：

1. `ApiKey` 是否正确
2. `WebSocketUrl` 是否正确
3. `Format` / `SampleRate` 是否与送入音频匹配
4. 服务端是否返回 `task-started`
5. 是否在任务未启动前过早停止

---

## 11. 建议的接入策略

如果你是第一次在项目里使用 CIC，推荐按以下思路接入：

### 11.1 只做 UDP 输入映射

- 只使用：`FUDPInputDevice`
- 优点：接入轻量，直接走 UE 输入系统

### 11.2 做手部驱动 / 手势交互

- 使用：`UInputPlusSubsystem` + `UHandDataListenerComponent`
- 如需骨骼驱动，再加：`UHandKinematicsBPLibrary`

### 11.3 做远程语音输入 + ASR

- 使用：`UNetMicWsComponent` + `UFunASRSubsystem`
- 如果希望完全本地采集，再用：`UFunASRMicComponent`

### 11.4 做角色级语音播放 / Viseme / 流式返回

- 使用：`UAudioStreamHttpWsComponent`
- 将每个角色或每个会话都看作一个独立组件实例

---

## 12. 源码索引

### 12.1 核心入口

- `Source/CustomInputController/Public/Core/CustomInputController.h`
- `Source/CustomInputController/Private/Core/CustomInputController.cpp`

### 12.2 输入设备与键

- `Source/CustomInputController/Public/Core/CustomInputKey.h`
- `Source/CustomInputController/Private/Core/CustomInputKey.cpp`

### 12.3 运行时配置

- `Source/CustomInputController/Public/Core/CICRuntimeSettings.h`
- `Config/DefaultCustomInputController.ini`

### 12.4 UDP / 手部 / 注视

- `Source/CustomInputController/Public/Input/UUDPHandler.h`
- `Source/CustomInputController/Public/Input/InputPlusSubsystem.h`
- `Source/CustomInputController/Public/HandTracking/HandDataListenerComponent.h`
- `Source/CustomInputController/Public/HandTracking/UHandRelRotBPLibrary.h`
- `Source/CustomInputController/Public/Input/CICGazeTrackingSubsystem.h`

### 12.5 音频流 / 网麦 / ASR

- `Source/CustomInputController/Public/Audio/AudioStreamHttpWsComponent.h`
- `Source/CustomInputController/Public/Audio/AudioStreamHttpWsSubsystem.h`
- `Source/CustomInputController/Public/Audio/NetMicWsComponent.h`
- `Source/CustomInputController/Public/Audio/NetMicWsSubsystem.h`
- `Source/CustomInputController/Public/ASR/FunASRSubsystem.h`
- `Source/CustomInputController/Public/ASR/FunASRMicComponent.h`
- `Source/CustomInputController/Public/Transport/CICWebSocketSession.h`

### 12.6 补充文档

- `Docs/CustomInputController_Reference_And_Flow.md`
- `Source/CustomInputController/Docs/ASR_Usage_Guide.md`

---

## 13. 总结

`CustomInputController` 不是单一功能插件，而是一个把**输入、手部、注视、音频流、网络麦克风、实时 ASR** 整合到同一运行时体系中的插件。

如果用一句话总结它的定位：

> **CIC 是一个把外部实时数据流转换为 Unreal 可消费交互能力的运行时桥接层。**

推荐的新项目使用方式如下：

- 输入映射：`FUDPInputDevice`
- 手部交互：`UInputPlusSubsystem` + `UHandDataListenerComponent`
- 注视追踪：`UCICGazeTrackingSubsystem`
- 音频流会话：`UAudioStreamHttpWsComponent`
- 网麦：`UNetMicWsComponent`
- 语音识别：`UFunASRSubsystem`

如果你后续还需要，我可以继续为这个插件补两份配套文档：

1. **面向策划 / 蓝图开发者的简化版使用手册**
2. **面向程序的协议说明与二次开发文档**
