# OpenGL OIT 之 Linked List 实现

> 源码地址：[GitHub 仓库](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_OIT_Linked_list)  

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
- **节点分配**：通过 GLSL 原生内置函数 `atomicCounterIncrement` 原子操作在 SSBO 节点大数组中分配唯一的节点索引 `index`
- **无锁头插法**：通过 GLSL 原生内置函数 `imageAtomicExchange` 在不可分割的显存硬件周期内同步将 `index` 写入头指针纹理，并取出旧头指针作为 `next`，建立无锁链表链接

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

## 6. 缓冲区初始化（CPU 侧）

在进入渲染循环之前，需要初始化四个 OIT 专用缓冲区。下面逐一解析。

### 1.1 原子计数器缓冲区

```cpp
GLuint zero = 0;
GLint nodeSize = 5 * sizeof(GLfloat) + sizeof(GLuint);
// nodeSize = 5*4 + 4 = 24 bytes
// 对应 Shader 中的 NodeType { vec4 color; float depth; uint next; }

maxNodes_ = width_ * height_ * 20;  // 800 * 600 * 20 = 9,600,000

// 创建原子计数器缓冲区
glGenBuffers(1, &atomicBuffer_);
glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);
glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);
```

**关键点**：
- `glBindBufferBase` 而非 `glBindBuffer`：前者将 buffer 绑定到特定的 **indexed binding point**（binding = 0），这是 Shader 中 `layout(binding = 0)` 所要求的
- `GL_DYNAMIC_DRAW`：每帧都要通过 `glBufferSubData` 更新，所以使用动态标记
- 初始化为 0：`glBufferSubData` 写入 `zero`

### 1.2 链表存储缓冲区 (SSBO)

```cpp
glGenBuffers(1, &linkedListBuffer_);
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linkedListBuffer_);
glBufferData(GL_SHADER_STORAGE_BUFFER, maxNodes_ * nodeSize, nullptr, GL_DYNAMIC_DRAW);
```

**关键点**：
- `glBufferData` 最后一个参数 `nullptr`：只分配空间，不初始化数据。每帧 Shader 会覆盖写入，初始值无意义
- `std430` 布局：Shader 中 `layout(std430)` 保证内存布局紧凑，`vec4` 16 字节 + `float` 4 字节 + `uint` 4 字节 = 24 字节，无 padding

### 1.3 头指针 Image Texture

```cpp
glGenTextures(1, &headPtrTexture_);
glBindTexture(GL_TEXTURE_2D, headPtrTexture_);
glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, width_, height_);
glBindImageTexture(0, headPtrTexture_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
```

**关键点**：
- `glTexStorage2D` 而非 `glTexImage2D`：`glTexStorage2D` 分配不可变的纹理存储，是 OpenGL 4.2+ 的推荐方式
- `GL_R32UI`：每个像素一个 `uint32`，存储头指针索引
- `glBindImageTexture`：将纹理绑定到 Image Unit 0，Shader 中通过 `layout(binding = 0, r32ui) uniform uimage2D` 访问

### 1.4 清空 PBO

```cpp
std::vector<GLuint> headPtrClearBuf(width_ * height_, 0xffffffff);
glGenBuffers(1, &clearBuf_);
glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);
glBufferData(GL_PIXEL_UNPACK_BUFFER, headPtrClearBuf.size() * sizeof(GLuint),
             headPtrClearBuf.data(), GL_STATIC_COPY);
```

**关键点**：
- `0xFFFFFFFF`：这是链表尾哨兵值，在 Shader 中 `while(idx != 0xffffffff)` 判断链表是否结束
- `GL_STATIC_COPY`：数据在初始化时写入一次，之后只读。`COPY` 表示数据从 CPU 复制到 GPU，之后由 GPU 使用
- PBO 通过 `glTexSubImage2D(..., nullptr)` 使用，`nullptr` 表示数据源来自当前绑定的 `GL_PIXEL_UNPACK_BUFFER`

---

## 7. Pass 2 Shader：oitRender.frag 逐行解析

这是整个 OIT 系统最核心的 Shader，负责将每个透明片段插入到对应像素的链表中。

### 2.1 接口与数据结构

```glsl
#version 430 core
#define MAX_FRAGMENTS 75

layout (location = 0) out vec4 FragColor;

// 顶点着色器传入
in vec3 vertexPos;
in vec3 vertexNor;
in vec2 textureCoord;

// Uniforms
uniform vec3 cameraPos;
uniform vec3 lightPos;
uniform vec3 k;
uniform uint MaxNodes;

// 普通纹理
uniform sampler2D texture_diffuse;
uniform sampler2D texture_depth;  // 来自 Pass 1 的不透明物体深度纹理

// 链表节点结构体 — 必须与 CPU 侧的 ListNode 结构体对齐
struct NodeType {
    vec4 color;   // RGBA 颜色
    float depth;  // 深度值
    uint next;    // 下一节点索引
};

// ---- 三个特殊缓冲区的 Shader 声明 ----
layout(binding = 0, r32ui) uniform uimage2D headPointers;     // 头指针纹理
layout(binding = 0, offset = 0) uniform atomic_uint nextNodeCounter;  // 原子计数器
layout(binding = 0, std430) buffer linkedLists {               // 链表 SSBO
    NodeType nodes[];
};
```

