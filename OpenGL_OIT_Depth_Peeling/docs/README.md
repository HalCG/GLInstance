# OpenGL OIT 之 Depth Peeling 实现

> 源码地址：[GitHub 仓库](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_OIT_Depth_Peeling)  

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

### 3.5 Depth Peeling 核心 FAQ 与 OpenGL 状态机总结

#### Q1. Peel Pass 与 Blend Pass 之间，OpenGL 全局状态机是如何切换的？

在 [DepthPeelingApp.cpp](file:///i:/opengl/liuhaonian/OpenGL_OIT_Depth_Peeling/src/DepthPeelingApp.cpp#L273-L310) 中，每一层迭代包含两个 Pass，全局状态机在两者之间交替切换：

| 渲染 Pass | 目标 FBO | 深度测试 `GL_DEPTH_TEST` | 深度写入 `glDepthMask` | 颜色混合 `GL_BLEND` | 核心操作 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **A. Peel Pass** | `fboPeel_.fbo` | **开启** (`glEnable`) | **开启** (`GL_TRUE`) | **关闭** (`glDisable`) | 采样上一层深度纹理，`discard` 掉 $\le \text{frontDepth}$ 的片元，剥离出当前层最近深度。 |
| **B. Blend Pass** | `fboAccum_.fbo` | **关闭** (`glDisable`) | **关闭** (`GL_FALSE`) | **开启** (`glEnable`) | 使用 `glBlendFuncSeparate` 将当前层颜色混合入累积 FBO，不干扰深度。 |

#### Q2. 为什么 Depth Peeling 算法具有极强的硬件兼容性？

相较于基于 SSBO 和 Atomic Counter 的 Linked List OIT 方案，Depth Peeling 纯粹依赖 **标准 Framebuffer (FBO)、2D 深度纹理与硬件深度测试 `GL_LESS`**。它不需要任何 OpenGL 4.3+ 的高级特性，即使在十年前老旧的移动设备或 OpenGL 3.3 硬件上也能稳定运行！

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

## 6. 核心 Shader 实现

### 1.1 depth_peeling_render.frag —— 剥离层着色器

这是整个 Depth Peeling 最核心的 Shader，负责渲染当前剥离层：

```glsl
#version 430 core
layout(location = 0) out vec4 FragColor;

in vec3 vertexPos;
in vec3 vertexNor;
in vec2 textureCoord;

uniform vec3 cameraPos;
uniform vec3 lightPos;
uniform vec3 k;    // x=环境光, y=漫反射, z=高光

uniform sampler2D texture_diffuse;   // 物体颜色纹理
uniform sampler2D texture_depth;     // 上一层的深度纹理
uniform vec2 u_ScreenSize;           // 屏幕尺寸（用于归一化 gl_FragCoord）

void main() {
    // ==== 步骤 1: 归一化屏幕坐标 ====
    // gl_FragCoord.xy 是像素坐标，uv 归一化到 [0, 1]
    vec2 uv = gl_FragCoord.xy / u_ScreenSize;

    // ==== 步骤 2: 采样上一层的深度 ====
    // 从上一层的深度纹理中读取该像素的深度值
    float frontDepth = texture(texture_depth, uv).r;

    // ==== 步骤 3: 深度裁剪 —— 这是"剥离"的核心 ====
    // 如果当前片段的深度 <= 上一层的深度（即不比上一层更远），丢弃
    // 注意：这里用的是 <= 而不是 <，因为 <= 表示"当前片段在上一层的前面或同一位置"
    // 只有 gl_FragCoord.z > frontDepth 的片段才保留（即比上一层更远的片段）
    if (gl_FragCoord.z <= frontDepth) {
        discard;
    }

    // ==== 步骤 4: Blinn-Phong 光照计算 ====
    vec3 lightColor = vec3(1.0);

    float ambientStrength = k.x;
    vec3 ambient = ambientStrength * lightColor;

    float diffuseStrength = k.y;
    vec3 normalDir = normalize(vertexNor);
    vec3 lightDir = normalize(lightPos - vertexPos);
    vec3 diffuse =
        diffuseStrength * max(dot(normalDir, lightDir), 0.0) * lightColor;

    float specularStrength = k.z;
    vec3 viewDir = normalize(cameraPos - vertexPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    vec3 specular = specularStrength *
                    pow(max(dot(normalDir, halfwayDir), 0.0), 2.0) * lightColor;

    // ==== 步骤 5: 采样纹理颜色和透明度 ====
    vec3 objectColor = vec3(0.8);
    float alpha = 0.0;
    if (textureCoord.x >= 0.0 && textureCoord.y >= 0.0) {
        vec4 sampled = texture(texture_diffuse, textureCoord);
        objectColor = sampled.rgb;
        alpha = sampled.a;
    }

    // 输出带透明度的光照颜色
    FragColor = vec4((ambient + diffuse + specular) * objectColor, alpha);
}
```

**深度裁剪逻辑详解**：

```
假设当前像素有 3 个透明片段重叠:

            Camera
              ▼
    ┌─────────────────────────────────┐
    │  Fragment A: depth = 0.3       │  ← 最近
    │  Fragment B: depth = 0.5       │
    │  Fragment C: depth = 0.7       │  ← 最远
    └─────────────────────────────────┘

Layer 1 剥离:
  frontDepth = 0.0 (初始深度缓冲值)
  条件: gl_FragCoord.z <= 0.0 ?  → 都不满足
  结果: A, B, C 都通过 → 硬件深度测试 GL_LESS 保留 A (depth=0.3)
  剥离出 A

Layer 2 剥离:
  frontDepth = 0.3 (Layer 1 的 A 深度)
  条件: gl_FragCoord.z <= 0.3 ?
    A: 0.3 <= 0.3 → discard ✓
    B: 0.5 <= 0.3 → 不满足，通过 → 硬件深度测试保留 B (depth=0.5)
    C: 0.7 <= 0.3 → 不满足，通过 → 硬件深度测试被 B 遮挡
  剥离出 B

Layer 3 剥离:
  frontDepth = 0.5 (Layer 2 的 B 深度)
  条件: gl_FragCoord.z <= 0.5 ?
    A: 0.3 <= 0.5 → discard ✓
    B: 0.5 <= 0.5 → discard ✓
    C: 0.7 <= 0.5 → 不满足，通过 → 硬件深度测试保留 C (depth=0.7)
  剥离出 C

Layer 4:
  frontDepth = 0.7
  所有片段都被 discard → sampleCount = 0 → break
```

### 1.2 depth_peeling_blend.frag —— 混合着色器

这是一个极简的全屏四边形 Shader，只做颜色传递：

```glsl
#version 330 core
out vec4 FragColor;
in vec2 textureCoord;
uniform sampler2D texture_diffuse;  // 当前剥离层的颜色纹理

void main() {
    FragColor = texture(texture_diffuse, textureCoord);
}
```

**为什么这么简单？** 因为混合逻辑完全由 `glBlendFuncSeparate` 处理——Shader 只需要输出颜色，OpenGL 的混合管线自动完成 Front-to-Back 累积。

### 1.3 depth_peeling_final.frag —— 最终合成着色器

将累积的颜色与背景色混合，输出到屏幕：

```glsl
#version 430 core
layout(location = 0) out vec4 FragColor;
in vec2 textureCoord;

uniform vec3 background_color;
uniform sampler2D texture_diffuse;  // fboAccum_.color（累积颜色）

void main() {
    vec4 frontColor = texture(texture_diffuse, textureCoord);

    // 将累积颜色与背景色混合
    // frontColor.alpha 表示"剩余透明度"（即 1 - 累积不透明度）
    // 背景色按剩余透明度混合
    FragColor = frontColor + vec4(background_color, 1.0) * frontColor.a;
    FragColor.a = 1.0;  // 最终输出不透明
}
```

**混合逻辑**：
- `frontColor` 是累积缓冲区中已经混合好的颜色
- `frontColor.a` 是"剩余透明度"：在 Front-to-Back 混合中，alpha 会逐渐减小，`1 - alpha` 表示已累积的不透明度
- 背景色乘以剩余透明度，加到累积颜色上，得到最终结果

---

## 7. CPU 侧关键代码

### 2.1 初始化：FBO 和深度缓冲

```cpp
bool DepthPeelingApp::initFramebuffers() {
    const int w = static_cast<int>(width_);
    const int h = static_cast<int>(height_);

    // 创建两个乒乓 FBO（颜色 + 深度）
    fboAccum_.create(w, h, "Accumulation (FBO_0)");
    fboPeel_.create(w, h, "Peel layer (FBO_1)");

    // 创建 oitRenderFBO — 与 fboAccum_ 共享深度纹理
    texOitColor_.createColorHDR(w, h);
    glGenFramebuffers(1, &fboOit_);
    glBindFramebuffer(GL_FRAMEBUFFER, fboOit_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texOitColor_.id, 0);
    // 关键：oitRenderFBO 的深度附件 = fboAccum_ 的深度纹理
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           fboAccum_.depth.id, 0);
    // ...
}
```

**FBO 深度纹理共享**：`fboOit_` 和 `fboAccum_` 共享同一个深度纹理，这样透明物体渲染时可以使用不透明物体的深度进行遮挡测试。

### 2.2 GlFramebuffer 辅助类

```cpp
class GlFramebuffer {
public:
    GLuint fbo = 0;
    GlTexture2D color;   // RGBA16F 颜色纹理
    GlTexture2D depth;   // DEPTH_COMPONENT32F 深度纹理

    void create(int width, int height, const char *debugName) {
        destroy();
        color.createColorHDR(width, height);
        depth.createDepth32F(width, height);

        glGenFramebuffers(1, &fbo);
        bindColorDepth(color.id, depth.id);
    }

    void bindColorDepth(GLuint colorTex, GLuint depthTex) const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, colorTex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, depthTex, 0);
        // ...
    }
};

class GlTexture2D {
public:
    GLuint id = 0;

    void createColorHDR(int width, int height) {
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
                     GL_RGBA, GL_HALF_FLOAT, nullptr);
        // ... 设置 wrap/filter 参数
    }

    void createDepth32F(int width, int height) {
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        // ...
    }
};
```

**关键设计**：`bindColorDepth` 允许动态切换深度附件，这是乒乓机制的基础——在剥离循环中，每层都会将不同的深度纹理绑定到 `fboPeel_` 的深度附件上。

### 2.3 剥离前的初始化

```cpp
void DepthPeelingApp::initPeelBuffers() {
    // 清空 FBO_0（累积缓冲）：颜色(0,0,0,1)，深度 0.0（最近）
    fboAccum_.bindColorDepth(fboAccum_.color.id, fboAccum_.depth.id);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(0.0f);   // ← 关键：深度清除为 0.0（最近）
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 清空 FBO_1（剥离层）：颜色(0,0,0,0)，深度 0.0
    fboPeel_.bindColorDepth(fboPeel_.color.id, fboPeel_.depth.id);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 全局深度状态
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);     // 深度值小于当前值的片段通过
    glDepthMask(GL_TRUE);     // 允许深度写入

    // 初始化乒乓索引
    inputDepthIndex_ = 0;   // fboAccum_.depth
    outputDepthIndex_ = 1;  // fboPeel_.depth
}
```

**为什么初始深度清除为 0.0？** 因为第 1 层剥离时，Shader 中的条件是 `gl_FragCoord.z <= frontDepth`，`frontDepth = 0.0`（初始值）。OpenGL 的深度范围是 [0, 1]，0 表示最近。任何片段的 `gl_FragCoord.z` 都 >= 0，所以第 1 层所有片段都会通过裁剪，由硬件深度测试 `GL_LESS` 选出每个像素上最近的那个片段。

### 2.4 剥离循环：核心实现

```cpp
void DepthPeelingApp::peelAndBlend() {
    for (int layer = 0; layer < AppConfig::kMaxDepthPeelLayers; ++layer) {

        // ===== Step A: 准备当前层的渲染目标 =====
        glBindFramebuffer(GL_FRAMEBUFFER, fboPeel_.fbo);
        // 动态绑定深度附件：使用 outputDepthIndex 对应的深度纹理
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D,
                               depthTexture(outputDepthIndex_), 0);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);  // 透明背景
        glClearDepth(1.0f);                     // 深度清除为最远
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ===== Step B: 渲染场景 → 剥离一层 =====
        glBeginQuery(GL_SAMPLES_PASSED, queryId_);
        drawSceneLayer(*shaderPeel_, fboPeel_.fbo,
                       inputDepthIndex_, outputDepthIndex_);
        glEndQuery(GL_SAMPLES_PASSED);

        const GLuint sampleCount = waitSampleCount();

        // ===== Step C: 将剥离层混合到累积缓冲 =====
        shaderBlend_->use();
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);     // 混合时不写深度
        glDisable(GL_DEPTH_TEST);  // 混合时不测试深度
        glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE,          // RGB
                            GL_ZERO, GL_ONE_MINUS_SRC_ALPHA); // Alpha

        // 全屏四边形：将 fboPeel_.color 混合到 fboAccum_.fbo
        modelQuad_->Draw(*shaderBlend_, fboAccum_.fbo,
                        {{"texture_diffuse", fboPeel_.color.id}}, {},
                        GL_TRIANGLES, {false, true});

        // 恢复状态
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);

        // ===== Step D: 交换深度索引 =====
        inputDepthIndex_ = (inputDepthIndex_ + 1) % 2;
        outputDepthIndex_ = (outputDepthIndex_ + 1) % 2;

        // ===== Step E: 提前终止检查 =====
        if (sampleCount <= 0) {
            break;  // 没有片段通过 → 没有更多层
        }
    }
}
```

### 2.4.1 Front-to-Back 自动实体遮挡原理 (Alpha 归零推导)

在 Front-to-Back 混合中，**不透明实体物体的遮挡是靠 Alpha 累积算式自动成立的**：
1. **源 (Source, `src`)**：当前刚剥离出来的这一层片元 (`fboPeel_.color`)。
2. **目标 (Destination, `dst`)**：FBO 中前面几轮已混合的旧颜色 (`fboAccum_.color`)。
3. **推导逻辑**：
   - 假设当前剥离出一层不透明实体（Alpha = 1.0），其透光率计算为：
     $$A_{\text{final}} = A_{\text{dst}} \cdot (1 - A_{\text{src}}) = A_{\text{dst}} \cdot (1 - 1.0) = 0.0$$
   - 这一步将累积透光率 $A_{\text{dst}}$ **直接扣减归零** ($A_{\text{dst}} = 0$)。
   - 此后后续 Pass 剥离出的任何背部物体（无论透明还是实体），其颜色在加入累积 Buffer 时：
     $$C_{\text{final}} = C_{\text{src\_next}} \cdot A_{\text{dst}} + C_{\text{accum}} = C_{\text{src\_next}} \cdot 0.0 + C_{\text{accum}} = C_{\text{accum}}$$
   - **结论：乘数因子变成了 0，后续剥离出的所有层颜色全部乘以 0 彻底无效（被自动遮挡）**，无需任何手动剔除逻辑。

### 2.4.2 遮挡查询 `waitSampleCount()` 阻塞同步机制

```cpp
GLuint DepthPeelingApp::waitSampleCount() {
  GLint available = 0;
  while (!available) {
    glGetQueryObjectiv(queryId_, GL_QUERY_RESULT_AVAILABLE, &available);
  }
  GLuint sampleCount = 0;
  glGetQueryObjectuiv(queryId_, GL_QUERY_RESULT, &sampleCount);
  return sampleCount;
}
```

- **为何确定查询的是采样点数量？**
  因为在调用 `waitSampleCount()` 前，程序显式执行了 `glBeginQuery(GL_SAMPLES_PASSED, queryId_)`，将 `queryId_` 的统计目标指定为了 `GL_SAMPLES_PASSED`（通过 Z-Test 的片元采样数）。
- **为什么要 `while(!available)` 阻塞等待？**
  GPU 是异步渲染管线，Query 数据写回内存需要耗时。阻塞等待可确保拿到的 `sampleCount` 100% 准确。
  - **如果彻底去掉 Occlusion Query**：画面依然完全正确，但剥离循环必须死板跑满上限 $N$ 次 Pass（白白浪费性能）。
  - **如果非阻塞读取脏数据**：会导致 `sampleCount` 误判为 0 而提前退出 `break`，产生半透明物体丢失、空洞或剧烈闪烁瑕疵。

### 2.4.3 深度写入职责辨析 (Fragment Shader 软逻辑 vs GPU 硬件固定管线全流程)

在 Depth Peeling 中，剥离层深度写入 `depthTexture(outputDepthIndex_)` 的过程涉及 **软件 Shader 代码** 与 **GPU 硬件固定管线 (ROP 单元)** 的协同运作：

#### 1. 职责分工对照表

| 阶段 | 负责主体 | 具体职责 |
| :--- | :--- | :--- |
| **第一关：软件筛选** | **Fragment Shader (`depth_peeling_render.frag`)** | 仅执行 `if (gl_FragCoord.z <= frontDepth) discard;` 判断（丢弃上一层及更靠前的旧片元），计算颜色 `FragColor`。**Shader 本身并没有手动改写 `gl_FragDepth`**。 |
| **第二关：硬件测试与写入** | **GPU 硬件固定管线 (ROP 单元 / Output Merger)** | 片元离开 Frag Shader 后进入芯片底层的专用电路。硬件检查 `glDepthMask(GL_TRUE)` 权限，与当前 Depth 缓冲区旧值做 `GL_LESS` 测试，**比对通过后自动将原生插值深度 `gl_FragCoord.z` 写入 FBO 深度附件**。 |

#### 2. GPU 硬件固定管线后光栅化 6 步处理流程

当片元顺利离开 Frag Shader 的 `main()` 函数出口后，进入 GPU 硬件固定管线（纯硬件逻辑电路，零 Shader 算力开销）：

1. **Step 0 (Early-Z 前置优化失效)**：因 Shader 含有 `discard;` 语句，GPU 自动关闭 Early-Z，片元必须执行完 Frag Shader 才能进入后光栅化硬件测试。
2. **Step 1 (Scissor Test)**：检查坐标是否在 `glScissor` 裁切框内。
3. **Step 2 (Stencil Test)**：按 `glStencilFunc` 比较模板缓冲。
4. **Step 3 (Depth Test / Z-Test)**：
   - 硬件读取 CPU 开启的 `glEnable(GL_DEPTH_TEST)` 和比较规则 `glDepthFunc(GL_LESS)`。
   - 硬件比较电路比对片元原生深度 `gl_FragCoord.z` 与当前 FBO 深度附件的旧值 `oldDepth`。
   - **若 `gl_FragCoord.z < oldDepth`** $\rightarrow$ 测试**通过**；否则直接硬件丢弃（Discard）！
5. **Step 4 (Color Blending)**：若开启 `glEnable(GL_BLEND)`，按 `glBlendFuncSeparate` 计算混合颜色并写入 Color Attachment；若未开启则直接覆盖。
6. **Step 5 (Depth Write / Z-Write)**：
   - 硬件确认 Depth Test 已通过且 CPU 开启了 `glDepthMask(GL_TRUE)`。
   - GPU 硬件 Z-Write 单元**自动将 `gl_FragCoord.z` 写入绑定在 FBO 上的 `depthTexture(outputDepthIndex_)` 对应像素地址中**！

> 💡 **总结**：深度写入**不是**在 Shader 代码里手写赋值的，而是通过 C++ 绑定 FBO 深度附件 + 开启 `glDepthMask(GL_TRUE)` 后，由 **GPU 硬件固定管线在 Z-Test 通过后自动完成显存写入**。

### 2.4.4 过程性双深度缓冲区 (Intermediate Dual Depth Attachment) 职责与生命周期

在 Depth Peeling 算法中，系统分配了两个深度附件（`fboAccum_.depth` 与 `fboPeel_.depth`）：

1. **核心职责与角色定位**：
   - **纯过程性辅助缓冲区 (Intermediate Helper Buffer)**：这两张 32 位浮点深度纹理**唯一的作用**就是作为 Depth Peeling 乒乓循环内部的 Z 值比较和写入介质。
   - **最终屏幕合成零依赖**：在剥离循环结束、执行 `compositeToScreen()` 将结果绘制到屏幕时，系统仅采样了 `fboAccum_.color`（累积颜色纹理），**完全不需要也不使用任何深度纹理**。

2. **乒乓（Ping-Pong）轮换机制**：
   - `depthTexture(inputDepthIndex_)`：被 Shader 绑定为只读采样器（Uniform `texture_depth`），仅用来做 `if (z <= frontDepth) discard;` 的**下限卡截（剔除旧片元）**。
   - `depthTexture(outputDepthIndex_)`：被动态挂载为当前 FBO 的 `GL_DEPTH_ATTACHMENT`，供 GPU 硬件 ROP 单元做 `GL_LESS` 测试并自动写入**当前剥离层的新深度（上限选近）**。
   - 在每轮 Pass 末尾，`input` 与 `output` 索引发生交替交换（`0 <-> 1`），形成无缝的数据推演环路。

3. **生命周期与重置**：
   - **帧初**：`initPeelBuffers()` 调用 `glClearDepth(0.0f)` / `glClearDepth(1.0f)` 抹去上一帧残留数据。
   - **帧中**：在剥离 Pass 中经历多次 Shader 软件剔除与 GPU 硬件写入。
   - **帧末**：画面绘制到屏幕后使命完成；下一帧再次被重置擦除。

---

### 2.5 场景绘制函数

```cpp
void DepthPeelingApp::drawSceneLayer(Shader &shader, GLuint targetFbo,
                                     int inputDepthIndex,
                                     int outputDepthIndex) {
    const glm::mat4 view = camera_.view();
    const glm::mat4 projection = camera_.projection();
    const GLuint inputDepth = depthTexture(inputDepthIndex);

    shader.use();
    shader.setVec3("cameraPos", cameraPos_);
    shader.setVec3("lightPos", lightPos_);
    shader.setVec3("k", lightCoeffs_);
    shader.setVec2("u_ScreenSize",
                   glm::vec2(static_cast<float>(width_),
                             static_cast<float>(height_)));

    // Lambda 复用绘制逻辑
    auto draw = [&](Model &model, const glm::vec3 &pos, GLuint diffuseId) {
        shader.setMat4("model", modelMatrix(pos));
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);
        // 传入上一层的深度纹理作为裁剪参考
        model.Draw(shader, targetFbo,
                   {{"texture_diffuse", diffuseId},
                    {"texture_depth", inputDepth}},
                   {}, GL_TRIANGLES, {false, false});
    };

    // 绘制所有物体（不透明 + 透明，每个层都完整绘制一遍场景）
    draw(*modelSpot_, glm::vec3(0.0f, 0.0f, 0.0f), texSpot_->id);
    draw(*modelQuad_, glm::vec3(-0.5f, 0.0f, 0.8f), texWindowR_->id);
    draw(*modelQuad_, glm::vec3(0.2f, -0.5f, -1.0f), texWindowG_->id);
    draw(*modelQuad_, glm::vec3(0.2f, 0.0f, -0.5f), texWindowB_->id);
}
```

**注意**：每个剥离层都需要**完整绘制一遍场景中的所有物体**——这是 Depth Peeling 性能开销的主要来源。如果场景有 100 个物体，10 层剥离意味着需要绘制 1000 次。

### 2.6 最终合成

```cpp
void DepthPeelingApp::compositeToScreen() {
    shaderFinal_->use();
    shaderFinal_->setVec3("background_color", AppConfig::backgroundColor());
    // 全屏四边形：将累积颜色混合背景色，输出到默认帧缓冲
    modelQuad_->Draw(*shaderFinal_, 0,  // 0 = 默认帧缓冲
                    {{"texture_diffuse", fboAccum_.color.id}}, {},
                    GL_TRIANGLES, {true, true});
}
```

---

## 8. 完整渲染循环

```cpp
void DepthPeelingApp::run() {
    while (!glfwWindowShouldClose(window_)) {
        beginFrame();          // 处理输入
        initPeelBuffers();     // 初始化 FBO 和深度缓冲
        peelAndBlend();        // 逐层剥离 + Front-to-Back 混合
        compositeToScreen();   // 合成到屏幕
        endFrame();            // 交换缓冲 + 事件轮询
    }
}
```

---

## 9. 总结：深度剥离的核心精髓

Depth Peeling（深度剥离）算法的全流程可高度概括为**两大核心步骤**：

```
                              ┌────────────────────────────────────────┐
                              │  核心步骤一：双重深度剥离 (Dual Peel) │
                              └────────────────────────────────────────┘
                                                  │
                  ┌───────────────────────────────┴───────────────────────────────┐
                  ▼                                                               ▼
  【软件关卡 1: Fragment Shader】                              【硬件关卡 2: GPU 固定管线 ROP】
  `if (z <= frontDepth) discard;`                              `glDepthFunc(GL_LESS) + glDepthMask(TRUE)`
  剔除上一层及更靠前的旧片元（斩断过去）                       在剩余片元中选取最小 Z，并自动写入 outputDepth（锁定眼前）
                                                  │
                                                  ▼
                              ┌────────────────────────────────────────┐
                              │  核心步骤二：剥离后的混合累积 (Blend) │
                              └────────────────────────────────────────┘
                                                  │
                  ┌───────────────────────────────┴───────────────────────────────┐
                  ▼                                                               ▼
  【层间累积: Front-to-Back 混合】                              【最终合成: Composite Pass】
  `glBlendFuncSeparate(...)`                                   `fboAccum_.color` 与背景色合成
  利用 Alpha 归零公式自动完成实体遮挡                         最终画面输出至屏幕 FBO 0
```

| 核心模块 | 关键逻辑与技术点 |
|---|---|
| **第一步：双重剥离 (Dual Peel)** | 1. **Frag Shader 软剥离**：`if (z <= frontDepth) discard;` — 砍掉已处理的近端旧层；<br>2. **GPU 固定管线硬剥离**：`GL_LESS` + `glDepthMask(TRUE)` — 硬件自动在剩余片元中捕获最近的新层并写入 `outputDepth`。 |
| **第二步：混合累积 (Blend & Composite)** | 1. **Front-to-Back 自动实体遮挡**：`glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA)`；<br>2. **全屏合成**：将 `fboAccum_.color` 与背景颜色混合输出至屏幕。 |
| **辅助机制** | **乒乓双缓冲**（`inputDepth` / `outputDepth` 轮换） + **Occlusion Query**（`GL_SAMPLES_PASSED` 提前终止空循环）。 |

与 Linked List 方案相比，Depth Peeling 的实现更"传统"——它完全依赖 OpenGL 标准管线（深度测试、混合），不需要任何 GPU 端动态数据结构（SSBO、Image Texture、Atomic Counter），因此兼容性极好，但代价是多 Pass 带来的 Draw Call 性能开销。

## 10. 附录：RenderDoc 问题记录

> Depth Peeling OIT 开发与调参过程中记录的问题。

Event Browser 中 peel 循环为 **每层：Clear → Peel Draw → Blend to accum**。控制台 `Samples passed`（若有）可与 RenderDoc 中每层 Draw 的片元数交叉验证。

**RenderDoc 顺序：** peel FBO clear 后 depth 初值 → Peel Draw 的 depth func → Blend 的 blend state → accum FBO 逐层 Texture Viewer。

---

## OIT-DP-18 · Peel 层 depth clear 为 0

**现象**  
透明层缺失或 peel 提前结束，多层叠加只剩一层或全透明。

**原因**  
peel 循环内 `glClearDepth(0.0f)`，第一层 peel 时所有片元 depth test 失败（相对 LESS + 与 inputDepth 比较的逻辑）。

**定位过程**  
layer 0 的 Peel Clear 之后 → Peel FBO **depth attachment** 初值：错误为 0，应为 **1.0**。

**处理**  
peel 每层 clear 前 `glClearDepth(1.0f)`，与 `inputDepth` ping-pong 语义一致。

**涉及文件**  
`src/DepthPeelingApp.cpp`（约 281 行，peel 循环内 clear）

---

## OIT-DP-19 · 全局 depth func 与 peel shader 不一致

**现象**  
剥离层数不对，透明叠加顺序错误或层数过少。

**原因**  
`initPeelBuffers` 里设成 `glDepthFunc(GL_GREATER)`，与 shader 内基于 `inputDepth` 的 LESS 比较假设冲突。

**定位过程**  
Peel Draw → Pipeline → **Depth comparison**；对照 peel shader 对当前层 depth 与 `inputDepth` 的比较方向。

**处理**  
恢复 `glDepthFunc(GL_LESS)`，与 peel 逻辑及 opaque pass 一致。

**涉及文件**  
`src/DepthPeelingApp.cpp`（约 208 行，`initPeelBuffers`）

---

## OIT-DP-20 · Blend 累积公式参数错误

**现象**  
多层透明后颜色过亮、过暗，或 alpha 未正确累积。

**原因**  
`glBlendFuncSeparate` 的 src/dst factor 被改错，accum FBO 每层 blend 不符合预乘 alpha 累积设计。

**定位过程**  
每层 **Blend Draw** → Blend State；Texture Viewer 看 **accum FBO** 随层数变化是否与 peel color 一致。

**处理**  
恢复源码中的 `glBlendFuncSeparate` 参数；区分 peel depth 问题（看 peel FBO）与 blend 问题（看 accum RT）。

**涉及文件**  
`src/DepthPeelingApp.cpp`（约 296～297 行）

---

## 同类问题速查

| 现象 | 优先看 |
|------|--------|
| 第一层无 peel | peel clear depth 是否为 1.0 |
| 层数不对 | Peel Draw depth func |
| 颜色累积错 | Blend func；accum FBO 逐层 |



## 附录 B：通用管线与状态机机制（摘自通用知识点汇总）

### 深度测试比较规则 (glDepthFunc)
- GL_LESS (默认)：当片元深度严格小于缓冲值时通过。适合大多数不透明单 Pass 场景。
- GL_LEQUAL (小于等于)：当片元深度小于或等于缓冲值时通过。常用于 **Depth Peeling (深度剥离) 算法** 或 **Skybox (天空盒) 渲染**（解决浮点数精度极度接近时的 Z-Fighting 或相同深度舍弃问题）。

### OpenGL 颜色混合 (Blending) 机制与固定管线配置

### 1. 核心概念：Source (源) 与 Destination (目标)

* **`glDisable(GL_BLEND)` (关混合，默认)**：
  Shader 刚算好的片元颜色直接**强行覆盖 (Overwrite/Replace)** 帧缓冲区原本的旧颜色。
  $$\text{Final Color} = \text{Source Color}$$
* **`glEnable(GL_BLEND)` (开混合)**：
  激活硬件混合单元，将 Shader 算出的新颜色与帧缓冲区的旧颜色按指定公式做**数学加权计算**。

#### **源 (Source, `src`) 与 目标 (Destination, `dst`) 的定义**：
- **Source (源, `src`)**：**当前绘制指令中，Shader 刚计算出来、即将写进 Framebuffer 的新片元颜色**。
- **Destination (目标, `dst`)**：**当前 Framebuffer 中原本已经存在、接收新画面的旧颜色**。

---

### 2. 硬件混合公式与 API 配置

通用计算公式：
$$\text{Color}_{\text{final}}.rgb = C_{\text{src}}.rgb \times \text{srcFactor} + C_{\text{dst}}.rgb \times \text{dstFactor}$$
$$A_{\text{final}} = A_{\text{src}} \times \text{srcAlphaFactor} + A_{\text{dst}} \times \text{dstAlphaFactor}$$

常用设置 API：
- **`glBlendFunc(srcFactor, dstFactor)`**：同时设置 RGB 与 Alpha 的计算因子。
- **`glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha)`**：独立控制 RGB 和 Alpha 通道的计算因子。

---

### 3. 三大经典混合模型与代码配置

| 混合模型 | 应用场景 | API 代码配置 | 最终数学公式 |
| :--- | :--- | :--- | :--- |
| **传统 Alpha 混合**<br>(Back-to-Front) | 普通半透明玻璃、UI 界面元素、不透明度 `a` | `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` | $C = C_{\text{src}} \cdot A_{\text{src}} + C_{\text{dst}} \cdot (1 - A_{\text{src}})$ |
| **加法叠加**<br>(Additive Blending) | 光效、火焰、粒子、激光、光晕 | `glBlendFunc(GL_ONE, GL_ONE)` | $C = C_{\text{src}} + C_{\text{dst}}$ |
| **Front-to-Back 混合**<br>(Depth Peeling) | 深度剥离 OIT 透明度累积 | `glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA)` | $C = C_{\text{src}} \cdot A_{\text{dst}} + C_{\text{dst}}$<br>$A_{\text{final}} = A_{\text{dst}} \cdot (1 - A_{\text{src}})$ |

### 4. Front-to-Back 自动实体遮挡机制
在 Depth Peeling 深度剥离等 Front-to-Back (自前向后) 渲染算法中，**实体（不透明）物体的遮挡是通过混合算式自动完成的**：
- **通俗逻辑**：如果当前剥离层的 Alpha 为 1（即不透明实体），后续计算出的剩余透光率就是 $(1 - 1) = 0$。
- **数学推导**：
  1. 当前层 Alpha $A_{\text{src}} = 1.0 \implies$ 剩余透光率 $A_{\text{final}} = A_{\text{dst}} \cdot (1 - 1.0) = 0.0$（直接归零）。
  2. 此后后续 Pass 剥离出的任何背部物体（无论透明还是实体），其颜色贡献 $C_{\text{src\_next}} \cdot A_{\text{dst}} = C_{\text{src\_next}} \cdot 0.0 = 0$。
- **结论**：**乘数因子变成了 0，后续剥离出来的所有层颜色全部乘以 0 彻底无效（被自动遮挡）**，无需手动剔除实体背后的透明物体！

---


### GPU 硬件固定管线（后光栅化 ROP 单元）工作机制全解析

### 1. 概念澄清：可编程着色器 vs 硬件固定管线

在 GPU 渲染管线中，**软件可编程阶段**与**硬件固定管线阶段**有着本质区别：

| 维度 | 可编程着色器 (Programmable Shader) | 硬件固定管线 (Fixed-Function Hardware / ROP) |
| :--- | :--- | :--- |
| **硬件载体** | GPU 通用计算核心（CUDA Cores / Shader Execution Units / ALUs） | 芯片上固化的专用硬件电路模块（如 **ROP - Render Output Unit / Output Merger**） |
| **代码执行** | 运行由开发人员编写的 GLSL / HLSL 编译生成的 GPU 字节码 | **不运行任何 Shader 代码**，由芯片硬件逻辑门直接执行 |
| **控制方式** | 通过 GLSL 代码逻辑实现（矩阵乘法、光照方程、`discard` 等） | 通过 CPU 端的 OpenGL API 开关控制状态（如 `glEnable(GL_DEPTH_TEST)`、`glDepthMask`、`glDepthFunc`、`glBlendFunc`） |
| **核心特点** | 灵活性极高、功能强大，但会消耗 GPU 算力和寄存器 | 功能固化无法编写自定义代码，但**吞吐量极大、硬编码电路零 Shader 算力开销** |

---

### 2. 片元离开 Fragment Shader 后的后光栅化全流程（6 步硬件流水线）

当一个片元（Fragment Candidate）在 Fragment Shader 中完成颜色计算并顺利从 `main()` 函数出口离开后，它会进入 GPU 芯片后端的 ROP 硬件单元，按严格的先后顺序执行以下测试与显存写入：

```
[Fragment Shader 输出] 
       │
       ▼
 1. 裁剪测试 (Scissor Test) ──(失败)──► [片元丢弃 (Discard)]
       │ (通过)
       ▼
 2. 模板测试 (Stencil Test) ──(失败)──► [片元丢弃 (Discard)]
       │ (通过)
       ▼
 3. 硬件深度测试 (Depth Test) ──(失败)──► [片元丢弃 (Discard)]
       │ (通过)
       ├──► 4. 颜色混合/覆盖 (Color Blending / Overwrite) ──► 写入 FBO 颜色附件 (Color Attachment)
       │
       └──► 5. 硬件深度写入 (Depth Write) ──► 写入 FBO 深度附件 (Depth Attachment)
```

#### **各步骤前因后果与硬件细节详解**：

#### **Step 0: 前置优化 —— 硬件 Early-Z 机制及其失效条件**
* **前因**：为了避免对最终会被遮挡的片元徒劳运行昂贵的 Fragment Shader，现代 GPU 包含 **Early-Z (前置深度测试)** 硬件单元，在顶点光栅化之后、Fragment Shader 运行**之前**就提前进行深度比较与剔除。
* **在 Depth Peeling / 复杂 Alpha 测试中失效的原因**：
  在 Depth Peeling 的剥离 Shader (`depth_peeling_render.frag`) 中，代码显式使用了 `if (gl_FragCoord.z <= frontDepth) discard;`。一旦 GPU 的 Early-Z 监测电路发现 Shader 内部包含 `discard` 指令或显式改写 `gl_FragDepth`，**硬件将强制关闭 Early-Z 优化**！
* **后果**：所有场景几何片元必须完整执行完 Fragment Shader 的 CPU/GPU 指令后，才能进入后续的固定管线深度测试。

#### **Step 1: 裁剪测试 (Scissor Test)**
* **检查权限与条件**：若 CPU 开启了 `glEnable(GL_SCISSOR_TEST)`，硬件检查该片元的屏幕坐标 $(x, y)$ 是否位于 `glScissor(x, y, width, height)` 指定的矩形框内。
* **结果**：若落在框外，ROP 硬件直接切断该片元信号（丢弃），流程结束。

#### **Step 2: 模板测试 (Stencil Test)**
* **检查权限与条件**：若 CPU 开启了 `glEnable(GL_STENCIL_TEST)`，硬件拿出绑定在 FBO 上的 Stencil 缓冲区旧值，按 `glStencilFunc` 进行比较。
* **结果**：未通过比较则放弃该片元，并根据 `glStencilOp` 更新模板缓冲区旧值。

#### **Step 3: 硬件深度测试 (Depth Test / Z-Test)**
* **检查权限与条件**：硬件读取 CPU 通过 `glEnable(GL_DEPTH_TEST)` 设置的开启标志。
* **硬件操作**：
  1. 硬件自动提取该片元的**原生插值深度值** `gl_FragCoord.z`（由顶点变换与光栅化硬件自动线性插值算出，范围在 $[0.0, 1.0]$）。
  2. 硬件从当前 FBO 挂载的深度纹理（Depth Attachment）中读取像素 $(x, y)$ 地址存储的**旧深度值** `oldDepth`。
  3. 硬件硬件比较电路按 CPU 设定的比较函数 `glDepthFunc`（例如 `GL_LESS` 或 `GL_LEQUAL`）进行比较：
     $$\text{IsPassed} = (\text{gl\_FragCoord.z} < \text{oldDepth})$$
* **结果**：
  * **未通过 (Fail)**：说明该片元比之前画在这里的物体更远（被遮挡了），ROP 硬件**立即丢弃该片元**！该片元的颜色和深度均不会写进 FBO。
  * **通过 (Pass)**：说明该片元比之前记录的深度更近，允许进入颜色计算与深度写入阶段！

#### **Step 4: 硬件颜色混合与写入 (Color Blending & Color Write)**
* **若 `glEnable(GL_BLEND)` 关闭**：
  ROP 硬件直接用 Frag Shader 输出的 `FragColor` **覆盖 (Overwrite)** FBO 颜色附件中 $(x, y)$ 位置的显存数据。
* **若 `glEnable(GL_BLEND)` 开启**：
  ROP 硬件读出颜色附件原有的旧颜色 $C_{\text{dst}}$，利用硬件乘加逻辑电路按 `glBlendFuncSeparate` 计算混合后的最终 RGB/Alpha 颜色，然后写入显存。

#### **Step 5: 硬件深度写入 (Depth Write / Z-Write)**
* **触发前提**：**深度测试成功通过** 并且 CPU 端开启了 `glDepthMask(GL_TRUE)`（允许深度写入）。
* **硬件自动化行为**：
  GPU 的 Z-Write 电路**自动**把该片元的原生插值深度 `gl_FragCoord.z` 写入到绑定在 FBO 深度附件上的深度纹理（Depth Texture）内存地址 $(x, y)$ 中。
* **注意**：如果 CPU 调用了 `glDepthMask(GL_FALSE)`（例如在 Alpha 混合渲染 Pass 中），即使深度测试通过了，GPU 硬件也会**锁定深度写入通道**，深度纹理中的旧值维持不变。

---

### 3. 总结：Depth Peeling 中的“双重 Z 筛选”分工

在 Depth Peeling 算法的每轮剥离 Pass 中，片元实际上先后经历了**软件 Shader 剔除**与**硬件固定管线更新**的双重筛选：

```
                              [几何顶点光栅化片元]
                                       │
                                       ▼
                       【第一关：Fragment Shader 软件逻辑】
                        检查: gl_FragCoord.z <= frontDepth ?
                                 /           \
                           (是: 属于旧层)    (否: 属于未剥离的新片元)
                               /               \
                       [discard 丢弃!]       计算 FragColor 并离开 Shader
                                                   │
                                                   ▼
                                   【第二关：GPU ROP 硬件固定管线】
                                    硬件比对: gl_FragCoord.z < outputDepth ? (GL_LESS)
                                             /           \
                                       (否: 被同层遮挡)  (是: 当前层最靠前片元)
                                           /               \
                                   [硬件丢弃片元]       1. 硬件自动写入 gl_FragCoord.z 到 outputDepth
                                                       2. 写入颜色到 fboPeel.color
```




