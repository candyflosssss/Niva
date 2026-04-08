# NetworkCorePlugin 当前功能与蓝图使用文档

`NetworkCorePlugin`（下文简称 **NWC**）当前已经完成与 `MCPFramework` 的职责拆分。  
现在它的定位不再是“网络 + MCP 总插件”，而是一个聚焦于以下能力的运行时网络插件：

- UE 内置 HTTP 路由封装
- LLM 异步请求节点
- TTS 异步请求节点
- 轻量在线状态 / Agent 条件子系统
- TurnGrid 网格/迷宫业务子系统

如果你现在在项目里使用 NWC，可以把它理解为：

> **面向 Unreal 项目的通用网络基础层 + 一组可直接给蓝图使用的 LLM/TTS/TurnGrid 节点。**

---

## 1. 插件定位

### 1.1 当前保留职责

NWC 当前主要负责：

1. **HTTP 路由与响应封装**
2. **LLM 请求发送与流式结果转发**
3. **TTS 请求发送与音频字节回传**
4. **在线状态与世界条件控制**
5. **TurnGrid 地图生成与寻路辅助**
---

## 2. 模块与主要能力概览

### 2.1 Runtime 模块：`NetworkCorePlugin`

当前源码中的运行时公开入口主要有：

- `UNetworkCoreSubsystem`
- `UNivaNetworkCoreSettings`
- `UNivaLLMRequest` / `UNivaAliyunLLMRequest` / `UNivaAgentLLMRequest` / `UNivaRunnerLLMRequest`
- `UBlueprintAsyncNode`（LLM 蓝图异步节点）
- `UNivaTTSRequest` / `UAliyunTTSRequest` / `UNivaMelotteTTSRequest`
- `UTTSNode`（TTS 蓝图异步节点）
- `UNivaOnlineSubsystem`
- `UAgentSystemSubsystem`
- `UTurnGridSubsystem`

### 2.2 Editor 模块：`NetworkCorePluginEditor`

当前编辑器模块仍保留在插件结构中，但**已没有 MCP 相关编辑器可视化功能**。  
所以目前它不再承担 MCP 编辑器扩展职责。

---

## 3. 当前功能清单

## 3.1 HTTP 路由能力

主要类型：

- `UNetworkCoreSubsystem`
- `FNivaHttpRequest`
- `FNivaHttpResponse`
- `ENivaHttpRequestVerbs`

当前能力：

- 在配置端口上获取 UE 的 `HttpRouter`
- 绑定 HTTP 路由
- 把 UE 原生请求包装成蓝图可用结构
- 快速创建文本/JSON HTTP 响应

适用场景：

- 在 UE 内部快速起一个轻量 HTTP 接口
- 给外部工具、本地脚本、调试页面、局域网服务提供简单接口

---

## 3.2 LLM 请求能力

主要类型：

- `UNivaLLMRequest`
- `UNivaAliyunLLMRequest`
- `UNivaAgentLLMRequest`
- `UNivaRunnerLLMRequest`
- `UBlueprintAsyncNode`

当前支持的 LLM 提供方：

- `Ollama`
- `Aliyun`
- `NivaAgent`
- `Runner`

特点：

- 支持蓝图异步节点调用
- 支持流式进度回调
- 根据项目设置自动选择底层请求实现

---

## 3.3 TTS 请求能力

主要类型：

- `UNivaTTSRequest`
- `UAliyunTTSRequest`
- `UNivaMelotteTTSRequest`
- `UTTSNode`

当前支持的 TTS 提供方：

- `Fish`
- `Melotte`
- `Aliyun`

特点：

- 蓝图中通过异步节点直接发起请求
- 返回的是**原始音频字节数组**
- 目前没有把结果自动转换成最终可播放 `SoundWave` 作为默认输出

> 这一点非常重要：如果你在蓝图里使用 TTS 节点，默认拿到的是字节数据和请求对象，而不是直接可播的声音资源。

---

## 3.4 Online / Agent 状态能力

主要类型：

- `UNivaOnlineSubsystem`
- `UAgentSystemSubsystem`

当前能力偏轻量，主要是：

- 保存 `DesiredPawn`
- 保存当前平台名 `Platform`
- 保存 `isServer` 状态
- 在满足条件时创建 `AgentSystemSubsystem`

