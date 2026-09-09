# OpenGL OIT 之 Linked List 实现（上篇）：原理、流程与缓冲区设计

> 源码地址：[GitHub 仓库](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_OIT_Linked_list)  
> **RenderDoc 问题记录：** [RenderDoc_问题记录.md](RenderDoc_问题记录.md)

## 0. 前言

在 OpenGL 渲染中，透明物体一直是一个棘手的问题。传统的 alpha 混合要求物体从远到近排序绘制，但实际场景中物体之间可能有穿插、包含关系，无法简单地按距离排序。更糟糕的是，随着视角旋转，物体的远近关系会动态变化，每帧都要重新排序——这在 CPU 侧代价高昂且容易出错。

**OIT（Order-Independent Transparency，与顺序无关的透明度）** 就是为了解决这个问题而生的。本文介绍 OIT 三种主流方案之一：**基于逐像素链表的 OIT（Linked List OIT）**。

---

## 1. 什么是 OIT？为什么需要它？

### 1.1 传统透明渲染的困境

在标准渲染管线中，透明物体的绘制顺序直接决定了最终颜色：

```
最终颜色 = 源颜色 * alpha + 目标颜色 * (1 - alpha)
```

这并不是一个**交换律**运算，所以 `A over B ≠ B over A`。如果先画近处的再画远处的，结果会出错：

```
错误顺序：近(含alpha) → 远(不透明) → 结果：近的透明物体遮挡了远的，但透明度计算错误
正确顺序：远(不透明) → 近(含alpha) → 结果：正确
```

CPU 侧排序的做法是：

1. 收集所有透明物体
2. 按距离排序
3. 从远到近逐个绘制

但这有几个致命问题：
- 物体间有穿插时无法排序
- 视角旋转后排序变化，需要每帧重新排序
- 排序本身有 O(n log n) 的开销

### 1.2 OIT 的核心思想

OIT 不要求 CPU 侧排序，而是将**排序的责任交给 GPU**。GPU 在渲染每个像素时，收集该像素上所有透明片段的颜色和深度，在 GPU 内部排序后再混合。

三种主流 OIT 方案：

| 方案 | 原理 | 优点 | 缺点 |
|------|------|------|------|
| **Depth Peeling** | 多次 Pass 逐层剥离 | 精确，兼容性好 | Pass 数多，性能较差 |
| **Linked List** | 逐像素链表存储所有片段 | 一次 Pass 收集，精确 | 显存开销大，需要原子操作 |
| **Stochastic** | 随机采样近似混合 | 单 Pass，性能好 | 非精确，有噪点 |

本文聚焦于 **Linked List 方案**——它用一次 Pass 收集所有透明片段到逐像素链表中，然后在一次全屏 Pass 中排序并混合，是一种精确且优雅的方案。

---

## 2. Linked List OIT 整体架构

### 2.1 三 Pass 渲染流程

Linked List OIT 将渲染分为三个阶段：

