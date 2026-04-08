# MCPFramework 使用文档

## 目标

`MCPFramework` 是独立的 MCP 插件，负责：

- MCP 工具注册与路由
- 工具参数模型与解析
- 工具回调句柄
- 组件暴露体系
- 基于 **streamable HTTP** 的 MCP 传输层

---

## 1. 核心模块

### `MCPFramework`

运行时模块，承载：

- `UMCPTransportSubsystem`
- `FMCPTool`
- `UMCPToolProperty*`
- `UMCPToolHandle`
- `URefreshMCPClientAsyncAction`
- `McpExposableBaseComponent`
- `McpSitComponent`
- `McpTwoPointComponent`
- `McpComponentRegistrySubsystem`

### `MCPFrameworkEditor`

编辑器模块，承载：

- `McpSitComponentVisualizer`
- `McpTwoPointComponentVisualizer`

---

## 2. 配置

配置类：

- `UMCPFrameworkSettings`

配置文件：

```ini
[/Script/MCPFramework.MCPFrameworkSettings]
StreamPort=8080
StreamBaseURL="http://127.0.0.1:8080"
StreamPath="/stream"
EndStreamPath="/end-stream"
```

字段说明：

- `StreamPort`：流式 HTTP 服务监听端口
- `StreamBaseURL`：外部客户端访问基地址
- `StreamPath`：建立流和发送 MCP 请求的主端点
- `EndStreamPath`：显式结束流的端点

---

## 3. 传输协议

当前传输协议为：

- `GET /stream`
  - 建立一个 chunked HTTP 长连接
  - 返回 `application/x-ndjson`
  - 首帧会下发 `stream.open`

- `POST /stream?stream_id=<id>`
  - 提交 MCP JSON-RPC 请求
  - 服务端异步处理，并将进度/结果写入该流

- `DELETE /stream?stream_id=<id>` 或 `POST /end-stream?stream_id=<id>`
  - 请求关闭流

### 首帧示例

```json
{"type":"stream.open","streamId":"abc123","writePath":"/stream?stream_id=abc123","endPath":"/end-stream?stream_id=abc123","contentType":"application/x-ndjson"}
```

### 心跳帧示例

```json
{"type":"heartbeat"}
```

### MCP 结果帧

工具回调最终仍然遵循 MCP JSON-RPC 语义，例如：

```json
{"jsonrpc":"2.0","id":1,"result":{"content":[{"type":"text","text":"ok"}],"isError":false}}
```

### MCP 进度帧

```json
{"jsonrpc":"2.0","method":"notifications/progress","params":{"progressToken":"42","progress":50,"total":100,"message":"running"}}
```

---

## 4. 运行时调试接口

- `GET /tools`
  - 返回当前已注册工具的 JSON
- `GET /tools/version`
  - 返回工具清单版本摘要
- `GET /ui/tools`
  - 返回内置调试页面

---

## 5. 与 TaskWeaver 的关系

`TaskWeaver` 现在直接依赖 `MCPFramework`：

- 通过 `UMCPTransportSubsystem` 注册任务工具
- 通过 `UMCPToolHandle` 上报进度与结果
- 通过 `FMCPTool` / `UMCPToolProperty*` 定义 MCP 工具参数

因此：

- Task 本身不需要关心底层是 SSE 还是 streamable HTTP
- 任务侧只需要继续使用 `ToolCallbackRaw` / `ToolCallback`

---

## 6. 备注

- 本插件当前已经独立于 `NetworkCorePlugin` 的公开类型边界。
- 传输层已不再暴露旧 `/message` + `/sse` 模式。
- 后续如果要继续标准化，可继续将 chunked NDJSON 收敛为更严格的单端点 MCP 传输协议。

