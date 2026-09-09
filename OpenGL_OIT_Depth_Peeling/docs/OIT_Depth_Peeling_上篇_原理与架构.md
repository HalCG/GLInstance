# OpenGL OIT 之 Depth Peeling 实现（上篇）：原理与架构

> 源码地址：[GitHub 仓库](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_OIT_Depth_Peeling)  
> **RenderDoc 问题记录：** [RenderDoc_问题记录.md](RenderDoc_问题记录.md)

## 0. 前言

在上一篇 [Linked List OIT](OIT_Linked_List_上篇_原理与缓冲区设计.md) 中，我们介绍了通过逐像素链表在 GPU 内部收集并排序所有透明片段的方法。Depth Peeling（深度剥离）是另一种经典的 OIT 方案，它不依赖 SSBO 和原子操作，而是通过**多次渲染 Pass 逐层剥离**来实现透明排序。

Depth Peeling 的最大优势是**兼容性极好**——它只需要标准的深度测试和 FBO，不需要任何 OpenGL 4.3+ 的高级特性（SSBO、Image Texture、Atomic Counter）。但代价是渲染 Pass 数与场景深度复杂度成正比。

---

## 1. Depth Peeling 原理

### 1.1 核心思想

Depth Peeling 的思路非常直观：**把透明物体像洋葱一样一层一层剥开**。

```
第 1 层: 剥离最近的一层（所有像素上最近的片段）
第 2 层: 剥离第二近的一层（其余片段中最近的）
第 3 层: 剥离第三近的一层
...
直到某层再也没有片段 → 结束
```

每一层剥离后，将这一层的颜色按 Front-to-Back 混合到累积缓冲区中，最终输出到屏幕。

### 1.2 如何"剥离"一层？

核心机制是**利用上一层的深度纹理作为"最近深度阈值"**：

```
当前层裁剪条件: 片段的深度 > 上一层的深度
```

即：在渲染当前层时，将上一层的深度纹理作为输入，只保留深度值**大于**（即比上一层更远）的片段。

```
┌──────────────────────────────────────────────────────┐
│                    深度剥离示意                        │
│                                                      │
│   相机 ←─── Layer 1 ─── Layer 2 ─── Layer 3 ─── 背景  │
│              (最近)                               (最远) │
│                                                      │
│   Pass 1: 渲染整个场景 → 深度缓冲得到 Layer 1 的深度     │
│           剥离 Layer 1 的颜色，混合到累积缓冲            │
│                                                      │
│   Pass 2: 用 Layer 1 的深度纹理做裁剪 → 只保留更深片段   │
│           深度缓冲得到 Layer 2 的深度                   │
│           剥离 Layer 2 的颜色，混合到累积缓冲            │
│                                                      │
│   Pass 3: 用 Layer 2 的深度纹理做裁剪 → 只保留更深片段   │
│           ... 依此类推                                 │
└──────────────────────────────────────────────────────┘
```

### 1.3 深度缓冲乒乓机制

为了实现"用上一层的深度裁剪当前层"，需要**两个深度纹理**交替使用：

```
     Layer N 的深度纹理  ──→  作为 Layer N+1 的裁剪参考
     Layer N+1 的深度纹理 ──→  作为 Layer N+2 的裁剪参考
     ...
```

两个深度纹理在 `fboAccum_` 和 `fboPeel_` 之间以乒乓方式交换：

```
     inputDepthIndex  = 0  (fboAccum_.depth)
     outputDepthIndex = 1  (fboPeel_.depth)

     Layer 0: 用 fboAccum_.depth 裁剪 → 结果写入 fboPeel_.depth
     swap: inputDepthIndex=1, outputDepthIndex=0

     Layer 1: 用 fboPeel_.depth 裁剪 → 结果写入 fboAccum_.depth
     swap: inputDepthIndex=0, outputDepthIndex=1

     Layer 2: 用 fboAccum_.depth 裁剪 → 结果写入 fboPeel_.depth
     ...
```

---

## 2. 整体渲染流程

### 2.1 每帧流程