**重要**：这三个 binding 虽然都是 `binding = 0`，但它们属于不同的 target 类型（`uimage2D` / `atomic_uint` / `std430 buffer`），互不冲突。这与 CPU 侧 `glBindBufferBase` 使用不同的 target 参数（`GL_ATOMIC_COUNTER_BUFFER` / `GL_SHADER_STORAGE_BUFFER`）和 `glBindImageTexture` 对应。

### 2.2 主逻辑

```glsl
void main() {
    // ==== 步骤 1: 深度遮挡剔除 ===
    // 将当前片段的屏幕坐标归一化到 [0,1] 范围
    vec2 uv = gl_FragCoord.xy / vec2(800, 600);

    // 从 Pass 1 的不透明深度纹理中采样
    float depth = texture(texture_depth, uv).r;

    // 如果当前片段的深度大于不透明物体的深度（即被遮挡），丢弃
    if (gl_FragCoord.z > depth + 0.0001) {
        discard;
    }

    // ==== 步骤 2: 原子分配节点索引 ===
    // atomicCounterIncrement 原子地返回旧值并自增 1
    // 返回值是当前片段可以使用的节点索引
    uint atomic_buffer = atomicCounterIncrement(nextNodeCounter);

    // ==== 步骤 3: 节点索引越界检查 ===
    if (atomic_buffer < MaxNodes) {

        // ==== 步骤 4: 头插法将新节点插入链表 ===
        // imageAtomicExchange 原子地：
        //   1. 读取 headPointers[x][y] 的旧值 → preHead
        //   2. 将 atomic_buffer 写入 headPointers[x][y]
        uint preHead = imageAtomicExchange(
            headPointers,
            ivec2(gl_FragCoord.xy),  // 像素坐标
            atomic_buffer             // 新头指针值
        );

        // 采样 diffuse 纹理获取片段颜色
        vec4 color = texture(texture_diffuse, textureCoord);

        // 写入链表节点
        nodes[atomic_buffer].color = color;
        nodes[atomic_buffer].depth = gl_FragCoord.z;
        nodes[atomic_buffer].next = preHead;  // 指向旧的头节点
    }

    // ==== 步骤 5: 输出到 oitRenderFBO ===
    // 这个输出本身不重要（颜色已经被写入链表），但 FBO 需要写入
    FragColor = texture(texture_diffuse, textureCoord);
}
```

### 2.2.1 GLSL 标准内置原子 API 全解析 (`atomicCounterIncrement` & `imageAtomicExchange`)

在 PPLL 链表构建的 Shader (`oitRender.frag`) 中，使用了两个极为关键的 **GLSL 标准内置原子函数**（OpenGL 4.2+ 原生内置支持，无需声明，由 GPU 显存控制器的硬件原子电路直接执行）：

| 内置 API 函数 | 原生签名 | 硬件级行为与核心职责 |
| :--- | :--- | :--- |
| **`atomicCounterIncrement`** | `uint atomicCounterIncrement(atomic_uint counter)` | **全局节点索引分配器**：GPU 显存控制器线程互斥地对全局计数器执行 `counter++`，并**返回自增前的整型旧值**。用于在 SSBO 节点大数组中为当前片元申请全局唯一的槽位 `index`。 |
| **`imageAtomicExchange`** | `uint imageAtomicExchange(gimage2D image, ivec2 P, uint data)` | **无锁头插法核心**：GPU 显存 Cache 级原子交换指令。在一个不可分割的原子周期内：<br>1. 将 `data` (即 `index`) 写入 `headPointers` 屏幕坐标 `P` 处；<br>2. **同时瞬间将该像素存储的旧头指针作为返回值 `preHead` 返回**。<br>从而无锁建立链表结构：`nodes[index].next = preHead`。 |

> 💡 **硬件意义**：在 GPU 成千上万个线程高度并发光栅化片元时，如果没有这两个 GPU 原生内置的硬件级原子 API，就无法保证链表构建的绝对线程安全与零锁并发性能。

### 2.3 头插法链表构建图解

假设像素 `(x, y)` 已经有 2 个节点，现在插入第 3 个：

```
插入前:
headPointers[x][y] = 5  ──→  Node[5]  ──→  Node[2]  ──→ END
                              (depth=0.7)    (depth=0.5)

插入 Node[8] (depth=0.3) 后:

1. atomicCounterIncrement 返回 8
2. imageAtomicExchange 返回 5（旧头指针）
3. 设置 Node[8]:
   nodes[8].color = 红色
   nodes[8].depth = 0.3
   nodes[8].next = 5   ← 指向旧头节点

headPointers[x][y] = 8  ──→  Node[8]  ──→  Node[5]  ──→  Node[2]  ──→ END
                              (depth=0.3)    (depth=0.7)    (depth=0.5)

注意：链表未排序，新节点总是在头部插入。排序在 Pass 3 的 composite.frag 中进行。
```

### 2.4 深度测试与深度遮罩协同原理解析 (`glEnable(GL_DEPTH_TEST)` + `glDepthMask(GL_FALSE)`)

在 Pass 2（透明片段收集 Pass）中，系统开启了硬件深度测试，但关闭了深度写入。`oitRenderFBO` 与 `opaqueFBO` **共享同一个深度附件 (`opaqueDepthTexture`)**：