```
┌─────────────────────────────────────────────────────────────────┐
│                        Pass 1: 不透明物体                         │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────────┐ │
│  │ 不透明物体    │ ──→ │ Blinn-Phong  │ ──→ │ opaqueFBO        │ │
│  │ (spot cow)   │     │ 光照计算     │     │ (color + depth)  │ │
│  └──────────────┘     └──────────────┘     └──────────────────┘ │
│  深度写入: ON   深度测试: LESS                                      │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Pass 2: 透明物体收集                           │
│                                                                 │
│  ┌──────────────┐     ┌──────────────────────────────────────┐  │
│  │ 透明物体 ×3  │ ──→ │ Fragment Shader 中：                  │  │
│  │ (RGB quads)  │     │ 1. atomicCounterIncrement 获取节点ID  │  │
│  └──────────────┘     │ 2. imageAtomicExchange 头插法入链表   │  │
│                      │ 3. 写入 linkedListBuffer[nodeID]      │  │
│                      └──────────────────────────────────────┘  │
│                            │          │                         │
│                            ▼          ▼                         │
│                   ┌────────────┐  ┌──────────────────┐         │
│                   │ SSBO 链表   │  │ oitRenderFBO     │         │
│                   │ (每像素一条) │  │ (color attachment)│        │
│                   └────────────┘  └──────────────────┘         │
│  深度写入: OFF  深度测试: ON (共享 opaque 的 depth)              │
│  作用: 让被不透明物体遮挡的透明片段被正确丢弃                      │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Pass 3: 合成输出                              │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ Fullscreen Quad + compositeShader:                       │   │
│  │ 1. 遍历当前像素的链表                                      │   │
│  │ 2. 去重（相邻三角形边界同一深度）                           │   │
│  │ 3. 插入排序（从远到近）                                    │   │
│  │ 4. Back-to-Front over 混合                                │   │
│  └──────────────────────────────────────────────────────────┘   │
│                            │                                     │
│                            ▼                                     │
│                   ┌──────────────────┐                           │
│                   │ 默认帧缓冲 (屏幕) │                           │
│                   └──────────────────┘                           │
│  glMemoryBarrier: 确保 SSBO/Image/Atomic 写入完成后才读取         │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 逐像素链表的数据结构

每个像素（屏幕坐标 `(x, y)`）对应一条单链表，链表中的每个节点存储：

```
NodeType {
    vec4 color;   // 片段的 RGBA 颜色
    float depth;  // 片段的深度值（用于排序）
    uint  next;   // 链表中下一个节点的索引（0xFFFFFFFF 表示链表尾）
}
```

- **头指针**：存储在 `headPointers` (Image Texture) 中，每个像素一个 `uint32`，指向该像素链表的第一个节点
- **节点存储**：存储在 `linkedListBuffer` (SSBO) 中，所有像素共享一个大的节点池
- **节点分配**：通过 `atomicCounterIncrement` 原子操作分配唯一的节点索引

示意图：

```
像素 (300, 200) 的链表:

headPointers[300,200] = 5  ──→  Node[5]  ──→  Node[2]  ──→  Node[0]  ──→ END
                                   │              │              │
                                红色片段        绿色片段        蓝色片段
                                depth=0.3      depth=0.5      depth=0.7
                                next=2         next=0         next=0xFFFFFFFF