```
┌─────────────────────────────────────────────────────────────────────┐
│                        每帧渲染循环                                  │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Phase 1: initPeelBuffers()                                  │  │
│  │  ┌───────────────┐          ┌───────────────┐                │  │
│  │  │ fboAccum_     │          │ fboPeel_      │                │  │
│  │  │ color: (0,0,0,1)│        │ color: (0,0,0,0)│              │  │
│  │  │ depth: 0.0    │          │ depth: 0.0    │                │  │
│  │  └───────────────┘          └───────────────┘                │  │
│  │  inputDepthIndex = 0, outputDepthIndex = 1                   │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                            │                                        │
│                            ▼                                        │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Phase 2: peelAndBlend() — 逐层剥离循环                       │  │
│  │                                                              │  │
│  │  for layer in 0..maxLayers:                                  │  │
│  │    ┌────────────────────────────────────────────────┐        │  │
│  │    │ Step A: 准备剥离目标 FBO                         │        │  │
│  │    │  - 绑定 fboPeel_ 为渲染目标                      │        │  │
│  │    │  - 将 depthTexture(outputDepthIndex) 设为深度附件  │        │  │
│  │    │  - 清空颜色(0,0,0,0) + 深度(1.0)                  │        │  │
│  │    └────────────────────────────────────────────────┘        │  │
│  │                            │                                   │  │
│  │                            ▼                                   │  │
│  │    ┌────────────────────────────────────────────────┐        │  │
│  │    │ Step B: 渲染场景 → 剥离一层                       │        │  │
│  │    │  - Shader: depth_peeling_render.frag            │        │  │
│  │    │  - 输入: texture_depth (上一层的深度纹理)          │        │  │
│  │    │  - 裁剪: gl_FragCoord.z <= frontDepth → discard │        │  │
│  │    │  - 通过 GL_SAMPLES_PASSED 查询通过的片段数        │        │  │
│  │    └────────────────────────────────────────────────┘        │  │
│  │                            │                                   │  │
│  │                            ▼                                   │  │
│  │    ┌────────────────────────────────────────────────┐        │  │
│  │    │ Step C: 混合到累积缓冲                           │        │  │
│  │    │  - 绑定 fboAccum_ 为渲染目标                     │        │  │
│  │    │  - Shader: depth_peeling_blend.frag             │        │  │
│  │    │  - 输入: fboPeel_.color (当前层颜色)              │        │  │
│  │    │  - Blend: Front-to-Back 混合                    │        │  │
│  │    └────────────────────────────────────────────────┘        │  │
│  │                            │                                   │  │
│  │                            ▼                                   │  │
│  │    ┌────────────────────────────────────────────────┐        │  │
│  │    │ Step D: 交换深度索引 + 检查是否结束                │        │  │
│  │    │  - swap(inputDepthIndex, outputDepthIndex)     │        │  │
│  │    │  - if sampleCount == 0 → break                 │        │  │
│  │    └────────────────────────────────────────────────┘        │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                            │                                        │
│                            ▼                                        │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Phase 3: compositeToScreen()                                │  │
│  │  - 将 fboAccum_.color 混合背景色，输出到默认帧缓冲              │  │
│  │  - Shader: depth_peeling_final.frag                          │  │
│  └──────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 三个 FBO 的设计

| FBO | 颜色附件 | 深度附件 | 用途 |
|-----|---------|---------|------|
| `fboAccum_` | 累积颜色 | 用于乒乓的深度纹理 A | 存储已混合的颜色 + 提供剥离深度参考 |
| `fboPeel_` | 当前层颜色 | 用于乒乓的深度纹理 B | 渲染当前剥离层 + 提供剥离深度参考 |
| `fboOit_` | 透明物体初次渲染颜色 | 共享 fboAccum_.depth | 透明物体初次渲染（仅在初始化时使用） |

---

## 3. 关键 GL 状态与技巧

### 3.1 深度测试的巧妙运用

Depth Peeling 的核心就是**深度测试**，但每一阶段的使用方式不同：

| 阶段 | 深度测试 | 深度写入 | 深度清除值 | 作用 |
|------|---------|---------|-----------|------|
| initPeelBuffers | `GL_LESS` | `GL_TRUE` | `0.0` | 初始化深度为 0（最近），为第 1 层剥离做准备 |
| 剥离层渲染 | `GL_LESS` | `GL_TRUE` | `1.0` | 深度清除为 1（最远），用 LESS 保留最近片段 |
| 混合到累积 | 禁用 | `GL_FALSE` | N/A | 不关心深度，仅做颜色混合 |

**关键设计**：
- 每次剥离层渲染前，`glClearDepth(1.0)` 将深度清为最大值（最远）
- 渲染时使用 `GL_LESS`，只有深度值小于当前缓冲的片段才通过
- 因此每个像素上**最近的那个片段**会写入深度缓冲，成为该层的"代表"
- Shader 中再用 `if (gl_FragCoord.z <= frontDepth) discard` 排除上一层及更近的片段

### 3.2 Front-to-Back 混合

Depth Peeling 使用 **Front-to-Back**（从近到远）混合，而不是传统的 Back-to-Front：

```cpp
glEnable(GL_BLEND);
glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE,    // RGB 混合
                    GL_ZERO,                  // Alpha 混合
                    GL_ONE_MINUS_SRC_ALPHA);
