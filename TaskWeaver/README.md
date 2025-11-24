# TaskWeaver 插件说明

- 功能分类：任务编排 / AI 行为
- 主要功能：
  - 提供任务（Task）抽象及其执行框架，便于以组件化方式在游戏中编排异步/延迟/序列任务。
  - 支持基于 Actor 组件的任务管理，集中调度任务队列与生命周期。

- 关键模块与类/对象（按角色分组）：
  - 任务模型
    - UTaskBase（UObject 基类）：任务基类，定义生命周期（启动、Tick/完成/取消）与通用接口。
    - UDelayTask：示例/内置延迟任务，实现基于时间的简单等待与回调。
  - 管理与编排
    - UTaskManagerComponent（Actor 组件）：挂载到 Actor 上的任务中枢，负责创建、入队、轮询与回收任务。

- 典型交互关系：
  - 游戏逻辑 -> UTaskManagerComponent：创建并调度 UTaskBase 派生任务（如 UDelayTask），监听完成事件后串联后续逻辑。

- 参考：
  - 插件清单：TaskWeaver.uplugin
  - 源码路径：Plugins/TaskWeaver/Source/TaskWeaver