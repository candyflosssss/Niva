# CustomInputController 模块化拆分执行计划

本文档给出 CIC 从现状迁移到目标低耦合架构的执行计划。目标不是一次性大爆炸重构，而是以“每一步都可编译、可验证、可回退”为原则，逐阶段推进。

目标架构设计见 [CustomInputController_Target_Architecture.md](CustomInputController_Target_Architecture.md)。

---

## 1. 执行原则

### 1.1 总原则

- 先拆基础层，再拆功能层，最后拆兼容层。
- 先消除横向硬耦合，再做模块物理迁移。
- 每一阶段都要保留可运行状态。
- 每一阶段都要明确验收口径与回退方式。

### 1.2 顺序原则

推荐顺序：

1. 提炼 Contracts 与 Transport
2. 解耦 Hand / Gaze / Input
3. 解耦 Audio / NetMic / ASR
4. 迁移 Settings
5. 建立兼容壳与清理遗留依赖

这个顺序的原因是：

- Transport 是所有域的共用底座，越早独立，后续 Feature 越好拆。
- NetMic 与 ASR 是当前最明显的横向耦合，应在 Audio 域物理拆分前先逻辑解耦。
- Settings 迁移涉及脚本路径和 CoreRedirects，应该在业务边界稳定后进行。

---

## 2. 总体阶段划分

| 阶段 | 名称 | 目标 |
| --- | --- | --- |
| Phase 0 | 基线与护栏 | 固化现状、建立文档、定义验收标准 |
| Phase 1 | 提炼基础层 | 建立 CICContracts / CICTransport |
| Phase 2 | 输入、手部、注视拆分 | 让 Input、Hand、Gaze 不再互相牵扯 |
| Phase 3 | 音频域内部解耦 | 先拆 Audio 内部对象边界 |
| Phase 4 | NetMic 与 ASR 解耦 | 清除最核心横向硬耦合 |
| Phase 5 | Settings 迁移 | 从大一统配置过渡到模块内配置 |
| Phase 6 | 兼容收敛与清理 | 保留兼容入口、下线旧依赖 |

---

## 3. Phase 0：基线与护栏

### 3.1 目标

- 固化当前功能边界、关键引用关系和运行行为。
- 补齐重构用文档。
- 明确每个阶段的验收标准。

### 3.2 交付物

- [CustomInputController_Target_Architecture.md](CustomInputController_Target_Architecture.md)
- [CustomInputController_Modularization_Execution_Plan.md](CustomInputController_Modularization_Execution_Plan.md)
- 当前依赖图和模块归属表

### 3.3 建议动作

- 盘点现有 Public API、Blueprint API、Settings 和关键资源路径。
- 明确哪些类属于兼容门面，哪些属于正式入口。
- 列出现有功能回归清单。

### 3.4 验收标准

- 所有主要类都有目标归属。
- 每一条横向耦合都能说明对应的消解路径。

---

## 4. Phase 1：提炼基础层

### 4.1 目标

- 建立 CICContracts 与 CICTransport。
- 把通用传输和共享协议对象从业务域中剥离出来。

### 4.2 核心任务

#### 任务 1：抽离 UUDPHandler 到 CICTransport

涉及对象：

- [Source/CustomInputController/Public/Input/UUDPHandler.h](../Source/CustomInputController/Public/Input/UUDPHandler.h)
- [Source/CustomInputController/Private/Input/UUDPHandler.cpp](../Source/CustomInputController/Private/Input/UUDPHandler.cpp)

目标：

- 迁移到 Transport 命名空间或模块路径。
- 保持功能不变，仅改变归属与 include 方向。

#### 任务 2：统一 WebSocket 通用会话

涉及对象：

- [Source/CustomInputController/Public/Transport/CICWebSocketSession.h](../Source/CustomInputController/Public/Transport/CICWebSocketSession.h)
- [Source/CustomInputController/Private/Transport/CICWebSocketSession.cpp](../Source/CustomInputController/Private/Transport/CICWebSocketSession.cpp)

目标：

- 作为唯一通用 WebSocket 传输层保留。
- 后续 Audio / ASR / NetMic 均优先使用该层，而不是各自直接碰 IWebSocket。

#### 任务 3：抽离共享协议对象

候选对象：

- Hand 数据对象
- AudioStreamPacket::FHeader
- 跨域委托和接口

目标：

- 移入 CICContracts。
- 让 Component 与 Subsystem 的 Public 头不再相互直连。

### 4.3 风险

- include 路径调整会影响大量源文件。
- 导出宏与 UHT 路径变化可能引入编译问题。

### 4.4 验收标准

- Input、Hand、Gaze、Audio、ASR 不再通过 UUDPHandler 形成目录级误依赖。
- AudioStreamPacket::FHeader 不再被迫通过 subsystem 头暴露。
- CICTransport 可以独立编译。

---

## 5. Phase 2：输入、手部、注视拆分

### 5.1 目标

- 使 CICInput、CICHandTracking、CICGaze 成为相互独立的 Feature。

### 5.2 核心任务

#### 任务 1：输入设备模块收口

涉及对象：