1. **开启硬件深度测试 (`glEnable(GL_DEPTH_TEST)`) 的目的**：
   利用 Pass 1 不透明物体写入的真实 Z-Buffer 深度。如果某个透明面片的片元位于不透明物体**后面**（$z_{\text{trans}} \ge z_{\text{opaque}}$），会被 GPU 硬件 Z-Test **直接剔除**。
   - **收益**：避免对隐藏在墙壁/实体后面的无效透明片元分配 SSBO 节点和执行 Atomic 操作，大大节省显存带宽与原子计数开销。

2. **关闭深度写入 (`glDepthMask(GL_FALSE)`) 的目的**：
   禁止透明片元更新/污染已有的深度缓冲区！
   - **原因**：在透明物体渲染中，同一像素位置可能重叠着多层不同深度的透明面片（如红、绿、蓝三层玻璃）。如果允许透明片元写入深度，最靠前的那层玻璃就会更新 Z-Buffer，导致后面几层原本可见的透明面片被后续的 Z-Test 误杀丢弃。
   - **效果**：保持深度缓冲区**只读**，确保所有位于相机与不透明物体之间的半透明面片都能 100% 成功插入 PPLL 链表中，交由 Pass 3 统一排序混合。

---

## 8. Pass 3 Shader：composite.frag 逐行解析

全屏四边形 Shader，负责遍历当前像素的链表、排序、混合。

### 3.1 接口

```glsl
#version 430 core
#define MAX_FRAGMENTS 75

layout (location = 0) out vec4 FragColor;
in vec2 textureCoord;

uniform sampler2D texture_opaque;  // Pass 1 的不透明颜色纹理

// 与 oitRender.frag 相同的三个缓冲声明
struct NodeType { vec4 color; float depth; uint next; };
layout(binding = 0, r32ui) uniform uimage2D headPointers;
layout(binding = 0, offset = 0) uniform atomic_uint nextNodeCounter;
layout(binding = 0, std430) buffer linkedLists { NodeType nodes[]; };

const float EPSILON = 0.0001;
```

### 3.2 主逻辑

```glsl
void main() {

    // ==================== 步骤 1: 定义局部数组 ====================
    // 将链表中的数据复制到固定大小的数组中，方便排序
    NodeType frags[MAX_FRAGMENTS];  // 最多 75 个片段

    int count = 0;

    // ==================== 步骤 2: 遍历链表 ====================
    // 获取当前像素的头指针
    uint idx = imageLoad(headPointers, ivec2(gl_FragCoord.xy)).r;

    // 遍历链表，将节点复制到数组中
    while (idx != 0xffffffff && count < MAX_FRAGMENTS) {
        NodeType node = nodes[idx];

        // ---- 去重逻辑 ----
        // 问题：一个三角形面片的两个相邻三角形共享边，
        // 其边界上的片段会被光栅化两次，产生两个深度相同的片段
        // 解决：检查深度是否与已有片段相同，相同则跳过
        bool isDuplicate = false;
        for (int i = 0; i < count; i++) {
            if (abs(frags[i].depth - node.depth) < EPSILON) {
                isDuplicate = true;
                break;
            }
        }
        if (!isDuplicate) {
            frags[count] = node;
            count++;
        }

        idx = node.next;  // 移动到下一个节点
    }

    // ==================== 步骤 3: 插入排序（从远到近） ====================
    // 排序后：frags[0] 最深（最远），frags[count-1] 最浅（最近）
    for (int i = 1; i < count; i++) {
        int j = i;
        NodeType toInsertNode = frags[i];
        // 比较 depth：depth 越大越远 → 排在前面
        while (j > 0 && toInsertNode.depth > frags[j - 1].depth) {
            frags[j] = frags[j - 1];
            j--;
        }
        frags[j] = toInsertNode;
    }

    // ==================== 步骤 4: Back-to-Front Over 混合 ====================
    // 从远到近混合，使用标准的 over 运算
    vec4 color = texture(texture_opaque, textureCoord);  // 从背景颜色开始

    for (int i = 0; i < count; i++) {
        // over 运算：
        // C_result = C_src * A_src + C_dst * (1 - A_src)
        // A_result = A_src + A_dst * (1 - A_src)
        color.rgb = color.rgb * (1.0 - frags[i].color.a)
                  + frags[i].color.rgb * frags[i].color.a;
        color.a = color.a + frags[i].color.a * (1.0 - color.a);
    }

    FragColor = color;
}
```

### 3.3 排序方向说明

排序后的数组 `frags[]` 是**从远到近**排列的（`[0]` 最远，`[count-1]` 最近）：

```glsl
while (j > 0 && toInsertNode.depth > frags[j - 1].depth) {
    // depth 越大 → 越远 → 排在前面
```

这意味着 `frags[0]` 的 depth 最大（最远），先混合；`frags[count-1]` 的 depth 最小（最近），最后混合。这正是 Back-to-Front 混合的正确顺序。

### 3.4 Over 运算详解

Over 运算（Porter-Duff "A over B"）是透明混合的标准公式：

```
给定：
  C_src = 源颜色（当前要混合的片段颜色）
  A_src = 源 alpha
  C_dst = 目标颜色（已累计的颜色）
  A_dst = 目标 alpha

结果：
  C_result = C_src * A_src + C_dst * (1 - A_src)
  A_result = A_src + A_dst * (1 - A_src)
```

