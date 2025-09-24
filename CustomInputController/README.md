# 自定义输入控制器插件

## 概述

该插件提供了自定义输入控制功能和音频流传输能力。音频流模块可以捕获麦克风音频并通过WebSocket以PCM格式推送到指定服务器。

## 音频流模块

### 功能

- 捕获系统麦克风音频
- 实时转换为PCM格式
- 通过WebSocket推送到服务器
- 提供可配置的设置，如采样率、通道数、缓冲区大小等
- 支持语音活动检测(VAD)

### 使用方法

#### 通过蓝图使用

1. 将`AudioStreamingComponent`添加到你的Actor中
2. 配置组件属性，如自动启动、音频增益等
3. 调用组件的`StartStreaming`、`StopStreaming`等方法控制音频流

#### 通过代码使用

```cpp
// 启动音频流
UAudioStreamingBPLibrary::StartAudioStreaming();

// 停止音频流
UAudioStreamingBPLibrary::StopAudioStreaming();
# 麦克风音频捕获插件

这个插件允许您捕获麦克风输入，并通过WebSocket将PCM音频数据推流到指定的服务器。

## 功能特点

- 捕获本地麦克风音频
- 将音频数据以PCM格式通过WebSocket推流
- 支持不同的采样率和声道配置
- 提供音量增益控制
- 可视化音量级别
- 显示可用麦克风设备列表
- 自动重连机制
- 编辑器集成工具栏

## 使用方法

### 通过蓝图使用

1. 将`MicAudioCaptureComponent`添加到Actor中
2. 配置必要的参数（采样率、声道数、服务器URL等）
3. 调用`StartCapture`开始捕获
4. 调用`ConnectToServer`连接到服务器

### 通过子系统使用

在任何蓝图或C++代码中获取子系统并使用：

```c++
// C++示例
UMicAudioCaptureSubsystem* MicSubsystem = GetGameInstance()->GetSubsystem<UMicAudioCaptureSubsystem>();
if (MicSubsystem)
{
    MicSubsystem->ConnectToServer("ws://your-server-url:port");
    MicSubsystem->StartCapture(0); // 使用第一个麦克风设备
}
```

```
// 蓝图示例
// 获取MicAudioCaptureSubsystem
// 然后调用ConnectToServer和StartCapture
```

## 调试工具

插件包含一个调试小部件，可以在游戏中显示：

1. 创建一个继承自`MicAudioCaptureDebugWidget`的蓝图小部件
2. 在UI中添加该小部件
3. 通过小部件可以监控音频级别、管理设备连接等

## 配置选项

在项目设置中可以找到`麦克风音频捕获`配置部分，包含以下选项：

- 默认WebSocket服务器URL
- 默认采样率和声道数
- 缓冲区大小
- 音量放大系数
- 调试日志开关
- 重连机制配置

## 编辑器集成

插件在编辑器工具栏中添加了以下功能：

- 开始/停止麦克风捕获
- 连接/断开WebSocket服务器
- 刷新麦克风设备
- 打开设置

## 技术细节

- 使用`FAudioCapture`API捕获麦克风数据
- 支持8kHz到48kHz的采样率
- 支持单声道和立体声配置
- 将浮点音频数据转换为16位PCM
- 使用WebSocket传输数据
- 通过`MediaStreamPacket`协议包装数据

## 服务器要求

目标WebSocket服务器需要能够接收以下格式的数据：

- 24字节的`FMediaPacketHeader`头部
- 紧随其后的16位PCM音频数据

默认服务器URL为`ws://10.1.20.57:8765`
// 设置增益
UAudioStreamingBPLibrary::SetAudioStreamingGain(1.5f);
```

### 配置

在项目设置中可以找到