- [Source/CustomInputController/Public/Core/CustomInputController.h](../Source/CustomInputController/Public/Core/CustomInputController.h)
- [Source/CustomInputController/Private/Core/CustomInputController.cpp](../Source/CustomInputController/Private/Core/CustomInputController.cpp)
- [Source/CustomInputController/Public/Core/CustomInputKey.h](../Source/CustomInputController/Public/Core/CustomInputKey.h)
- [Source/CustomInputController/Private/Core/CustomInputKey.cpp](../Source/CustomInputController/Private/Core/CustomInputKey.cpp)

目标：

- 输入设备模块只处理输入键注册和输入注入。
- 不再持有对 Hand / Gaze / Audio 的概念性依赖。

#### 任务 2：将手部数据对象从 InputPlusSubsystem 中剥离

涉及对象：

- [Source/CustomInputController/Public/Input/InputPlusSubsystem.h](../Source/CustomInputController/Public/Input/InputPlusSubsystem.h)
- [Source/CustomInputController/Public/HandTracking/HandDataListenerComponent.h](../Source/CustomInputController/Public/HandTracking/HandDataListenerComponent.h)

目标：

- 将 FHandLandmarkData 抽到 Contracts 或 Hand 专属公共头。
- HandDataListenerComponent 不再通过具体实现头认识 InputPlusSubsystem。

#### 任务 3：Gaze 独立成域

涉及对象：

- [Source/CustomInputController/Public/Input/CICGazeTrackingSubsystem.h](../Source/CustomInputController/Public/Input/CICGazeTrackingSubsystem.h)
- [Source/CustomInputController/Private/Input/CICGazeTrackingSubsystem.cpp](../Source/CustomInputController/Private/Input/CICGazeTrackingSubsystem.cpp)
- [Source/CustomInputController/Public/Input/CICGazeTrackingSettings.h](../Source/CustomInputController/Public/Input/CICGazeTrackingSettings.h)

目标：

- Gaze 模块路径、依赖和配置完全脱离 Input 域。

### 5.3 验收标准

- Hand 不再依赖 Input 的实现层。
- Gaze 不再位于 Input 目录，也不再依赖 Input 模块。
- 输入、手部、注视三个域都可独立说明入口与配置。

---

## 6. Phase 3：音频域内部解耦

### 6.1 目标

- 先解决 Audio 域内部过度耦合，再谈物理拆模块。

### 6.2 核心任务

#### 任务 1：解耦 AudioStreamHttpWsComponent 与 Subsystem 的 Public 头关系

涉及对象：

- [Source/CustomInputController/Public/Audio/AudioStreamHttpWsComponent.h](../Source/CustomInputController/Public/Audio/AudioStreamHttpWsComponent.h)
- [Source/CustomInputController/Public/Audio/AudioStreamHttpWsSubsystem.h](../Source/CustomInputController/Public/Audio/AudioStreamHttpWsSubsystem.h)

目标：

- 将 AudioStreamPacket::FHeader 抽到独立公共头。
- 组件头中不再为了类型引用而包含 subsystem 头。

#### 任务 2：分离“会话控制”和“路由服务”职责

目标：

- Component 专注于单会话行为。
- Subsystem 专注于注册、路由、统计、缓冲和服务端协作。

#### 任务 3：梳理 AudioStreamSettings 职责

涉及对象：

- [Source/CustomInputController/Public/Audio/AudioStreamSettings.h](../Source/CustomInputController/Public/Audio/AudioStreamSettings.h)

目标：

- 保留 Audio Stream 配置核心职责。
- 将 NetMic 专属配置外移，为后续 CICNetMic 独立做准备。

### 6.3 验收标准

- AudioStream 组件与子系统的 Public include 关系被收敛。
- AudioSettings 中不再混放明显属于其他功能域的配置。

---

## 7. Phase 4：NetMic 与 ASR 解耦

### 7.1 目标

- 清除当前最强的横向硬耦合。

### 7.2 核心任务

#### 任务 1：定义音频帧消费接口

建议新增：

- IAudioFrameSink
- 或等效的委托 / 桥接协议

目标：

- NetMic 只负责输出 PCM 音频帧和状态。
- ASR 通过接口消费音频帧。

#### 任务 2：移除 NetMic 对 UFunASRSubsystem 的直接调用

涉及对象：

- [Source/CustomInputController/Private/Audio/NetMicWsComponent.cpp](../Source/CustomInputController/Private/Audio/NetMicWsComponent.cpp)

目标：

- 不再在 NetMic 内部直接调用 StartASR、StopASR、SendAudioFrame。
- 改为桥接器或编排层决定如何联动 ASR。

#### 任务 3：保留兼容门面但隔离边界

涉及对象：

- [Source/CustomInputController/Public/Audio/NetMicWsSubsystem.h](../Source/CustomInputController/Public/Audio/NetMicWsSubsystem.h)

目标：

- 兼容门面仅对旧 API 提供过渡。
- 不再被新路径主逻辑视作正式依赖。

### 7.3 验收标准

- NetMic 模块不再直接依赖 ASR 的实现类。
- NetMic 可以单独运行，不要求 ASR 模块存在。
- ASR 可以接收来自 NetMic 之外的其他音频源。

