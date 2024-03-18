# OpenGL 抗锯齿对比框架 (OpenGL_Anti_Aliasing)

> 源码地址：[GitHub 仓库](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_Anti_Aliasing)  
> 完整技术文档与架构指南请参阅：[docs/README.md](docs/README.md)

---

## 1. 项目简介

这是一个基于 OpenGL 4.x 的 **四种抗锯齿 (Anti-Aliasing) 方案对比测试框架**。在同一场景、同一套前向渲染 Pass 下，运行时按键实时切换对比：

| 模式 | 按键 | 方案类型 | 核心思想 |
| :--- | :--- | :--- | :--- |
| **None** | `1` / `F1` | 无 AA (基线) | 场景渲染到单采样离屏 FBO 后直接 blit 输出到屏幕 |
| **MSAA** | `2` / `F2` | 硬件多重采样 AA | 硬件多重采样 FBO 渲染，离屏 resolve 合成出屏 |
| **FXAA** | `3` / `F3` | 屏幕后处理 AA | 依赖感知亮度 (Luma) 检测图像强对比边缘并沿切线模糊 |
| **TAA** | `4` / `F4` | 时间复用 AA | 相机子像素抖动 (Halton Jitter) + 历史帧重投影与 AABB Color Clamping 混合 |

---

## 2. 快速开始与构建运行

### 编译工程
```bash
cmake -B build -S .
cmake --build build --config Release
```

### 运行控制按键
- `1` ~ `4` / `F1` ~ `F4`：切换抗锯齿模式 (None / MSAA / FXAA / TAA)
- `[` / `]`：MSAA 模式下切换 2x / 4x 采样倍率
- 鼠标左键/右键/滚轮：轨道球旋转、平移与缩放视角
- `ESC`：退出程序

---

## 3. 完整技术文档

详细的模块架构设计、渲染流水线时序、四种 AA 算法数学推导、`fxaa.frag` / `taa.frag` 源码解析、问答集锦 (Q1~Q11) 以及 RenderDoc 典型踩坑排查实录，请参阅：
👉 [**docs/README.md**](docs/README.md)
