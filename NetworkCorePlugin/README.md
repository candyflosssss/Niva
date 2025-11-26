# NetworkCorePlugin 使用文档

本插件提供一个统一的“网络核心”能力，用于在 Unreal Engine 5 项目中：
- 启动轻量内置 HTTP 服务，用于常规http请求。
- SSE（Server‑Sent Events）通道，用于MCP服务器框架。


1. 架构与关键模块
- UNetworkCoreSubsystem（GameInstanceSubsystem）
  - 用于注册 UE 内部 `HttpRouter` 路由；
  - 提供 `BindRoute`、`MakeResponse` 等辅助，管理 `FHttpServerModule` 生命周期；
  - 通过项目设置中的 `Port` 指定监听端口。
- UMCPTransportSubsystem（GameInstanceSubsystem）
  - 基于 civetweb 启动另一个轻量 HTTP 服务，承载 MCP 消息与 SSE 推送；
  - 提供 JSON‑RPC 包装、会话管理（SessionId -> SSE 消息队列）、工具注册与可视化；
  - 通过项目设置中的 `MCPPort` 指定监听端口；
  - 对外暴露端点：`/message`（POST JSON‑RPC）、`/sse`（SSE 通道）、`/tools`（工具清单 JSON）、`/ui/tools`（简易可视化页）、`/tools/version`。
- UNivaNetworkCoreSettings（UDeveloperSettings）
  - 集中管理端口、LLM/TTS 等可选能力的配置；
  - 可在“项目设置 > NetworkCorePlugin”页面可视化配置，或写入 `DefaultNetworkCorePlugin.ini`。
#### UMCPToolProperty*（工具参数类型工厂与取值）
##### 用于定义和解析 MCP 工具的参数类型。
- UMCPToolPropertyString
  - `CreateStringProperty(Name, Desc)`（BlueprintCallable, Pure）：创建字符串参数定义。
  - `GetValue(Json)`（BlueprintCallable, Pure）：从 JSON 提取字符串值。
- UMCPToolPropertyNumber
  - `CreateNumberProperty(Name, Desc, Min, Max)`（BlueprintCallable, Pure）：创建浮点参数。
  - `GetValue(Json)`（BlueprintCallable, Pure）：提取浮点值。
- UMCPToolPropertyInt
  - `CreateIntProperty(Name, Desc, Min, Max)`（BlueprintCallable, Pure）：创建整型参数。
  - `GetValue(Json)`（BlueprintCallable, Pure）：提取整型值。
- UMCPToolPropertyActorPtr
  - `CreateActorPtrProperty(Name, Desc, ActorClass)`（BlueprintCallable, Pure）：创建 Actor 指针参数。
  - `GetAvailableTargets()`（BlueprintCallable）：枚举可选目标（Actor 名称）。
  - `GetValue(Json)`（BlueprintCallable, Pure）：从 JSON 解析到 Actor。
- UMCPToolPropertyComponentPtr
  - `CreateComponentPtrProperty(Name, Desc, ComponentClass)`（BlueprintCallable, Pure）：创建组件指针参数。
  - `GetAvailableTargets()`（BlueprintCallable）：枚举组件候选（标签/标识）。
  - `GetComponentByLabel(Label)`（BlueprintCallable）：按标签查找组件。
  - `GetValue(Json)`（BlueprintCallable, Pure）：从 JSON 解析到组件。
- UMCPToolPropertyArray
  - `CreateArrayProperty(Name, Desc, Property)`（BlueprintCallable, Pure）：创建“任意元素类型”的数组参数。
  - `GetJsonObject()`：导出 Schema（用于对外描述）。

提示：以上 `Create*Property` 与 `GetValue` 都标注为 BlueprintCallable/BlueprintPure，适合蓝图侧定义工具参数与解析入参。

- UMCPToolBlueprintLibrary
  - 提供蓝图侧工具属性 JSON 的解析与辅助追加。
- URefreshMCPClientAsyncAction
  - 辅助调用外部 HTTP 刷新工具列表（或进行连通性检查），包含 `OnSuccess`/`OnFailure` 多播。

源码位置
- Plugins/NetworkCorePlugin/Source/NetworkCorePlugin


2. 安装与启用
- 将插件放入项目 `Plugins/NetworkCorePlugin` 目录，确保在插件管理器中启用。
- 运行时会自动创建两个 Subsystem：
  - `UNetworkCoreSubsystem`：负责 `HttpRouter`；
  - `UMCPTransportSubsystem`：负责 MCP 服务与 SSE。
- 若要在游戏启动时立即开启 MCP 服务，可在任意时机调用：
  - `GetGameInstance()->GetSubsystem<UMCPTransportSubsystem>()->StartMCPServer();`
  - 默认会在端口 `MCPPort` 上监听并注册内置工具。