循环中，`color` 初始为不透明物体颜色（alpha=1.0），然后依次与每个透明片段混合。由于是 Back-to-Front 顺序，每个片段的背景就是它后面所有已混合的颜色。

### 3.5 去重逻辑的必要性

去重逻辑解决的是光栅化边界问题。考虑一个四边形由两个三角形组成：

```
    A ───── B
    │     ╱ │
    │   ╱   │
    │ ╱     │
    C ───── D
```

对角线 `B-C` 上的像素同时属于两个三角形，会被光栅化两次。如果不去重，链表中会有两个深度几乎相同的片段，混合时该边界会比周围更亮，产生可见的接缝。

通过比较 `abs(frags[i].depth - node.depth) < EPSILON`，可以识别并丢弃重复片段。

---

## 9. CPU 侧渲染 Pass 代码

### 4.1 Pass 1：不透明物体

```cpp
void LinkedListOITApp::renderOpaquePass() {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);       // 允许深度写入 — 关键！
    glDisable(GL_CULL_FACE);

    blinnPhongShader_->use();
    blinnPhongShader_->setVec3("cameraPos", cameraPos_);
    blinnPhongShader_->setVec3("lightPos", lightPos_);
    blinnPhongShader_->setVec3("k", k_);

    // 设置 MVP 矩阵
    glm::mat4 view = glm::lookAt(
        2.0f * glm::vec3(glm::sin(glm::radians(viewRotate_)),
                          0.0f,
                          glm::cos(glm::radians(viewRotate_))),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(...);

    blinnPhongShader_->setMat4("model", modelMatrix({0.0f, 0.0f, 0.0f}, 0.5f));
    blinnPhongShader_->setMat4("view", view);
    blinnPhongShader_->setMat4("projection", projection);

    // 绘制到 opaqueFBO（颜色 + 深度）
    spot_->Draw(*blinnPhongShader_, opaqueFBO_,
                {{"diffuse_texture", textureSpot_->id}}, {},
                GL_TRIANGLES, {true, true});
}
```

**输出**：`opaqueFBO_` 包含 `opaqueTexture_`（颜色附件）和 `opaqueDepthTexture_`（深度附件）

### 4.2 Pass 2：透明物体收集

```cpp
void LinkedListOITApp::renderTransparentPass() {
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);      // 禁止深度写入 — 关键！

    // ---- 重置 OIT 缓冲区 ----
    // 1. 原子计数器归零
    GLuint zero = 0;
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

    // 2. 重新绑定 SSBO
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linkedListBuffer_);

    // 3. 清空头指针纹理（全部设为 0xFFFFFFFF = 空链表）
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);
    glBindTexture(GL_TEXTURE_2D, headPtrTexture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_,
                    GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);

    // ---- 绘制三个透明物体 ----
    oitRenderShader_->use();
    oitRenderShader_->setUint("MaxNodes", maxNodes_);
    // ... 设置 view, projection ...

    // 红色透明方块
    glm::mat4 model = modelMatrix({-0.5f, 0.0f, 0.8f}, 0.5f);
    oitRenderShader_->setMat4("model", model);
    quad_->Draw(*oitRenderShader_, oitRenderFBO_,
                {{"diffuse_texture", textureWindowR_->id},
                 {"texture_depth", opaqueDepthTexture_}},  // 传入不透明深度纹理
                {}, GL_TRIANGLES, {true, false});

    // 绿色透明方块
    model = modelMatrix({0.2f, -0.5f, -1.0f}, 0.5f);
    oitRenderShader_->setMat4("model", model);
    quad_->Draw(*oitRenderShader_, oitRenderFBO_,
                {{"diffuse_texture", textureWindowG_->id},
                 {"texture_depth", opaqueDepthTexture_}},
                {}, GL_TRIANGLES, {false, false});

    // 蓝色透明方块
    model = modelMatrix({0.2f, 0.0f, -0.5f}, 0.5f);
    oitRenderShader_->setMat4("model", model);
    quad_->Draw(*oitRenderShader_, oitRenderFBO_,
                {{"diffuse_texture", textureWindowB_->id},
                 {"texture_depth", opaqueDepthTexture_}},
                {}, GL_TRIANGLES, {false, false});
}
```

**注意**：三个透明方块绘制时，`oitRenderFBO_` 的深度附件是 `opaqueDepthTexture_`（与 `opaqueFBO_` 共享），而 `glDepthMask(GL_FALSE)` 确保透明物体不会修改这个深度缓冲。

### 4.3 Pass 3：合成输出

```cpp
void LinkedListOITApp::renderCompositePass() {
    // 确保 GPU 完成了 Pass 2 的所有 SSBO/Image/Atomic 写入
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_SHADER_STORAGE_BARRIER_BIT |
                    GL_ATOMIC_COUNTER_BARRIER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    compositeShader_->use();
    // 传入不透明颜色纹理，作为混合的起始背景
    quad_->Draw(*compositeShader_, 0,  // framebuffer = 0 = 默认帧缓冲
                {{"texture_opaque", opaqueTexture_}}, {},
                GL_TRIANGLES, {true, true});
}
```

