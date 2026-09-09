# OpenGL 抗锯齿 Demo — 问答索引

> 完整论述见 **[Anti_Aliasing_博客.md](Anti_Aliasing_博客.md)**（设计、实现与问答已融合）。  
> 源码地址：[GitHub 仓库](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_Anti_Aliasing)  
> **RenderDoc 问题记录：** [RenderDoc_问题记录.md](RenderDoc_问题记录.md)  
> 本文仅作 **快速检索**：问题 → 博客锚点 / 源码位置。

---

## 性能与主循环

| 问题 | 博客章节 | 关键文件 |
|------|----------|----------|
| `PerfStats::endFrame` 在算什么？`scenePass`/`postPass` 有什么用？ | [Q1](Anti_Aliasing_博客.md#q-perfstats) | `src/PerfStats.cpp`，`AntiAliasingApp::renderFrame` |
| 为什么 TAA 模式要持续出帧，其它模式可以 `waitEvents`？ | [§9.2](Anti_Aliasing_博客.md#92-主循环事件驱动-vs-taa-持续渲染)、[Q11](Anti_Aliasing_博客.md#q-poll-order) | `AntiAliasingApp::run` |
| `scenePassMs + postPassMs` 为何小于 `totalFrameMs`？ | [Q1](Anti_Aliasing_博客.md#q-perfstats) | `PerfStats.cpp` |
| `cameraDirty_` 是什么？ | [代码导读 §5](Anti_Aliasing_代码导读.md) | `AntiAliasingApp.hpp` |

---

## FBO 与出屏

| 问题 | 博客章节 | 关键文件 |
|------|----------|----------|
| 创建 FBO 时 `glDrawBuffers` 是在画图吗？ | [Q2](Anti_Aliasing_博客.md#q-drawbuffers) | `src/Framebuffer.cpp` |
| `blitColorToDefault` 做什么？ | [Q3](Anti_Aliasing_博客.md#q-blit) | `Framebuffer.cpp`，None 分支 |
| 为什么不用窗口自带 MSAA？ | [§6.3](Anti_Aliasing_博客.md#63-为何不用窗口自带-msaa) | `AntiAliasingApp::initWindow` |

---

## 全屏后处理

| 问题 | 博客章节 | 关键文件 |
|------|----------|----------|
| `GL_TRIANGLE_FAN` / `drawFullscreen` 在干什么？ | [Q4](Anti_Aliasing_博客.md#q-triangle-fan) | `PostProcess.cpp`，`TaaPass.cpp` |
| FXAA 的 C++ 如何实现平滑？ | [§7](Anti_Aliasing_博客.md#7-fxaa后处理抗锯齿)，[Q5](Anti_Aliasing_博客.md#q-fxaa) | `PostProcess.cpp`，`fxaa.frag` |
| `uTexelSize` 在哪里设置？ | [Q5](Anti_Aliasing_博客.md#q-fxaa) | `PostProcess::applyFxaa` |

---

## TAA

| 问题 | 博客章节 | 关键文件 |
|------|----------|----------|
| `TaaPass.cpp` 整体在做什么？ | [§8](Anti_Aliasing_博客.md#8-taa时间抗锯齿重点)，[Q10](Anti_Aliasing_博客.md#q-taapass) | `TaaPass.cpp`，`taa.frag` |
| `locHistoryColor_` 是纹理 ID 吗？ | [Q6](Anti_Aliasing_博客.md#q-texture-uniform) | `TaaPass::apply` |
| `uInvViewProj` 等 uniform 各干什么？ | [Q7](Anti_Aliasing_博客.md#q-taa-uniforms) | `TaaPass::apply` 149～153 行 |
| 为什么 `depth * 2.0 - 1.0`？ | [Q8](Anti_Aliasing_博客.md#q-depth-ndc) | `taa.frag` `reprojectHistory` |
| `hasHistory` 和 `validHistory_` 区别？ | [Q8](Anti_Aliasing_博客.md#q-has-history)，[§8.4](Anti_Aliasing_博客.md#84-hasprevviewproj_-与-validhistory_) | `TaaPass.cpp`，`AntiAliasingApp` |
| 为什么有 history 还要 `clipHistory`？ | [Q9](Anti_Aliasing_博客.md#q-clip-history) | `taa.frag` |
| `taa.frag` 逐行逻辑？ | [§8.3](Anti_Aliasing_博客.md#83-taafrag-完整逻辑) | `resources/shaders/taa.frag` |
| Halton jitter 做什么？ | [§8.2](Anti_Aliasing_博客.md#82-taapass-c-流程) | `TaaPass::nextJitter` |

---

## 架构与阅读

| 需求 | 文档 |
|------|------|
| 设计、实现、问答、对比表与观察指南 | [Anti_Aliasing_博客.md](Anti_Aliasing_博客.md)（[附录](Anti_Aliasing_博客.md#13-附录)） |
| 按文件读代码、时序表 | [Anti_Aliasing_代码导读.md](Anti_Aliasing_代码导读.md) |

---

*若博客与源码不一致，以源码为准；发现偏差可更新博客对应章节。*