```

---

## 3. 四种特殊缓冲区详解

这是本项目的核心难点，涉及四个不常用的 OpenGL 缓冲区类型，这里逐一剖析。

### 3.1 Atomic Counter Buffer —— 原子计数器

```cpp
// 创建
glGenBuffers(1, &atomicBuffer_);
glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);
glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
```

**本质**：一个可以原子增加的 `uint32` 缓冲区。

**绑定点**：`GL_ATOMIC_COUNTER_BUFFER`，binding = 0

**作用**：为每个透明片段分配一个**全局唯一**的节点索引。在 Fragment Shader 中通过 `atomicCounterIncrement` 原子地获取当前计数值并自增 1。

**为什么需要原子操作？** 因为多个像素的 Fragment Shader 在 GPU 上并行执行，如果使用普通变量，多个线程可能读到相同的值（竞态）。原子操作保证：读取 → 返回旧值 → 写入新值 这三个步骤不可分割。

**每帧重置**：在 Pass 2 开始前，必须将计数器归零：
```cpp
GLuint zero = 0;
glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);
```

**Shader 侧使用**：
```glsl
layout(binding = 0, offset = 0) uniform atomic_uint nextNodeCounter;
// ...
uint nodeIndex = atomicCounterIncrement(nextNodeCounter);
```

### 3.2 SSBO (Shader Storage Buffer Object) —— 链表存储

```cpp
GLint nodeSize = 5 * sizeof(GLfloat) + sizeof(GLuint);  // vec4 + float + uint
glGenBuffers(1, &linkedListBuffer_);
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linkedListBuffer_);
glBufferData(GL_SHADER_STORAGE_BUFFER, maxNodes_ * nodeSize, nullptr, GL_DYNAMIC_DRAW);
```

**本质**：一块 GPU 可读写的大缓冲区，大小 = `maxNodes * sizeof(NodeType)`

**绑定点**：`GL_SHADER_STORAGE_BUFFER`，binding = 0

**与 UBO 的区别**：
- UBO 大小有限（通常 16KB-64KB），只读，适合小量 uniform 数据
- SSBO 大小可达 GB 级，可读写，适合大量结构化数据

**节点容量估算**：
```
maxNodes = width * height * 20 = 800 * 600 * 20 = 9,600,000 个节点
每个节点 = 24 bytes (vec4=16 + float=4 + uint=4)
总大小 ≈ 9,600,000 * 24 ≈ 230 MB
```

这是 Linked List OIT 的主要代价——显存开销大。

**Shader 侧使用**：
```glsl
layout(binding = 0, std430) buffer linkedLists {
    NodeType nodes[];
};
// 写入: nodes[nodeIndex].color = color;
// 读取: NodeType node = nodes[idx];
```

### 3.3 Image Texture —— 头指针纹理

```cpp
glGenTextures(1, &headPtrTexture_);
glBindTexture(GL_TEXTURE_2D, headPtrTexture_);
glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, width_, height_);
glBindImageTexture(0, headPtrTexture_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
```

**本质**：一个 `R32UI` 格式的 2D 纹理，每个像素存储一个 `uint32` 头指针。通过 `glBindImageTexture` 绑定，允许 Shader 中的 `imageLoad` / `imageStore` / `imageAtomicExchange` 直接读写。

**与普通纹理的区别**：

| | 普通纹理 (sampler) | Image 纹理 |
|---|---|---|
| 读取方式 | `texture(sampler, uv)` 带过滤 | `imageLoad(image, ivec2)` 精确像素 |
| 写入方式 | 只读（通过 FBO 颜色附件） | `imageStore(image, ivec2, data)` 直接写 |
| 原子操作 | 不支持 | 支持 `imageAtomicExchange` 等 |
| 用途 | 采样颜色 | 通用数据存储/计算 |

**`glBindImageTexture` 参数解析**：
```cpp
glBindImageTexture(
    0,            // unit: Image Unit 索引，对应 shader 中 binding = 0
    texture,      // 纹理对象
    0,            // level: mipmap 层级
    GL_FALSE,     // layered: 是否分层
    0,            // layer: 分层索引
    GL_READ_WRITE,// access: 读写权限
    GL_R32UI      // format: 内部格式
);
```

**关键操作：`imageAtomicExchange`**

在 Fragment Shader 中，这是链表插入的核心操作：
```glsl
uint preHead = imageAtomicExchange(headPointers, ivec2(gl_FragCoord.xy), newNodeIndex);
```

这行代码原子地完成了两个操作：
1. 读取 `headPointers[x][y]` 的旧值，赋给 `preHead`
2. 将 `newNodeIndex` 写入 `headPointers[x][y]`

然后设置 `nodes[newNodeIndex].next = preHead`，完成**头插法**。

**为什么需要原子操作？** 两个透明片段可能同时覆盖同一个像素，如果不用原子操作，两个线程可能同时读取旧头指针，然后各自写入自己的索引，导致其中一个丢失。

### 3.4 PBO (Pixel Unpack Buffer) —— 清空缓冲区

```cpp
std::vector<GLuint> headPtrClearBuf(width_ * height_, 0xffffffff);
glGenBuffers(1, &clearBuf_);
glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);
glBufferData(GL_PIXEL_UNPACK_BUFFER, headPtrClearBuf.size() * sizeof(GLuint),
             headPtrClearBuf.data(), GL_STATIC_COPY);
```

**本质**：一个 PBO，存储了 `width * height` 个 `0xFFFFFFFF`（即链表尾哨兵值）。

**为什么需要它？** 每帧 Pass 2 开始前，需要将所有像素的头指针重置为 `0xFFFFFFFF`（表示空链表）。直接使用 `glTexSubImage2D` 需要从 CPU 内存上传数据，而 PBO 允许**异步 DMA 传输**，数据已经在 GPU 内存中，比 CPU 上传快得多。

**使用方式**：
```cpp
// 绑定 PBO 到 GL_PIXEL_UNPACK_BUFFER
glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);
// 绑定头指针纹理
glBindTexture(GL_TEXTURE_2D, headPtrTexture_);
// texSubImage 的最后一个参数为 nullptr 表示从当前绑定的 PBO 读取数据
glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_,
                GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