**`glMemoryBarrier` 的必要性**：Pass 2 中 Fragment Shader 写入 SSBO 和 Image Texture 的写入可能还在 GPU 缓存中，`glMemoryBarrier` 强制刷新，确保 Pass 3 读取到最新数据。没有这步会导致随机闪烁或丢失片段。

---

## 10. FBO 设计总结

本项目有两个 FBO，它们共享一个深度纹理：

```
┌─────────────────────────────────────────────────────┐
│  opaqueFBO                                          │
│  ┌─────────────────────┐  ┌───────────────────────┐ │
│  │ opaqueTexture       │  │ opaqueDepthTexture    │ │
│  │ RGBA16F (color)     │  │ DEPTH_COMPONENT32F    │ │
│  └─────────────────────┘  └───────────┬───────────┘ │
└───────────────────────────────────────┼─────────────┘
                                        │ 共享
┌───────────────────────────────────────┼─────────────┐
│  oitRenderFBO                         │             │
│  ┌─────────────────────┐             │             │
│  │ oitTexture          │  ◄──────────┘             │
│  │ RGBA16F (color)     │                            │
│  └─────────────────────┘                            │
└─────────────────────────────────────────────────────┘
```

设计意图：
- `opaqueFBO` 写入不透明物体的颜色和深度
- `oitRenderFBO` 写入透明物体的颜色，但**共享**不透明物体的深度纹理
- 这样 Pass 2 中透明物体的深度测试会比较不透明物体的深度，被遮挡的透明片段被正确丢弃
- 同时 `glDepthMask(GL_FALSE)` 确保透明物体不会污染共享的深度缓冲

---

## 11. 附：Blinn-Phong 光照 Shader

Pass 1 使用标准 Blinn-Phong 光照模型，计算代码在 `blinnPhong.frag` 中：

```glsl
#version 430 core
layout (location = 0) out vec4 FragColor;
in vec3 vertexPos;
in vec3 vertexNor;
in vec2 textureCoord;

uniform vec3 cameraPos;
uniform vec3 lightPos;
uniform vec3 k;  // k.x=环境光, k.y=漫反射, k.z=高光

void main() {
    vec3 lightColor = vec3(1.0);
    vec3 normalDir = normalize(vertexNor);
    vec3 lightDir = normalize(lightPos - vertexPos);
    vec3 viewDir = normalize(cameraPos - vertexPos);

    // 环境光
    vec3 ambient = k.x * lightColor;

    // 漫反射
    vec3 diffuse = k.y * max(dot(normalDir, lightDir), 0.0) * lightColor;

    // Blinn-Phong 高光
    vec3 halfwayDir = normalize(lightDir + viewDir);
    vec3 specular = k.z * pow(max(dot(normalDir, halfwayDir), 0.0), 2) * lightColor;

    vec3 objectColor = texture(texture_diffuse, textureCoord).xyz;
    FragColor = vec4((ambient + diffuse + specular) * objectColor, 1.0);
}
```

---

## 12. 总结

下篇逐行解析了 Linked List OIT 的核心 Shader 实现：

| 文件 | 核心功能 |
|------|---------|
| `oitRender.frag` | 头插法将透明片段插入逐像素链表，使用 `atomicCounterIncrement` 分配节点 ID，`imageAtomicExchange` 原子更新头指针 |
| `composite.frag` | 遍历链表 → 去重 → 插入排序 → Back-to-Front over 混合 |
| `blinnPhong.frag` | 标准 Blinn-Phong 光照计算不透明物体 |

以及 CPU 侧三个 Pass 的 GL 状态管理细节和 FBO 共享深度纹理的设计思路。