3. 配置项（UNivaNetworkCoreSettings）
主要字段（名称以实际代码为准）：
- 基础网络
  - `Port`：UNetworkCoreSubsystem 使用的 `HttpRouter` 端口；
  - `MCPPort`：UMCPTransportSubsystem 使用的 civetweb 端口；
- 可选 LLM/TTS（如不使用可不配置）
  - `LLM`, `LLMModel`, `LLMOllamaURL`, `LLMAliyunURL`, `LLMAliyunAPIKey`, ...
  - `TTSRequestType`, `TTSURL`, `TTSAliyunURL`, `TTSAliyunAccessKey`, `TTSAliyunVoice`, `TTSAliyunFormat`, `TTSAliyunSampleRate`, 以及其他供应商参数。

配置文件示例（Config/DefaultNetworkCorePlugin.ini）：
```
[/Script/NetworkCorePlugin.NivaNetworkCoreSettings]
Port=18080
MCPPort=18081
; 如使用 TTS/LLM，请按需填写对应 URL 与 Key
```


4. 运行时服务与接口
4.1 UE 内置 HttpRouter 路由（UNetworkCoreSubsystem）
- 绑定路由
```
UNetworkCoreSubsystem* NetCore = GetGameInstance()->GetSubsystem<UNetworkCoreSubsystem>();
NetCore->BindRoute(
    TEXT("/hello"),
    ENivaHttpRequestVerbs::GET,
    FNetworkCoreHttpServerDelegate::CreateLambda([](FNivaHttpRequest Req){
        FNivaHttpResponse Res;
        Res.HttpServerResponse = UNetworkCoreSubsystem::MakeResponse(TEXT("Hello"), TEXT("text/plain"), 200);
        return Res;
    })
);
```
- 发送响应
  - 使用 `MakeResponse(Text, ContentType, Code)` 生成 `FHttpServerResponse` 并交给 `OnComplete`。

4.2 MCP 传输服务（UMCPTransportSubsystem）
- 启动服务：`StartMCPServer()`
- 端点一览（监听 `MCPPort`）：
  - `POST /message?session_id=<id>`：接收 JSON‑RPC 请求包，解析后执行业务；
    - 服务器若需要向客户端返回数据，统一通过 SSE 推送（下述 `/sse`）。
  - `GET /sse?session_id=<id>`：建立 Server‑Sent Events 流；
    - 服务器端通过 `SendSSE(SessionId, Event, Data)` 推送消息；
    - 插件内部事件名通常为 `message`，`Data` 为 JSON 字符串（已去除换行）。
  - `GET /tools`：返回所有注册的 MCP 工具的 JSON；
  - `GET /tools/version`：返回版本号或摘要信息；
  - `GET /ui/tools`：内置的工具可视化（简易 HTML）。

- JSON‑RPC 约定（插件内置示例）：
  - `initialize`：客户端初始化时发送，服务器通过 SSE 回 `result`；
  - 其他方法可按需扩展。示例代码位于 `UMCPTransportSubsystem::HandlePostRequest`。


5. MCP 工具系统
5.1 工具与属性类型
- 结构体 `FMCPTool`
  - `Name`：工具名（唯一）；
  - `Description`：说明；
  - `Properties`：参数列表（`UMCPToolProperty*` 数组）。
- 属性基类 `UMCPToolProperty`（字段：`Name`, `Type`, `Description`）
- 已实现的属性类型（子类）：
  - `UMCPToolPropertyString`：字符串；
  - `UMCPToolPropertyNumber`：浮点数，带可选范围；
  - `UMCPToolPropertyInt`：整数，带可选范围；
  - `UMCPToolPropertyActorPtr`：Actor 指针（通过名称/Label 选择）；
  - `UMCPToolPropertyComponentPtr`：组件指针（继承自 `UMcpExposableBaseComponent` 的组件）；
  - `UMCPToolPropertyArray`：数组（包装任意子属性）。

5.2 注册工具与处理回调
- 注册 API：`UMCPTransportSubsystem::RegisterToolProperties(FMCPTool Tool, FMCPRouteDelegate Delegate)`
- 回调签名：`FMCPRouteDelegate(const FString& Result, UMCPToolHandle* Handle, const FMCPTool& Tool)`
  - `Result`：调用方上传的参数 JSON（字符串）；
  - `Handle`：用于回传进度/最终结果的句柄；
  - `Tool`：当前工具定义（含属性）。

