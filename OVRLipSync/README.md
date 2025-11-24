# OVRLipSync 插件说明

- 功能分类：音频 / 语音 / 口型同步
- 主要功能：
  - 集成 Oculus OVR LipSync，基于音频输入推断 viseme/phoneme，实现角色口型同步。
  - 支持实时（麦克风/流音频）与离线播放两种工作模式。

- 关键模块与类/对象（按角色分组）：
  - 组件
    - UOVRLipSyncActorComponentBase：口型同步组件基类，封装上下文与公共流程。
    - UOVRLipSyncLiveActorComponent：实时口型同步组件，从实时音频流获取数据并驱动 viseme。
    - UOVRLipSyncPlaybackActorComponent：离线/回放口型同步组件，基于录制/预处理数据驱动口型。
  - 上下文与工具
    - FOVRLipSyncContextWrapper：上下文包装器，管理底层 OVRLipSync 上下文创建、销毁与调用。
    - CookFrameSequenceAsync：用于处理/烹饪帧序列的异步工具（便于批处理与资源准备）。

- 典型交互关系：
  - 音频输入（实时/文件） -> 组件（Live/Playback） -> 角色面部/骨骼驱动（根据 viseme 输出驱动曲线/蒙皮）。

- 参考：
  - 插件清单：OVRLipSync.uplugin
  - 源码路径：Plugins/OVRLipSync/Source/OVRLipSync
  - 依赖与库：OVRLipSyncShim（Windows/Android 对应库与延迟加载 DLL）