> 源码地址：[GitHub 仓库](https://github.com/user/OpenGL_OIT_Linked_list)（请替换为实际仓库地址）

## 13. 项目问答与复习笔记

### 13.1. 头指针纹理：`glTexStorage2D`

**代码位置：** `LinkedListOITApp.cpp` 第 179 行

```cpp
glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, width_, height_);
```

### 在做什么

为 **Linked List OIT** 分配 **头指针纹理（head pointer texture）** 的不可变存储。

| 参数 | 值 | 含义 |
|------|-----|------|
| `target` | `GL_TEXTURE_2D` | 2D 纹理 |
| `levels` | `1` | 仅 1 级 mipmap |
| `internalformat` | `GL_R32UI` | 单通道 32 位无符号整数 |
| `width` / `height` | `width_`, `height_` | 与窗口同分辨率 |

与 `glTexImage2D` 不同，`glTexStorage2D` **固定**纹理尺寸和格式，之后不可再改，但可用 `glTexSubImage2D` 写入数据。

### 在 OIT 中的作用

- 每个屏幕像素对应一个 `uint32`
- 存储该像素透明片元链表的 **头节点索引**（指向 `linkedListBuffer_`）
- 空链表哨兵值为 `0xffffffff`（见初始化 PBO 清空逻辑）

### 数据流（简化）

```
屏幕像素 (x, y)
    ↓
headPtrTexture_[x,y]  →  链表头 index（uint）
    ↓
linkedListBuffer_     →  节点 { color, depth, next }
    ↓
atomicBuffer_         →  全局节点计数器
```

---

### 13.2. Image 绑定：`glBindImageTexture`

**代码位置：** `LinkedListOITApp.cpp` 第 180 行

```cpp
glBindImageTexture(0, headPtrTexture_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
```

### 在做什么

将 `headPtrTexture_` 绑定到 **Image Unit 0**，使 shader 能以 **`uimage2D`** 方式随机读写，而非普通 `sampler2D` 采样。

### 与普通纹理的区别

| | `sampler2D` | `uimage2D` |
|---|---|---|
| 访问 | `texture(uv)`，可过滤 | `imageLoad(ivec2)`，按像素精确访问 |
| 写入 | 一般只读 | `imageStore` / `imageAtomic*` |
| 原子操作 | 不支持 | 支持 |
| 用途 | 贴图 | 数据结构（链表头指针） |

### 参数对应

| 参数 | 值 | 含义 |
|------|-----|------|
| unit | `0` | 对应 shader `layout(binding = 0)` |
| texture | `headPtrTexture_` | 头指针纹理 |
| level | `0` | mipmap 层级 |
| access | `GL_READ_WRITE` | shader 可读可写 |
| format | `GL_R32UI` | 按 R32UI 解释 |

### Shader 侧

```glsl
layout(binding = 0, r32ui) uniform uimage2D headPointers;
uint preHead = imageAtomicExchange(headPointers, ivec2(gl_FragCoord.xy), newIndex);
```

`binding = 0` 在 atomic counter、SSBO 上也会出现，但 **不同类型有独立 binding 空间**，互不冲突。

---

### 13.3. OIT FBO 初始化收尾（249–256 行）

**代码位置：** `LinkedListOITApp.cpp` `initFramebuffers()` 末尾

```cpp
GLenum drawBuffersOIT[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
glDrawBuffers(2, drawBuffersOIT);
if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ...
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
glBindFramebuffer(GL_FRAMEBUFFER, 0);
```

### 上下文

`oitRenderFBO_` 是 **Pass 2（透明物体）** 的渲染目标：

| 附件 | 纹理 | 作用 |
|------|------|------|
| `GL_COLOR_ATTACHMENT0` | `oitTexture_` | 颜色输出（RGBA16F） |
| `GL_DEPTH_ATTACHMENT` | `opaqueDepthTexture_` | **复用** Pass 1 深度，做深度测试 |

### 逐行说明

- **`glDrawBuffers`**：指定哪些 **颜色附件** 接收片元颜色。通常只需 `{GL_COLOR_ATTACHMENT0}`。深度写入由 `GL_DEPTH_ATTACHMENT` 绑定 + 深度测试完成，不应放进 `glDrawBuffers`（此处写法与 opaque FBO 类似，属常见复制粘贴问题，多数驱动仍能工作）。
- **`glCheckFramebufferStatus`**：检查 FBO 是否完整。
- **`glClear`**：清空 FBO 初始内容。
- **`glBindFramebuffer(0)`**：解绑，回到默认帧缓冲。

---

### 13.4. 光照：半透明物体与世界包围盒

### 半透明物体：**没有做光照**

CPU 侧在 `renderTransparentPass()` 传了 `lightPos`、`cameraPos`、`k`，但 `oitRender.frag` **未使用**：

```glsl
vec4 color = texture(texture_diffuse, textureCoord);
nodes[atomic_buffer].color = color;  // 直接存贴图色
```

Pass 3 `composite.frag` 也只做 alpha 混合，不重新打光。

| 物体 | Pass | 光照 |
|------|------|------|
| spot 牛（不透明） | Pass 1 | Blinn-Phong ✓ |
| 红/绿/蓝透明 quad | Pass 2 | 仅贴图 ✗ |
| 背景 | Pass 3 | 来自 opaqueTexture，非天空盒 |

### 世界包围盒 / 天空盒：**未渲染**

- 项目有 `SkyBox.hpp`，但 `LinkedListOITApp` **未使用**
- `quadShader_` 已加载但 **从未调用**
- 背景是不透明 FBO 内容 + 清屏色，不是环境贴图天空盒

> 本项目侧重 OIT 算法演示，Pass 2 简化了光照。若需透明物体受光，应在 `oitRender.frag` 写入链表前计算 Blinn-Phong，或在 `composite.frag` 混合时重新着色。

---

### 13.5. Memory Barrier：`GL_SHADER_IMAGE_ACCESS_BARRIER_BIT`

**代码位置：** `renderCompositePass()` Pass 3 开始前

```cpp
glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                GL_SHADER_STORAGE_BARRIER_BIT |
                GL_ATOMIC_COUNTER_BARRIER_BIT);
```

### 三个 bit 各对应什么

| Barrier bit | 内存类型 | 本项目对象 | Pass 2 写 | Pass 3 读 |
|-------------|----------|------------|-----------|-----------|
| `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT` | Image Texture | `headPtrTexture_` | `imageAtomicExchange` | `imageLoad` |
| `GL_SHADER_STORAGE_BARRIER_BIT` | SSBO | `linkedListBuffer_` | `nodes[i] = ...` | 遍历 `nodes[]` |
| `GL_ATOMIC_COUNTER_BARRIER_BIT` | Atomic Counter | `atomicBuffer_` | `atomicCounterIncrement` | （Pass 3 通常只读 SSBO/image） |

### 为什么需要

Pass 2 与 Pass 3 是不同 draw call，GPU 可能并行。不加 barrier，Pass 3 可能读到 Pass 2 尚未写完的数据，链表遍历会出错。

---

### 13.6. SSBO 是什么？还能用在哪里？

### 定义

**SSBO（Shader Storage Buffer Object）** 是 OpenGL 4.3+ 的 **GPU 可读写的通用大缓冲区**，绑定 `GL_SHADER_STORAGE_BUFFER`，GLSL 中用 `layout(std430) buffer` 声明。

本项目：

```cpp
glBufferData(GL_SHADER_STORAGE_BUFFER, maxNodes_ * nodeSize, ...);
```

```glsl
layout(binding = 0, std430) buffer linkedLists { NodeType nodes[]; };
```

### 与其他缓冲对比

| 类型 | 用途 | Shader | 大小 | 原子 |
|------|------|--------|------|------|
| UBO | 相机、光照参数 | 只读 | 较小 | ✗ |
| SSBO | 大量动态数据 | 读写 | 很大 | ✓ |
| Atomic Counter | 计数器 | 原子增减 | 很小 | ✓ |
| Image Texture | 按像素读写 | 2D 网格 | 纹理尺寸 | ✓ |

### 常见应用场景

- **OIT 链表节点**（本项目）
- Compute Shader：粒子、物理、图像处理
- 实例化：`mat4` 变换数组
- Clustered / Tiled Lighting 光源列表
- GPU 剔除、间接绘制命令缓冲
- GPU 哈希表、排序、BVH 等数据结构

### 注意事项

1. Pass 间读写需 `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)`
2. 并发写同一位置需原子操作，或像本项目用 counter 分配唯一 index
3. CPU `ListNode` 与 GLSL `NodeType` 必须 **内存对齐一致**（`std430`）

---

### 13.7. GLSL 原子函数是内置的吗？

**代码位置：** `oitRender.frag` 第 44、48 行

```glsl
uint atomic_buffer = atomicCounterIncrement(nextNodeCounter);
uint preHead = imageAtomicExchange(headPointers, ivec2(gl_FragCoord.xy), atomic_buffer);
```

### 结论

- **是 GLSL 语言内置函数**（规范定义，无需自己实现）
- **不是** OpenGL C API（不是 `glXxx`）
- 由 **GPU 硬件原子单元** 执行，驱动负责编译

### 分层理解

```
oitRender.frag 调用 GLSL 内置函数
    → 驱动/编译器
    → GPU 原子指令
    → 显存中的 buffer / texture
```

### CPU 侧前置条件

| Shader | CPU 绑定 |
|--------|----------|
| `atomic_uint nextNodeCounter` | `glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_)` |
| `uimage2D headPointers` | `glBindImageTexture(0, headPtrTexture_, ...)` |

### 版本要求

- `atomicCounterIncrement`：OpenGL **4.2+**
- `imageAtomicExchange`：OpenGL **4.2+**
- 本项目 `#version 430 core`，完全支持

---

### 13.8. SSBO 节点写入：需要 new Node 吗？

**代码位置：** `oitRender.frag` 第 53–55 行

```glsl
nodes[atomic_buffer].color = color;
nodes[atomic_buffer].depth = gl_FragCoord.z;
nodes[atomic_buffer].next = preHead;
```

### 不需要 `new Node()`，也没有空指针

GPU 侧是 **预分配数组 + 整数索引**，不是 CPU 堆上的指针链表。

### 初始化（对象池）

```cpp
maxNodes_ = width_ * height_ * 20;
glBufferData(GL_SHADER_STORAGE_BUFFER, maxNodes_ * nodeSize, ...);
```

等价于：

```cpp
Node nodes[maxNodes_];  // 整块内存，槽位已全部存在
```

### 分配流程

| 步骤 | 机制 | 说明 |
|------|------|------|
| 预分配 | `glBufferData` | 创建 `maxNodes_` 个槽位 |
| 领编号 | `atomicCounterIncrement` | 返回 0, 1, 2, … 唯一 index |
| 越界保护 | `if (atomic_buffer < MaxNodes)` | 池满则丢弃 |
| 写数据 | `nodes[atomic_buffer] = ...` | 填第 N 个槽 |
| 串链表 | `next = preHead` | **uint 索引**，非指针 |

### 为何不怕空指针

1. GLSL **没有指针**；`next` 是 `uint`，链表结束用 `0xffffffff`
2. `atomic_buffer < MaxNodes` 保证不越界
3. 每个片元通过 counter 拿到 **不同 index**，写冲突少
4. 头指针在 image texture 中，用 `imageAtomicExchange` 原子更新

### 真正需要担心的

| 风险 | 处理 |
|------|------|
| 节点池满 | `atomic_buffer < MaxNodes` |
| Pass 间不同步 | `glMemoryBarrier` |
| struct 对齐 | CPU/GPU 结构体一致 |
| 同像素并发 | `imageAtomicExchange` |

---

### 13.9. 附录：三 Pass 与缓冲对照

```
Pass 1  opaqueFBO          Blinn-Phong → opaqueTexture + opaqueDepthTexture
Pass 2  oitRenderFBO        oitRender.frag → SSBO 链表 + headPtrTexture + atomicBuffer
Pass 3  默认帧缓冲         composite.frag → 读链表排序混合 + opaqueTexture
```

| 缓冲 | 类型 | Shader 变量 | 主要操作 |
|------|------|-------------|----------|
| `atomicBuffer_` | Atomic Counter | `atomic_uint nextNodeCounter` | Pass 2 递增分配 index |
| `linkedListBuffer_` | SSBO | `nodes[]` | Pass 2 写，Pass 3 读 |
| `headPtrTexture_` | Image (R32UI) | `uimage2D headPointers` | Pass 2 原子交换，Pass 3 imageLoad |
| `opaqueDepthTexture_` | 深度纹理 | `sampler2D texture_depth` | Pass 2 深度遮挡测试 |

---



## 14. 附录：RenderDoc 问题记录

> Linked List OIT 实现与联调过程中遇到的问题及 RenderDoc 排查路径。

帧结构：**Opaque → Transparent（写链表）→ Composite**。Atomic / SSBO 相关问题常在 **第二帧** 才暴露，排查时建议至少 Capture 两帧。

**RenderDoc 顺序：** Opaque FBO 是否正常 → Transparent 前 atomic/head 是否清零 → Transparent 的 DepthMask → Composite 前是否有 MemoryBarrier → Buffer Viewer 看链表节点。

---

## OIT-LL-15 · Atomic counter 帧间未清零

**现象**  
首帧透明合成正常，**从第二帧起** 透明区域闪烁、随机色块，偶发链表节点写越界。

**原因**  
Transparent Pass 前漏掉 `glBufferSubData` 把 atomic counter 写回 0，上一帧的 counter 值被沿用，新帧从错误 offset 分配节点。

**定位过程**  
1. 分别 Capture 第 1、2 帧。  
2. 第 2 帧 Transparent Pass 开始前看 **Atomic Counter** 初值：应为 0，异常时 ≠ 0。  
3. 对比两帧 Transparent 写入的 SSBO 起始区域。

**处理**  
每帧 Transparent 前对 atomic buffer 写 0；与 head pointer clear 放在同一帧初逻辑。

**涉及文件**  
`src/LinkedListOITApp.cpp`（约 319 行）

---

## OIT-LL-16 · Transparent 与 Composite 之间缺少 barrier

**现象**  
Composite 读到的链表不完整或随机变化，同一帧内 Transparent 已写入但 Composite 像读到旧数据。

**原因**  
去掉或漏写 `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)`，SSBO 写入对 Composite 的 read 不可见。

**定位过程**  
Event Browser：Composite Draw 前是否有 **MemoryBarrier** event；Composite 时 Buffer Viewer 与 Transparent 最后一个 Draw 后的 SSBO 是否一致。

**处理**  
Transparent Pass 结束后、Composite 前恢复 barrier。

**涉及文件**  
`src/LinkedListOITApp.cpp`（约 372～373 行）

---

## OIT-LL-17 · 透明 Pass 误开 depth write

**现象**  
透明 quad 叠加顺序错误，部分区域被「透明物挡住」，与预期 alpha 混合不符。

**原因**  
Transparent Pass 使用 `glDepthMask(GL_TRUE)`，透明片元改写了 depth，后续片元 depth test 行为被破坏。

**定位过程**  
Transparent Draw → Pipeline → **Depth Mask: Write Enabled**（OIT 透明 Pass 应为 false）；检查 opaque depth 纹理是否被意外修改。

**处理**  
透明 Pass 保持 `glDepthMask(GL_FALSE)`，depth test 可读 opaque depth 但不写入。

**涉及文件**  
`src/LinkedListOITApp.cpp`（约 314 行）

---

## 同类问题速查

| 现象 | 优先看 |
|------|--------|
| 第二帧才坏 | Atomic counter 帧初清零 |
| Composite 随机 | MemoryBarrier；SSBO 同步 |
| 透明 depth 乱 | Transparent DepthMask |



## 15. 附录：通用管线与状态机机制（摘自通用知识点汇总）

### glBindBufferBase vs glBindFramebuffer`n
| 维度 | glBindBufferBase | glBindFramebuffer |
| :--- | :--- | :--- |
| **绑定对象** | 数据缓冲区（Buffer Object，如 SSBO / UBO） | 帧缓冲区对象（Framebuffer Object, FBO） |
| **核心作用** | 将通用数据 Block 关联到 Shader 的**索引绑定点（Indexed Binding Point）**，供 Shader 读写数据 | 指定 Shader 渲染输出的**目标（Render Target）**，决定像素画到哪里 |
| **对应 GLSL 声明** | layout(std430, binding = N) buffer BlockName { ... }; | layout(location = N) out vec4 FragColor; |
| **典型使用场景** | 传递点光源数组、相机/视图矩阵块、实例数据等 | 切换到屏幕 ( ) 或 G-Buffer / Shadow Map 离屏 FBO |

> **关键结论**：
> - glBindBufferBase 解决的是 **数据输入/输出管道** 的映射；
> - glBindFramebuffer 解决的是 **像素绘制目标（屏幕或纹理附件）** 的切换。

### SSBO 内存对齐规则 (std430 / std140)
- **std140**（常用于 UBO）：数组元素强制按 ec4 (16 字节) 对齐。
- **std430**（常用于 SSBO）：数组元素更紧凑，例如结构体包含 ec3 position 和 loat radius，恰好占 16 字节，内存分布与 CPU 端紧凑结构体一致。

