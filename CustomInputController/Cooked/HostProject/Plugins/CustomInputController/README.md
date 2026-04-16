﻿# CustomInputController 插件说明

- 功能分类：输入与设备 / 数据采集 / 音频流处理
- 主要功能：
  - 自定义输入设备接入，将 UDP 数据转换为 UE 输入事件与轴值。
  - UDP 文本/二进制接收、手部 21 点解析，以及蓝图友好的监听组件。
  - 音频 HTTP / WebSocket / UDP 推流、拉流、本地播放与基础统计。
  - FunASR 语音识别接入，以及网络麦克风链路的会话管理。

## 当前源码中的主要类与模块

- 模块与输入设备
  - `FCustomInputControllerModule`：插件模块入口，负责注册输入设备模块能力。
  - `FUDPInputDevice`：自定义输入设备实现，将 UDP 数据转换为键/轴输入。
  - `FMyCustomInputKeys` / `UCustomInputKey`：自定义按键注册与辅助对象。

- 通用 UDP 与手部数据链路
  - `UUDPHandler`：通用 UDP 接收器，提供文本与二进制接收委托。
  - `UInputPlusSubsystem`：解析手部关键点数据、缓存最近一帧，并对外广播。
  - `UHandDataListenerComponent`：订阅手部数据，执行平滑、过滤与姿态计算。
  - `UHandKinematicsBPLibrary`（文件名 `UHandRelRotBPLibrary.*`）：提供手部相对旋转、腕部朝向、标定与映射计算。
  - `UCICGazeTrackingSettings` / `UCICGazeTrackingSubsystem`：注视追踪相关配置与子系统。
  - `UCICRuntimeSettings`：统一管理输入/手部/注视/音频中继的运行时端口与自动启动策略。

- 音频流与网络麦克风
  - `UAudioStreamHttpWsSubsystem`：音频流会话中枢，负责 HTTP/WS/UDP 推拉流、分发与统计。
  - `UAudioStreamHttpWsComponent`：Actor 侧音频流组件，提供推流、拉流和播放控制入口。
  - `UNetMicWsSubsystem`：网络麦克风兼容门面；保留旧 API，但实时 WebSocket 连接已不再由它持有。
  - `UNetMicWsComponent`：Actor 侧网络麦克风组件；现在是 NetMic 实时连接与重连逻辑的唯一所有者。
  - `UStreamProcSoundWave`：过程音频波形，用于流式音频播放。
  - `UAudioStreamSettings`：音频流、同步与 Opus 相关设置。
  - `FCICWebSocketSession`：共享传输层 WebSocket 会话实现，供 `NetMic` / `FunASR` 等上层会话复用。

- ASR 相关
  - `UFunASRSettings`：FunASR 配置。
  - `UFunASRSubsystem`：FunASR WebSocket 会话与结果分发。
  - `UFunASRMicComponent`：本地音频采集并向 FunASR 子系统送帧。

## 典型交互关系

- 输入注入链：
  - `FCustomInputControllerModule` → `FUDPInputDevice` → `UUDPHandler` → UE 输入系统
- 手部数据链：
  - `UInputPlusSubsystem` → `UHandDataListenerComponent` → 蓝图/角色骨骼驱动
- 音频流链：
  - `UAudioStreamHttpWsComponent` ↔ `UAudioStreamHttpWsSubsystem` ↔ HTTP / WebSocket / UDP 服务端
- 网络麦克风 + 识别链：
  - `UNetMicWsComponent` / `UFunASRMicComponent` → `UFunASRSubsystem` → 识别结果回调

## 当前边界约定（重要）

- 传输层：`FCICWebSocketSession`
  - 只负责 WebSocket 建连、断连、文本/二进制收发与二进制分片组装。
- 会话层：`UNetMicWsComponent`、`UFunASRSubsystem`
  - 负责各自协议状态、重连策略、控制命令和业务协作。
- 兼容层：`UNetMicWsSubsystem`
  - 仅用于兼容旧蓝图 API，不再拥有实时 WebSocket 连接；它会把调用转发给已注册的 `UNetMicWsComponent`。

## 统一运行时配置

- 配置文件：`Plugins/CustomInputController/Config/DefaultCustomInputController.ini`
- 项目设置项：`CIC Runtime Settings`
- 当前可配置项包括：
  - 输入设备 UDP 端口 / 是否自动启动
  - 手部 UDP 端口 / 是否自动启动
  - 注视 UDP 端口 / 是否自动启动
  - 音频中继 UDP 绑定端口范围

## 使用提示

- 若要接入 UDP 手部数据：
  1. 确保 `UInputPlusSubsystem` 已在运行时初始化。
  2. 在目标 Actor 上挂载 `UHandDataListenerComponent`。
  3. 在蓝图中绑定组件事件，或轮询最近一帧手部数据。

- 若要接入网络麦克风：
  1. 在角色或控制器上添加 `UNetMicWsComponent`。
  2. 在 `BeginPlay` 中调用连接接口。
  3. 根据服务端协议请求设备列表、选择设备并开始/停止录音。

- 若要接入音频推流/拉流：
  1. 使用 `UAudioStreamHttpWsComponent` 发起控制。
  2. 通过 `UAudioStreamSettings` 配置缓冲、同步与 Opus 相关参数。

## 参考

- 源码：`Plugins/CustomInputController/Source/CustomInputController`
- 补充文档：`Plugins/CustomInputController/Docs/CustomInputController_Reference_And_Flow.md`

