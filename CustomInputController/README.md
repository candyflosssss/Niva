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

// 设置增益
UAudioStreamingBPLibrary::SetAudioStreamingGain(1.5f);
```

### 配置

在项目设置中可以找到