```

当 `GL_PIXEL_UNPACK_BUFFER` 绑定了 PBO 时，`glTexSubImage2D` 的 `data` 参数（`nullptr`）表示从 PBO 的偏移量 0 处读取数据，而不是从 CPU 内存。

---

## 4. GL 状态设置与各 Pass 的作用

### 4.1 Pass 1: 不透明物体渲染

```cpp
glEnable(GL_DEPTH_TEST);   // 开启深度测试
glDepthFunc(GL_LESS);      // 深度值小于当前值的片段通过测试
glDepthMask(GL_TRUE);      // 允许写入深度缓冲
glDisable(GL_CULL_FACE);   // 关闭面剔除
```

| 状态 | 设置 | 作用 |
|------|------|------|
| `GL_DEPTH_TEST` | `GL_TRUE` | 启用深度测试，丢弃被遮挡的片段 |
| `glDepthFunc` | `GL_LESS` | 只有比已有深度更近的片段才通过，实现正确的遮挡关系 |
| `glDepthMask` | `GL_TRUE` | 允许向深度缓冲写入，记录不透明物体的精确深度 |
| `GL_CULL_FACE` | 禁用 | 渲染双面，确保模型完整显示 |

**输出**：`opaqueTexture`（颜色）+ `opaqueDepthTexture`（深度）

### 4.2 Pass 2: 透明物体收集

```cpp
glEnable(GL_DEPTH_TEST);   // 开启深度测试（丢弃被不透明物体完全遮挡的透明片段）
glDepthMask(GL_FALSE);     // 禁止写入深度缓冲（透明物体不遮挡彼此）
```

| 状态 | 设置 | 作用 |
|------|------|------|
| `GL_DEPTH_TEST` | `GL_TRUE` | 利用不透明物体的深度，丢弃被不透明物体遮挡的透明片段 |
| `glDepthMask` | `GL_FALSE` | **关键！** 透明物体不写入深度缓冲。如果写入，先画的透明物体可能遮挡后画的透明物体，导致后画的片段被错误丢弃 |

**注意**：Pass 2 的深度测试使用 Pass 1 写入的 `opaqueDepthTexture`（oitRenderFBO 的深度附件和 opaqueFBO 共享同一个深度纹理），但透明物体自身的深度不写入，确保所有未被遮挡的透明片段都能进入链表。

**Shader 中的深度比较**：Fragment Shader 还额外进行了一次采样比较：
```glsl
float depth = texture(texture_depth, uv).r;
if (gl_FragCoord.z > depth + 0.0001) {
    discard;
}
```
这是在 `gl_FragCoord.z` 基础上额外做的保护，确保被不透明物体遮挡的片段被丢弃（因为 `glDepthMask(GL_FALSE)` 意味着硬件的深度测试仍会执行，但 depth texture 是干净的，双重保险）。

### 4.3 Pass 3: 合成输出

```cpp
glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                GL_SHADER_STORAGE_BARRIER_BIT |
                GL_ATOMIC_COUNTER_BARRIER_BIT);

glEnable(GL_DEPTH_TEST);
glDepthMask(GL_TRUE);
```

**`glMemoryBarrier`** — 这是 Pass 2 到 Pass 3 之间**最关键的一步**。

GPU 是高度并行的，Pass 2 的 Fragment Shader 写入 SSBO 和 Image Texture 时，这些写入可能还在 GPU 的缓存中，尚未刷新到全局内存。`glMemoryBarrier` 强制所有之前的写入操作在下一次读取之前完成，否则 Pass 3 可能读到不完整或过时的数据。

三个 barrier bit 的含义：

| Barrier Bit | 保护的资源 |
|-------------|-----------|
| `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT` | Image Texture 的读写（head pointers） |
| `GL_SHADER_STORAGE_BARRIER_BIT` | SSBO 的读写（链表节点） |
| `GL_ATOMIC_COUNTER_BARRIER_BIT` | 原子计数器的读写 |

---

## 5. 总结

本文覆盖了 Linked List OIT 的三大核心：

1. **三 Pass 渲染流程**：不透明物体 → 透明片段收集到链表 → 排序混合输出
2. **四种特殊缓冲区**：Atomic Counter（分配节点ID）、SSBO（存储链表）、Image Texture（存储头指针）、PBO（快速清空）
3. **GL 状态管理**：深度测试与深度写入的开关控制各 Pass 的行为，Memory Barrier 保证 Pass 间的数据一致性

> 源码地址：[GitHub 仓库](https://github.com/user/OpenGL_OIT_Linked_list)（请替换为实际仓库地址）

下篇将深入 Shader 代码实现，逐行解析 `oitRender.frag` 和 `composite.frag` 的关键逻辑。