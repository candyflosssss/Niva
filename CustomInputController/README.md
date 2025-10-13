# CustomInputController 插件说明

- 功能分类：输入与设备 / 数据采集、音频采集与流式传输
- 主要功能：
  - 自定义输入设备接入与按键/事件分发。
  - 麦克风采集、音频缓冲与推流（HTTP / WebSocket / UDP），同时支持从网络拉取音频流做本地播放与统计。
  - 手部/外设数据监听，提供手部21点解析、平滑/过滤与骨骼驱动辅助。
  - UDP 监听器用于接收低延迟二进制/文本消息（含 C++/蓝图事件）。

- 完整类/对象清单（按角色分组）：
  - 流媒体/网络
    - UAudioStreamHttpWsSubsystem（UGameInstanceSubsystem）：音频 HTTP/WS 会话中枢；路由注册、推流/拉流、统计与调度；内置媒体 UDP 组播/单播分发、客户端对时与抖动缓冲。
    - UAudioStreamHttpWsComponent（UActorComponent）：为 Actor 提供推流/控制入口，管理缓冲、viseme 队列与与子系统的注册绑定。
    - UNetMicWsSubsystem（UGameInstanceSubsystem）：最小“网络麦克风”子系统，经 HTTP POST 获取 wsUrl 后建立 WS，接收二进制音频并做环形暂存（蓝图事件 OnAudioBinary）。
    - UUDPHandler（UObject）：轻量 UDP 接收器，封装 FUdpSocketReceiver；事件：OnBinaryReceived（C++）、OnDataReceived/OnDataReceivedDynamic（文本）。
    - UStreamProcSoundWave（USoundWaveProcedural）：过程音频波形，支持多生产者/单消费者入队、欠载淡入与内存压缩；用于拉流端播放。
  
  - 音频采集
    - UMicAudioCaptureComponent（UActorComponent）：采集本地麦克风；设备枚举、音量检测、分包与（可选）WebSocket 推送；动态多播事件（开始/停止/音量等）。
    - UMicAudioCaptureSubsystem（UGameInstanceSubsystem）：封装组件实例的全局访问与参数管理，向上层提供统一蓝图/代码接口。
    - Mic 相关设置类：
      - UAudioStreamSettings（UDeveloperSettings，Config=Game）：媒体 UDP 端口、接收缓冲、默认 WS/组件/viseme/同步/日志等参数。
      - UMicAudioCaptureSettings（UDeveloperSettings 或等效设置类，若存在）：麦克风采集默认参数（如采样率、声道、缓冲）。

  - 输入/设备与手部工具
    - UInputPlusSubsystem（UGameInstanceSubsystem）：
      - 从 UDP 文本流解析双手 21 点（左右分组）；缓存最新帧；对外广播手部数据（动态/非动态委托）。
    - UHandDataListenerComponent（UActorComponent）：
      - 订阅并输出左/右手数据；提供平滑（指数）、离群过滤、自适应阈值、速度限幅与丢帧策略等；可输出相对/世界旋转映射。
    - UHandKinematicsBPLibrary（UBlueprintFunctionLibrary；文件名 UHandRelRotBPLibrary.h）：
      - FHandRuntimeState / FHandLimitsConfig / FHandCalibOffsets 辅助结构；
      - 计算父->子相对旋转、腕部朝向；
      - Offset 标定/应用；Mannequin 左右手映射生成等。

- 其他公开头文件与结构：
  - AudioStreamSettings.h（见上）、StreamProcSoundWave.h、InputPlusSubsystem.h、HandDataListenerComponent.h、UHandRelRotBPLibrary.h、UUDPHandler.h 等。

- 典型交互关系：
  - 采集推流：UMicAudioCaptureComponent -> UAudioStreamHttpWsSubsystem / UNetMicWsSubsystem ->（HTTP/WS/UDP）服务端。
  - 拉流播放：UAudioStreamHttpWsSubsystem/UNetMicWsSubsystem -> UStreamProcSoundWave（本地播放）-> 统计/viseme。
  - 手部数据：UInputPlusSubsystem（UDP 解析） -> UHandDataListenerComponent（平滑/过滤/输出旋转）。

- 参考与定位：
  - 插件清单：CustomInputController.uplugin（依赖 NetworkCorePlugin）。
  - 源码：Plugins/CustomInputController/Source/CustomInputController（Public/Private）。