```

使用 `glBlendFuncSeparate` 分别设置 RGB 和 Alpha 的混合因子：

**RGB 混合**：`GL_DST_ALPHA, GL_ONE`
```
C_result.rgb = C_src.rgb * DST_ALPHA + C_dst.rgb * ONE
             = C_src.rgb * A_dst + C_dst.rgb
```

**Alpha 混合**：`GL_ZERO, GL_ONE_MINUS_SRC_ALPHA`
```
C_result.a = C_src.a * ZERO + C_dst.a * ONE_MINUS_SRC_ALPHA
           = 0 + C_dst.a * (1 - A_src.a)
           = C_dst.a * (1 - A_src.a)
```

**整体效果**（Front-to-Back 下的标准混合公式）：
```
当 layer 1, 2, 3 依次混合时：
  A = 1 - (1-a1)*(1-a2)*(1-a3)  → 累积透明度
  C = c1*a1 + c2*a2*(1-a1) + c3*a3*(1-a1)*(1-a2) + ...
```

这种混合天然支持从近到远的顺序，不需要像 Linked List 那样先排序再混合。

### 3.3 GL_SAMPLES_PASSED 查询

Depth Peeling 不知道需要剥离多少层——这取决于场景中重叠的透明物体数量。使用 `GL_SAMPLES_PASSED` 查询可以**提前终止循环**：

```cpp
glBeginQuery(GL_SAMPLES_PASSED, queryId_);
drawSceneLayer(...);  // 渲染当前层
glEndQuery(GL_SAMPLES_PASSED);

GLuint sampleCount = waitSampleCount();  // 等待 GPU 完成查询
if (sampleCount == 0) {
    break;  // 没有片段通过 → 没有更多层 → 提前结束
}
```

`GL_SAMPLES_PASSED` 统计有多少个片段通过了深度测试。如果为 0，说明当前层没有任何片段（所有像素上都已没有更深的透明片段），可以提前终止循环。

**`waitSampleCount()` 的实现**——轮询等待 GPU 完成查询：
```cpp
GLuint waitSampleCount() {
    GLint available = 0;
    while (!available) {
        glGetQueryObjectiv(queryId_, GL_QUERY_RESULT_AVAILABLE, &available);
    }
    GLuint sampleCount = 0;
    glGetQueryObjectuiv(queryId_, GL_QUERY_RESULT, &sampleCount);
    return sampleCount;
}
```

**注意**：`glGetQueryObjectiv` 的轮询会阻塞 CPU，但在实际应用中通常是必要的——我们需要知道结果才能决定是否继续循环。更高级的实现可以使用双缓冲查询（一帧查询，下一帧使用结果），但会增加一帧延迟。

### 3.4 深度清除值的巧妙选择

| 时机 | 清除值 | 原因 |
|------|--------|------|
| initPeelBuffers | `glClearDepth(0.0)` | 初始化为最近深度，确保第 1 层剥离时 `GL_LESS` 能通过所有片段 |
| 每层剥离前 | `glClearDepth(1.0)` | 初始化为最远深度，让 `GL_LESS` 保留该像素上第一个（最近）通过的片段 |

---

## 4. 与 Linked List 方案对比

| 维度 | Depth Peeling | Linked List |
|------|--------------|-------------|
| **Pass 数** | O(N) — N 取决于最大重叠层数 | 固定 3 Pass |
| **显存** | 两个 FBO 的深度+颜色纹理 | ~230MB SSBO |
| **OpenGL 版本** | 3.3+ | 4.3+ (SSBO, Image, Atomic) |
| **精度** | 精确 | 精确 |
| **最坏情况** | 物体完全重叠时 Pass 数 = 物体数 | 固定 3 Pass |
| **优势** | 兼容性好，无显存爆炸风险 | Pass 数恒定，性能可预测 |
| **劣势** | Pass 数可变，重叠多时性能差 | 显存开销大，需要高级 GL 特性 |

---

## 5. 总结

Depth Peeling 的核心要点：

1. **逐层剥离**：利用上一层的深度纹理作为当前层的"近裁剪面"，逐层剥离出从近到远的透明片段
2. **双深度缓冲乒乓**：两个深度纹理交替使用，避免读-写冲突
3. **Front-to-Back 混合**：使用 `glBlendFuncSeparate` 实现从近到远的累积混合，无需排序
4. **GL_SAMPLES_PASSED 提前终止**：通过查询通过的片段数判断是否还有更多层

下篇将深入每个 Shader 的代码实现。