---

## 8. Phase 5：Settings 迁移

### 8.1 目标

- 让每个功能域拥有自己的配置边界。

### 8.2 核心任务

#### 任务 1：拆分 CICRuntimeSettings

涉及对象：

- [Source/CustomInputController/Public/Core/CICRuntimeSettings.h](../Source/CustomInputController/Public/Core/CICRuntimeSettings.h)

目标：

- 将输入、手部、注视、音频端口参数迁移到各自模块配置类。

#### 任务 2：拆分 AudioStreamSettings 中的跨域配置

目标：

- 将 DefaultNetMicWsUrl 等 NetMic 专属配置迁移出去。
- 保留 Audio Stream 业务自身配置。

#### 任务 3：补齐兼容重定向

目标：

- 为 Settings 类脚本路径、属性名变化配置 CoreRedirects。
- 确保项目旧 ini 能平滑读取。

### 8.3 风险

- Settings 脚本路径变化可能影响现有项目配置读取。
- Blueprint 或 Editor 面板显示路径可能变化。

### 8.4 验收标准

- 各域配置可以独立说明。
- 旧配置在迁移窗口期可被兼容读取。

---

## 9. Phase 6：兼容收敛与清理

### 9.1 目标

- 完成从旧结构到新结构的收口。

### 9.2 核心任务

- 保留旧模块名兼容壳或转发头。
- 清理不再需要的跨域 include。
- 清理不再需要的临时桥接逻辑。
- 更新文档，统一入口说明。

### 9.3 验收标准

- 新文档成为主文档来源。
- 兼容层只用于历史项目，不影响新路径。
- 代码中不存在明显违反依赖规则的新增引用。

---

## 10. 每阶段的测试建议

### 10.1 输入域回归

- 自定义键是否成功注册
- UDP 输入是否仍能注入 UE 输入系统
- 端口配置是否仍按预期生效

### 10.2 手部域回归

- 手部 21 点数据解析是否正确
- ListenerComponent 是否仍能收到广播
- 平滑、过滤、运动学工具是否行为一致

### 10.3 注视域回归

- Gaze UDP 是否可正常接入
- 时间戳与延迟估计是否正常

### 10.4 音频域回归

- AudioStream 会话建立是否正常
- 子系统注册、路由、播放和统计是否正常
- Opus 路径与 PCM 回退是否正常

### 10.5 ASR / NetMic 回归

- NetMic 是否可独立连通服务器
- ASR 是否可独立启动和识别
- 通过桥接器连接时，NetMic -> ASR 的端到端链路是否正常

---

## 11. 风险与应对

| 风险 | 描述 | 应对 |
| --- | --- | --- |
| UHT / 脚本路径变化 | UCLASS/USTRUCT 迁移模块后脚本路径变化 | 使用 CoreRedirects，并分阶段迁移 |
| Public include 震荡 | 大量头文件路径变化可能引发连锁编译错误 | 先做共享头抽离，再做物理迁移 |
| 行为回归 | 重构后运行时链路被破坏 | 每阶段配套最小回归场景 |
| 兼容债务失控 | 临时桥接代码长期保留 | 给兼容层设置明确下线窗口 |
| 配置错位 | 旧 ini 读不到新设置 | 为类名、属性名和脚本路径都准备重定向 |

---

## 12. 里程碑建议

### Milestone A：基础层完成

达成条件：

- CICContracts / CICTransport 建立
- UUDPHandler 与 FCICWebSocketSession 完成基础抽离

### Milestone B：输入、手部、注视独立

达成条件：

- Input、Hand、Gaze 不再横向耦合
- 对应配置开始独立

### Milestone C：Audio、ASR、NetMic 边界清晰

达成条件：

- NetMic 不再直接调用 ASR
- Audio Public 头关系被收敛

### Milestone D：兼容收口完成

达成条件：

- 新结构成为主路径
- 旧结构退化为兼容壳与转发层

---

## 13. 建议的实施顺序

如果只做最小风险路径，建议按下面的具体顺序执行：

1. 抽 UUDPHandler 到 CICTransport
2. 抽 FCICWebSocketSession 到稳定基础层并统一复用策略
3. 抽共享协议对象到 CICContracts
4. 将 Hand 数据结构与数据源接口从 Input 中拆出
5. 将 Gaze 从 Input 域独立出去
6. 收敛 Audio 组件与子系统的 Public 依赖
7. 将 NetMic 从 Audio 域独立出去
8. 用接口或桥接器替换 NetMic -> ASR 直连
9. 拆分 Settings，并补齐重定向
10. 保留兼容壳，清理遗留 include 和旧路径

---

## 14. 结论

这次模块化拆分的关键，不是“把文件移到新目录”，而是严格执行以下三件事：

- 公共能力下沉到 Foundation。
- Feature 之间不再直接认识对方的实现类。
- 兼容逻辑留在边缘，不再反向污染核心架构。

只要按阶段推进，并把每一步都控制在“可编译、可验证、可回退”的范围内，CIC 可以比较稳地从当前大一统模块演进到低耦合系统框架。