# LightFieldPreview 插件说明

- 功能分类：渲染 / 可视化 / 预览
- 主要功能：
  - 提供光场/体渲染相关的预览与调试工具，便于在编辑器/运行时快速查看效果与参数影响。

- 关键模块与类/对象：
  - 模块入口（LightFieldPreview 模块）：负责模块初始化/反初始化、注册预览面板/命令等。
  - 预览/调试工具：用于渲染光场样本、切片、参数调优（具体类见源码）。

- 典型交互关系：
  - 编辑器/运行时工具 -> 预览渲染：根据参数生成可视化画面，辅助调参与问题定位。

- 参考：
  - 插件清单：LightFieldPreview.uplugin
  - 源码路径：Plugins/LightFieldPreview/Source/LightFieldPreview