示例（注册两个查询工具，代码节选自插件）：
```
FMCPTool Tool1;
Tool1.Name = TEXT("QueryObject");
Tool1.Description = TEXT("根据对象查询所有可用工具");
Tool1.Properties.Add(UMCPToolPropertyString::CreateStringProperty(TEXT("ObjectName"), TEXT("对象名称")));

FMCPRouteDelegate Delegate1;
Delegate1.BindDynamic(this, &UMCPTransportSubsystem::OnToolRouteCallback);
RegisterToolProperties(Tool1, Delegate1);

FMCPTool Tool2;
Tool2.Name = TEXT("QueryTool");
Tool2.Description = TEXT("根据工具查询所有可用对象");
Tool2.Properties.Add(UMCPToolPropertyString::CreateStringProperty(TEXT("ToolName"), TEXT("工具名")));

FMCPRouteDelegate Delegate2;
Delegate2.BindDynamic(this, &UMCPTransportSubsystem::OnToolTargetsCallback);
RegisterToolProperties(Tool2, Delegate2);
```

在回调中返回数据
- 使用 `UMCPToolHandle` 进行回传：
```
void UMCPTransportSubsystem::OnToolRouteCallback(const FString& Json, UMCPToolHandle* Handle, const FMCPTool& Tool)
{
    // 组装一个 JSON 字符串（或 FJsonObject）
    Handle->ToolCallback(/*isError=*/false, TEXT("处理中..."));        // 中间态文本
    Handle->ToolCallbackRaw(false, TEXT("{\"ok\":true}"), /*bFinal=*/true, 100, 100); // 最终态
}
```

5.3 从属性 JSON 取值（蓝图/CPP）
- 蓝图函数库 `UMCPToolBlueprintLibrary`：
  - `GetStringValue/ GetNumberValue/ GetIntValue/ GetActorValue/ GetComponentValue`
  - `AddProperty(FMCPTool& Tool, UMCPToolProperty* Property)`
- 典型蓝图流程：
  1) 拿到 `FMCPTool` 定义；
  2) 从网络接收的参数 JSON（字符串）中，使用上述 `GetXXXValue` 提取；
  3) 执行业务逻辑并通过 `UMCPToolHandle` 回发。


6. 蓝图节点与示例
- 异步节点：`URefreshMCPClientAsyncAction::RefreshMCPClient(WorldContextObject)`
  - 用于从外部地址拉取/刷新工具清单或进行连通性检查；
  - 结果通过 `OnSuccess(int32 Result)` / `OnFailure(int32 Result)` 多播输出。
- 结合工具属性：
  - 在蓝图中可用 `UMCPToolPropertyXxx::CreateXxxProperty(...)` 组装 `FMCPTool` 参数定义；
  - 使用 `UMCPToolBlueprintLibrary::GetXxxValue` 从调用 JSON 提取对应值。


7. 客户端示例（curl/JS）
- 建立 SSE 会话（建议先生成随机 `session_id`）
```
# 终端窗口 1：监听 SSE（保持长连接）
curl -N "http://127.0.0.1:18081/sse?session_id=abc123"
```
- 发送 JSON‑RPC 请求
```
# 终端窗口 2：POST /message，服务器将通过 SSE 回结果
curl -X POST "http://127.0.0.1:18081/message?session_id=abc123" \
     -H "Content-Type: application/json" \
     -d '{
           "jsonrpc": "2.0",
           "id": 1,
           "method": "initialize",
           "params": {}
         }'
```
- 浏览器端 JS（最小示例）
```
const sid = 'abc123';
const es = new EventSource(`http://127.0.0.1:18081/sse?session_id=${sid}`);
es.addEventListener('message', (ev) => {
  console.log('SSE message:', ev.data); // 一般为 JSON 字符串
});

fetch(`http://127.0.0.1:18081/message?session_id=${sid}`, {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'initialize', params: {} })
});
```


8. 常见问题与排查
- 端口占用
  - `Port`（HttpRouter）与 `MCPPort`（civetweb）必须可用；查看日志是否 `无法在端口 X 初始化 IHttpRouter` 或 `mg_start` 失败。
- SSE 无消息
  - 确认 `session_id` 一致；服务器日志若提示 `UNKNOW session`，说明未建立或已过期。
- 工具列表为空
  - 确认在 `StartMCPServer` 后调用了 `RegisterToolProperties`；
  - 访问 `GET /tools` 确认 JSON 是否包含你的工具。
- 参数解析失败
  - 核对 `FMCPTool.Properties` 中各 `UMCPToolProperty` 的 `Name/Type` 与上传 JSON 的键/值类型一致；
  - 对于 Actor/Component 指针，确保对象在当前关卡存在且可通过名称/唯一标识找到。


附注
- 插件还包含 LLM/TTS 相关的 Async 节点与配置，这些能力与网络核心解耦，可按需使用；
- 若要把 MCP 与项目中其他系统（例如 TaskWeaver）打通，可在工具回调里调用相应组件并用 `UMCPToolHandle` 上报进度与结果。