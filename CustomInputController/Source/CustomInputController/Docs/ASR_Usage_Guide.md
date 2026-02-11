# ASR 子系统使用说明（CIC 插件）

本文档介绍 CustomInputController 插件中的 FunASR 子系统（`UFunASRSubsystem`）的使用方法、典型时序与配置说明。本文仅聚焦子系统本身的使用，不包含其他组件的说明。

## 子系统概览

`UFunASRSubsystem` 负责与阿里云 DashScope FunASR WebSocket 服务建立连接、管理识别任务（run-task/finish-task）、发送音频帧与解析识别结果，并在异常时按策略重连。

## 子系统设置（`UFunASRSettings`）

在项目设置中配置以下参数：
- WebSocketUrl：DashScope 推理服务地址，例如 `wss://dashscope.aliyuncs.com/api-ws/v1/inference/`
- ApiKey：DashScope 的 API Key（用作 `Authorization: Bearer <key>`）
- WorkspaceId：工作空间 ID（可选，`X-DashScope-WorkSpace`）
- Format：音频格式字符串，通常 `pcm_s16le`
- SampleRate：采样率，推荐与音频源一致（默认 16000）
- bSemanticPunctuation / MaxSentenceSilence / LanguageHints：语义标点与静音、语言提示参数
- bAutoReconnect / ReconnectInterval / MaxReconnectAttempts：重连策略与阈值

## 对外 API（蓝图/C++）

- `StartASR()`：开始识别任务。若尚未连接，子系统会先连接，连接成功后自动发送 `run-task`。
- `StopASR()`：结束识别任务，向服务端发送 `finish-task`，并等待 `task-finished`。
- `SendAudioFrame(const TArray<uint8>& Bytes)`：在任务运行期间发送音频二进制帧（例如 PCM S16LE 16kHz 单声道）。当任务未运行时，子系统会丢弃数据以避免协议错误。
- `ForceDisconnect()`：强制关闭连接与清理内部状态（一般无需手动调用）。
- `IsTaskRunning()`：查询当前识别任务是否处于运行状态。

## 事件

- `OnTaskStarted`：收到 `task-started` 后触发，表示服务端准备好接收音频帧。
- `OnResultReceived(const FString& Text, bool bSentenceEnd)`：收到 `result-generated` 时触发，返回文本与句子是否结束标志。
- `OnTaskFinished`：收到 `task-finished` 后触发，表示任务结束。
- `OnTaskCompletedWithFullText(const FString& FullText)`：任务彻底结束后，返回本次会话聚合的完整文本。
- `OnError(const FString& ErrorMessage)`：连接错误、任务失败或协议异常时触发。

## 典型时序

1. 初始化阶段可选：根据 `UFunASRSettings` 配置，子系统在首次调用 `StartASR()` 时完成连接并发送 `run-task`。
2. 识别启动：收到 `OnTaskStarted` 后，开始以固定帧率向子系统调用 `SendAudioFrame(Bytes)` 推送音频数据。
3. 识别过程：持续接收 `OnResultReceived` 文本；可根据 `bSentenceEnd` 判断句子结束以做 UI/逻辑更新。
4. 识别结束：调用 `StopASR()`，等待 `OnTaskFinished` 与 `OnTaskCompletedWithFullText`（完整文本）。

## 错误与重连

- 连接错误或异常关闭：子系统根据设置中的 `bAutoReconnect` 与 `ReconnectInterval` 安排重连，超过 `MaxReconnectAttempts` 时停止重试。
- 协议错误（例如音频在任务未运行时发送）：子系统内部会丢弃帧并通过 `OnError` 提示，避免服务端返回 `Invalid payload data`。

## 注意事项

- 请确保音频帧的格式与 `UFunASRSettings` 中的 `Format` 与 `SampleRate` 一致（默认 PCM S16LE / 16kHz / 单声道）。
- 建议以固定帧时长（例如 20ms）发送音频，保持服务端缓冲稳定。
- 如需在引擎内统一管理日志，请关注 `LogFunASR` 与 CoreManager 的 `CoreLog` 输出。

## 参考路径

- 子系统源码：`Plugins/CustomInputController/Source/CustomInputController/Private/ASR/FunASRSubsystem.cpp`
- 子系统设置：`Plugins/CustomInputController/Source/CustomInputController/Public/ASR/FunASRSettings.h`