其中 `UAgentSystemSubsystem` 当前的创建条件包括：

- 必须是游戏世界
- 地图名必须是 `Rooms`
- `UNivaOnlineSubsystem.isServer` 必须为 `true`

这块更适合被理解为：

> **项目内的运行时状态与世界条件过滤层**

而不是完整的在线系统实现。

---

## 3.5 TurnGrid 业务能力

主要类型：

- `UTurnGridSubsystem`
- `FWayNodes`

当前能力：

- 生成迷宫墙体网格
- 维护可行走图
- A* 路径查找
- 生成路点与邻接关系
- 计算从任意起点出发可到达的关键路点
- 在网格坐标上生成 Actor

当前默认只在地图：

- `TurnGridGame`

中创建该子系统。

---

## 4. 配置说明

配置类：

- `UNivaNetworkCoreSettings`

配置段：

```ini
[/Script/NetworkCorePlugin.NivaNetworkCoreSettings]
Port=9090
```

### 4.1 通用网络配置

| 配置项 | 说明 |
| --- | --- |
| `Port` | `UNetworkCoreSubsystem` 使用的 HTTP Router 监听端口 |

### 4.2 TurnGrid 配置

| 配置项 | 说明 |
| --- | --- |
| `LocationDescriptions` | TurnGrid 路点名称池，用于给生成的节点命名 |

### 4.3 LLM 配置

| 配置项 | 说明 |
| --- | --- |
| `LLM` | 当前使用的 LLM 提供方 |
| `ShouldPrompt` | 是否在请求中附加系统提示词 |
| `LLMPrompt` | 系统提示词 |
| `LLMOllamaURL` | Ollama 接口地址 |
| `LLMModel` | Ollama 模型枚举 |
| `LLMAliyunURL` | 阿里云 LLM 接口地址 |
| `LLMAliyunModel` | 阿里云模型枚举 |
| `LLMAliyunAccessKey` | 阿里云访问密钥 |
| `AgentChatURL` | NivaAgent 接口地址 |
| `DefaultAgentID` | 默认 Agent ID |
| `LLMRunnerURL` | Runner 接口地址 |
| `LLMRunnerType` | Runner 请求类型 |
| `LLMRunnerCallbackURL` | Runner 回调地址 |

### 4.4 TTS 配置

| 配置项 | 说明 |
| --- | --- |
| `TTSRequestType` | 当前使用的 TTS 提供方 |
| `shouldTTSWait` | 是否等待 TTS 过程 |
| `TTSURL` | Fish TTS 地址 |
| `ReferenceID` | Fish 参考音色 ID |
| `TTSFishAPIKey` | Fish API Key |
| `MelotteTTSURL` | Melotte TTS 地址 |
| `TTSAliyunAccessKey` | 阿里云 TTS Key |
| `TTSAliyunURL` | 阿里云 TTS WebSocket 地址 |
| `TTSAliyunVoice` | 阿里云音色 |
| `TTSAliyunFormat` | 输出格式 |
| `TTSAliyunSampleRate` | 输出采样率 |
| `TTSAliyunVolume` | 音量 |
| `TTSAliyunRate` | 语速 |
| `TTSAliyunPitch` | 音调 |
| `bEnableSSML` | 是否启用 SSML |

---

## 5. 蓝图环境下的主要节点与入口

这一节重点面向蓝图使用者。

## 5.1 获取子系统的方式

NWC 的大多数能力都通过子系统或异步节点提供。

### GameInstance Subsystem

可通过蓝图获取：

- `UNetworkCoreSubsystem`
- `UNivaOnlineSubsystem`

### World Subsystem

可通过蓝图获取：

- `UTurnGridSubsystem`
- `UAgentSystemSubsystem`（当前无明确蓝图接口，主要用于运行时条件控制）

---

## 5.2 蓝图节点：`NWC LLM Chat`

对应入口：

- `UBlueprintAsyncNode::LLMChat(...)`

蓝图显示名：

- `NWC LLM Chat`

### 输入

| 输入名 | 类型 | 说明 |
| --- | --- | --- |
| `Chatted` | `Map<String, String>` | 历史对话，通常表示 user → assistant 的历史映射 |
| `Chat` | `String` | 当前输入文本 |

### 输出事件

