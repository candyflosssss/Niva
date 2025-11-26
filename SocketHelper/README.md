# SocketHelper 插件说明

- 功能分类：网络通信 / 协议（Socket 工具）
- 主要功能：
  - 面向 TCP/Socket 的常用封装，提供蓝图可调用的实用函数与客户端连接工具。
  - 支持异步连接、发送/接收以及域名解析等常见网络操作。

- 关键模块与类/对象（按角色分组）：
  - 蓝图与工具
    - USocketUtility（UBlueprintFunctionLibrary）：暴露常用 Socket/网络相关的蓝图节点，如解析主机、检查地址、阻塞/异步操作封装等。
      - FHostResolverAction / FInfosAddrAction：配合 USocketUtility 实现的潜伏动作（Latent Action），用于异步域名解析/地址信息查询。
  - TCP 客户端
    - FTCPClient：TCP 客户端封装，负责连接、收发与回调处理。
    - FTCPClientHandler：为 FTCPClient 提供事件处理/数据分发的适配层。
  - 底层连接线程
    - FSocketConnection：基于 FRunnable 的底层连接与线程封装，负责稳定的数据收发循环与资源管理。

- 典型交互关系：
  - 蓝图/代码 -> USocketUtility：调用蓝图节点完成网络基础设施工作（解析、检查、连接）。
  - FTCPClient -> FTCPClientHandler：客户端在接收数据后交由 Handler 解析/分发。
  - FSocketConnection 为上述客户端提供线程化的底层支撑。

- 参考：
  - 插件清单：SocketHelper.uplugin
  - 源码路径：Plugins/SocketHelper/Source/SocketHelper