| 事件 | 说明 |
| --- | --- |
| `ProgressDelegate` | 流式返回时持续触发 |
| `CompleteDelegate` | 请求结束时触发 |

### 行为说明

- 节点会根据 `NivaNetworkCoreSettings` 中的 `LLM` 自动选择底层请求实现。
- 如果当前选择的是流式模型，`ProgressDelegate` 会多次触发。
- `CompleteDelegate` 通常用于拿最终完整结果或结束状态。

### 适合的蓝图用法

1. 在 UI 或控制器蓝图里调用 `NWC LLM Chat`
2. 将 `Chat` 传入用户当前输入
3. 将 `ProgressDelegate` 绑定到文本框增量显示
4. 将 `CompleteDelegate` 绑定到最终 UI 状态收束逻辑

### 注意事项

- `Chatted` 是一个简单映射，不适合表达复杂多轮结构化消息。
- 不同提供方返回内容格式会经过插件内部转换，但仍建议你在蓝图侧按字符串消费，而不要假定所有平台原始格式完全一致。

---

## 5.3 蓝图节点：`NWC Send TTS Request`

对应入口：

- `UTTSNode::SendTTSRequest(...)`

蓝图显示名：

- `NWC Send TTS Request`

### 输入

| 输入名 | 类型 | 说明 |
| --- | --- | --- |
| `Message` | `String` | 要合成的文本 |

### 输出事件

| 事件 | 说明 |
| --- | --- |
| `CompleteDelegate` | 请求结束时触发，返回音频字节数组与请求对象 |

### 行为说明

- 节点会根据 `TTSRequestType` 自动选择 Fish / Melotte / Aliyun。
- Aliyun TTS 当前走 WebSocket。
- 完成时返回的是 **`TArray<uint8>` 音频字节流**。

### 适合的蓝图用法

1. 在交互蓝图或 UI 蓝图中调用 `NWC Send TTS Request`
2. 将文本传入 `Message`
3. 在 `CompleteDelegate` 中检查返回的字节数组长度
4. 如果需要播放音频：
   - 可以在 C++ 中补一层字节转 `SoundWave`
   - 或在后续流程中交给自定义音频处理模块

### 注意事项

- 该节点默认不是“直接播报节点”，而是“请求 + 返回音频数据节点”。
- 如果你需要真正的一键播报链路，建议在项目层再封装一层蓝图工具函数或组件。

---

## 5.4 子系统：`UNetworkCoreSubsystem`

主要蓝图可见函数：

- `BindRoute`
- `MakeResponse`
- `UnitoString`

### `BindRoute`

作用：

- 绑定一个 HTTP 路由到处理委托

典型用途：

- 给本地调试工具提供 HTTP 接口
- 给局域网设备、脚本工具、自动化服务暴露简单 API

### `MakeResponse`

作用：

- 快速构造 HTTP 响应

常用参数：

- 文本内容
- Content-Type
- 状态码

### `UnitoString`

作用：

- 将 Unicode 转义字符串转换为普通字符串

适用场景：

- 后端返回了 `\uXXXX` 形式文本时做快速解码

---

## 5.5 子系统：`UNivaOnlineSubsystem`

主要蓝图函数：

- `SetDesiredPawn`
- `GetDesiredPawn`
- `SetIsServer`

### 使用建议

适合用来保存：

- 玩家想要使用的 Pawn 名称
- 当前是否作为服务端运行

它不是完整登录系统，更像是项目内的运行时状态记录器。

---

## 5.6 子系统：`UTurnGridSubsystem`

主要蓝图函数：

- `GridSpawnActor`
- `GenerateMazeWalls`
- `FindPathAStar`
- `getWayNodes`
- `getWalkableWayNodes`

### `GenerateMazeWalls`

作用：

- 按宽高生成迷宫墙体坐标
- 同时输出可通行街道坐标

### `FindPathAStar`

作用：

- 在当前图上做 A* 寻路

### `getWayNodes`

作用：

- 获取当前关键路点及其邻接关系

### `getWalkableWayNodes`

作用：

- 获取从指定起点出发，不跨越其他关键路点即可到达的节点列表

### 蓝图典型用法

1. 获取 `TurnGridSubsystem`
2. 调用 `GenerateMazeWalls`
3. 根据返回的墙体坐标生成关卡内容
4. 使用 `FindPathAStar` 给角色或棋子计算路径
5. 使用 `getWayNodes` / `getWalkableWayNodes` 做事件点或回合逻辑

---

## 6. 蓝图推荐使用流程

## 6.1 LLM UI 对话流程

推荐流程：

1. 在 Widget 蓝图中采集输入文本
2. 调用 `NWC LLM Chat`
3. `ProgressDelegate` 中实时刷新聊天气泡
4. `CompleteDelegate` 中做最终状态收尾

适合：

- AI 聊天
- Agent 文本返回
- 调试模型接入

---

## 6.2 文本转语音流程

推荐流程：

1. 调用 `NWC Send TTS Request`
2. 在 `CompleteDelegate` 中拿到字节数组
3. 把音频字节交给你自己的播放逻辑

适合：

- NPC 语音生成
- 旁白生成
- 文本播报队列

---

## 6.3 TurnGrid 回合网格流程

推荐流程：

1. 在 `TurnGridGame` 地图中获取 `TurnGridSubsystem`
2. 调用 `GenerateMazeWalls`
3. 记录返回的 `Street` 与 `WayNodes`
4. 角色行动时调用 `FindPathAStar`
5. 需要高层逻辑时再用 `getWalkableWayNodes`

适合：

- 回合制迷宫
- 棋盘探索
- 路点事件触发

---

## 7. 当前限制与注意事项

### 7.1 TTS 默认输出不是 `SoundWave`

当前 TTS 蓝图节点返回的是：

- 音频字节数组
- 请求对象

如果项目需要“一步合成并播放”，建议在项目层补一层音频封装。

### 7.2 `UAgentSystemSubsystem` 目前不是蓝图主入口

它当前更像运行时条件子系统：

- 地图必须是 `Rooms`
- 且 `isServer = true`

所以不建议把它当作主要蓝图 API 使用。

### 7.3 `TurnGridSubsystem` 只在特定地图创建

默认仅在：

- `TurnGridGame`

世界中创建。

### 7.4 HTTP 路由更适合程序/高级蓝图场景

`BindRoute` 虽然对蓝图可见，但更适合：

- 有明确请求/响应结构设计的高级蓝图
- 或直接由 C++ 管理

---

## 8. 与 MCPFramework 的边界

如果你需要的是以下能力，请不要继续在 NWC 中寻找：

- MCP 工具注册
- MCP 参数模型
- MCP 组件暴露
- MCP 流式工具回调
- MCP 进度与结果回传

这些都已经属于：

- `Plugins/MCPFramework`

NWC 现在的职责重点是：

- HTTP Router
- LLM
- TTS
- Online 状态
- TurnGrid

---

## 9. 源码索引

### 核心子系统

- `Source/NetworkCorePlugin/Public/NetworkCoreSubsystem.h`
- `Source/NetworkCorePlugin/Private/NetworkCoreSubsystem.cpp`

### 配置

- `Source/NetworkCorePlugin/Public/NivaNetworkCoreSettings.h`
- `Config/DefaultNetworkCorePlugin.ini`

### LLM

- `Source/NetworkCorePlugin/Public/AsyncNode/LLMAsyncNode.h`
- `Source/NetworkCorePlugin/Private/AsyncNode/LLMAsyncNode.cpp`

### TTS

- `Source/NetworkCorePlugin/Public/AsyncNode/TTSAsyncNode.h`
- `Source/NetworkCorePlugin/Private/AsyncNode/TTSAsyncNode.cpp`

### Online / Agent

- `Source/NetworkCorePlugin/Public/NivaOnlineSubsystem.h`
- `Source/NetworkCorePlugin/Private/NivaOnlineSubsystem.cpp`

### TurnGrid

- `Source/NetworkCorePlugin/Public/TurnGridSubsystem.h`
- `Source/NetworkCorePlugin/Private/TurnGridSubsystem.cpp`

---

## 10. 一句话总结

现在的 `NetworkCorePlugin` 已经不再承担 MCP 的职责。  
它当前是一套更聚焦的 Unreal 网络与蓝图辅助插件，主要提供：

- HTTP 路由
- LLM 异步节点
- TTS 异步节点
- Online/Agent 状态管理
- TurnGrid 网格业务支持

如果你接下来希望，我还可以继续为 NWC 再补一份：

1. **纯蓝图用户速查表**
2. **面向程序的接口与返回格式文档**
