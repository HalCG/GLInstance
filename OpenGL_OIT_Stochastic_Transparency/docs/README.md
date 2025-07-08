# OpenGL OIT 之 Stochastic Transparency 实现

> 源码地址：[GitHub 仓库](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_OIT_Stochastic_Transparency)

---

## 0. 前言

在前两篇中，我们分别介绍了 [Linked List OIT](OIT_Linked_List_上篇_原理与缓冲区设计.md)（通过 GPU 端链表收集排序）和 [Depth Peeling OIT](OIT_Depth_Peeling_上篇_原理与架构.md)（通过多次 Pass 逐层剥离）。这两种方案都是**确定性**的——每个像素的最终颜色是精确计算的。

Stochastic Transparency（随机/各向同性透明度）走了一条完全不同的路：**用随机化替代排序**。它不精确，但实现极其简单，只需要一个 Pass 和一个 Shader。

---

## 1. 核心思想

### 1.1 问题回顾

回顾 alpha 混合的 over 算子：

```
C_result = C1 * α1 + (1 - α1) * C2 * α2 + (1 - α1)(1 - α2) * C3 * α3 + ...
```

这个公式要求片段按深度排序（Back-to-Front）。如果排序错误，结果就不对。

### 1.2 Stochastic Transparency 的直觉

与其精确排序，不如**随机抽样**。假设一个像素有 3 个重叠的透明片段：

| 片段 | 颜色 | 透明度 |
|------|------|--------|
| A | 红色 | 0.5 |
| B | 绿色 | 0.3 |
| C | 蓝色 | 0.2 |

如果我们有 16 个样本（MSAA 16x），按概率分配：

```
样本 1-8:  红色   (0.5 × 16 = 8 个样本)
样本 9-12: 绿色   (0.3 × 16 ≈ 4.8 → 4 个样本)
样本 13-16: 蓝色  (0.2 × 16 ≈ 3.2 → 3 个样本)
```

硬件 MSAA 自动将这 16 个样本混合成最终颜色，近似得到正确结果。**不需要排序，因为每个样本只会被一个片段覆盖**——这就是随机化解决的问题。

---

## 2. 关键技术：MSAA + gl_SampleMask

### 2.1 MSAA 基础

MSAA（Multi-Sample Anti-Aliasing）是 GPU 硬件支持的抗锯齿技术。对于每个像素，GPU 维护多个子样本（通常 4x、8x、16x），每个子样本独立存储颜色和深度。

正常 MSAA 流程：
1. 光栅化产生多个覆盖样本
2. 每个样本独立进行深度测试
3. 解析（resolve）时，所有样本的颜色平均得到最终像素颜色

### 2.2 gl_SampleMask —— 核心武器

`gl_SampleMask` 是 GLSL 4.0+ 引入的内建输出变量，允许 Fragment Shader **逐样本控制哪些样本被写入**。

```glsl
out int gl_SampleMask[];  // 每个 bit 控制一个样本
```

- `gl_SampleMask[0]` 是一个 32-bit 整数，bit 0 控制样本 0，bit 1 控制样本 1，以此类推
- 某 bit 为 1 → 该样本被当前片段覆盖
- 某 bit 为 0 → 该样本不被当前片段覆盖（保留原有值或不变）

**这正是 Stochastic Transparency 的核心**：用 alpha 值作为概率，决定每个样本是否被覆盖。

### 2.3 为什么彻底不需要排序？（MSAA 子采样点 Z-Buffer 裁决机制）

对不熟悉 GPU 硬件处理机制的人来说，最核心的疑问往往是：**“如果多个重叠的半透明物体，恰好抢占/筛中了同一个 MSAA 子采样点（孔 $i$），会发生什么？”**

答案是：**每个 MSAA 子采样点（孔 $i$）在显存中都拥有一套独立的 32 位 Hardware Depth Buffer（深度缓冲）和 RGBA 颜色**！当多个物体在同一个孔 $i$ 发生碰撞时，GPU 硬件层会按以下规则自动裁决：

```
假设像素位置有：近处红色玻璃 A (Z=0.3) 和 远处蓝色玻璃 B (Z=0.7)

┌─────────────────────────┬─────────────────────────┬──────────────────────────────────────────┐
│  近处物体 A 的掩码选择   │  远处物体 B 的掩码选择   │         MSAA 子采样点 i 上的硬件裁决结果         │
├─────────────────────────┼─────────────────────────┼──────────────────────────────────────────┤
│ 孔 i 被选中 (bit i = 1) │ 孔 i 被选中 (bit i = 1) │ 碰撞！GPU 硬件 Z-Test 发挥作用：0.3 < 0.7  │
│                         │                         │ 近处物体 A 硬件覆盖远处物体 B 的颜色与深度 │
├─────────────────────────┼─────────────────────────┼──────────────────────────────────────────┤
│ 孔 i 未选中 (bit i = 0) │ 孔 i 被选中 (bit i = 1) │ 近处 A 留空，远处物体 B 写入孔 i           │
│                         │                         │ 结果：从孔 i 中成功透出了远处的蓝色玻璃 B │
├─────────────────────────┼─────────────────────────┼──────────────────────────────────────────┤
│ 孔 i 未选中 (bit i = 0) │ 孔 i 未选中 (bit i = 0) │ 两个物体均留空，孔 i 保留最背后的背景颜色│
└─────────────────────────┴─────────────────────────┴──────────────────────────────────────────┘
```

**关键结论**：
1. **子采样点内依然有排序**：在每一个单独的 MSAA 孔内，遮挡关系**依然由 GPU 硬件 Z-Buffer 自动裁决（近者覆盖远者）**！
2. **免 CPU/GPU 手动排序**：因为所有透明物体只是在“随机抢孔”。抢到同一个孔的，谁近谁留存；没抢到的，给后面的物体留出空隙。
3. **统计学期望精准**：最终 16 个孔做 MSAA 平均（Resolve）时，透光率和遮挡关系在统计学期望上完美对齐 Ground Truth。

---

## 3. 算法流程

### 3.1 整体流程

```
┌─────────────────────────────────────────────────────────────────────┐
│                        每帧渲染循环                                  │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Step 1: 初始化 MSAA 状态                                     │  │
│  │  - glEnable(GL_MULTISAMPLE)    ← 启用 MSAA                    │  │
│  │  - glEnable(GL_SAMPLE_MASK)    ← 允许 Shader 写 sample mask    │  │
│  │  - glEnable(GL_DEPTH_TEST)     ← 启用深度测试                  │  │
│  │  - glDepthFunc(GL_LEQUAL)      ← 深度比较函数                  │  │
│  │  - glDepthMask(GL_TRUE)        ← 允许深度写入                  │  │
│  │  - GLFW_SAMPLES = 16           ← 16x MSAA                     │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                            │                                        │
│                            ▼                                        │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Step 2: 逐个对象渲染（同一 Pass）                             │  │
│  │                                                              │  │
│  │  for each object:                                            │  │
│  │    ┌────────────────────────────────────────────────┐        │  │
│  │    │ 2a. 设置 model/view/projection 矩阵              │        │  │
│  │    │ 2b. 传入 sampleCnt（MSAA 样本数）                 │        │  │
│  │    │ 2c. 传入 frameID（随机种子，每个对象不同）          │        │  │
│  │    │ 2d. Fragment Shader:                              │        │  │
│  │    │     - 采样纹理获取颜色和 alpha                    │        │  │
│  │    │     - alpha → 覆盖率                              │        │  │
│  │    │     - 对每个样本 i 生成随机数 r[i]                  │        │  │
│  │    │     - if r[i] < coverage → randMask |= (1 << i)  │        │  │
│  │    │     - gl_SampleMask[0] = randMask                │        │  │
│  │    └────────────────────────────────────────────────┘        │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                            │                                        │
│                            ▼                                        │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Step 3: 硬件 MSAA 自动解析                                   │  │
│  │  - glfwSwapBuffers 时 GPU 自动将样本平均为最终像素颜色          │  │
│  │  - 无需额外 Shader 或 FBO                                     │  │
│  └──────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.2 随机数生成

Shader 中使用经典的伪随机数生成器：

```glsl
vec2 seed = vec2(sampleIdx, frameID);
float r = fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
```

- `sampleIdx`：样本索引 0..15，保证每个样本的随机数不同
- `frameID`：每个对象的唯一 ID，作为不同对象之间的种子区分
- 结果 `r` 在 [0, 1) 范围内均匀分布

### 3.3 覆盖率决策

```glsl
float coverage = color.w;  // alpha 值作为覆盖率
if (r < coverage) {
    randMask |= (1u << i);  // 样本 i 被当前片段覆盖
}
```

例如 alpha = 0.5，16 个样本中大约 8 个会被覆盖（因为 r < 0.5 的概率是 50%）。

---

## 4. 深度测试与遮挡处理

### 4.1 为什么需要深度测试？

Stochastic Transparency 不排序，但**深度测试仍然是必要的**。原因是：

- 如果一个不透明的片段遮挡了透明的片段，不透明的片段应该覆盖所有样本
- 深度测试的 `GL_LEQUAL` 确保：如果当前片段在已有的片段前面，则覆盖；如果在后面，则不覆盖

### 4.2 深度写入

```cpp
glDepthMask(GL_TRUE);  // 允许深度写入
```

每个片段通过深度测试后，会更新深度缓冲。这确保了后续更远的片段不会覆盖更近的片段。

### 4.3 完整 GL 状态

```cpp
glEnable(GL_MULTISAMPLE);   // 启用 MSAA 多重采样
glEnable(GL_SAMPLE_MASK);   // 启用 sample mask 输出
glEnable(GL_DEPTH_TEST);    // 启用深度测试
glDepthFunc(GL_LEQUAL);     // 通过深度测试：<= 当前深度
glDepthMask(GL_TRUE);       // 写入深度缓冲
```

**GLFW 窗口配置**：
```cpp
glfwWindowHint(GLFW_SAMPLES, 16);  // 请求 16x MSAA
```

---

## 5. 随机种子（frameID）的作用

### 5.1 为什么需要不同的种子？

如果所有对象用相同的种子，每个样本的随机数序列就完全一样——这意味着所有对象会在**完全相同的样本**上竞争，导致结果不随机、不正确。

使用不同的 `frameID` 作为种子，每个对象得到独立的随机数序列，避免了样本上的竞争模式。

### 5.2 本实现中的 frameID 策略

```cpp
static int frameID = 0;
int modelCnt = 4;

// 每个对象使用不同的 frameID
shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
```

- 4 个对象（1 个 Spot + 3 个 Window），frameID 在 0-3 之间循环
- 每个对象有固定的随机种子，这意味着结果是**静态的**（不会随时间变化）

> **注意**：这种实现每次运行结果相同，不产生时间噪声。如果希望每帧随机化，可以将 `frameID` 设为随机值或在每帧开始时随机化。

---

## 6. 与其他 OIT 方案对比

| 特性 | Linked List | Depth Peeling | Stochastic |
|------|-------------|---------------|-------------|
| **排序方式** | GPU 链表排序 | 逐层剥离 | 随机化，不排序 |
| **Pass 数量** | 3 个 | N 层 × 1 Pass | 1 个 Pass |
| **GPU 存储** | SSBO + Image | 2 个 FBO 深度纹理 | 无（仅 MSAA 缓冲） |
| **精度** | 精确 | 精确 | 近似（随机） |
| **兼容性** | GL 4.3+ | GL 3.3+ | GL 4.0+ |
| **性能** | 链表操作开销 | N × 场景绘制 | 1 × 场景绘制 |
| **噪声** | 无 | 无 | 有（静态/动态） |
| **实现复杂度** | 高 | 中 | 极低 |

---

## 7. 优缺点

### 优点
- **实现极其简单**：一个 Shader，一个 Pass，不需要 FBO、SSBO、Atomic Counter
- **性能好**：不需要多次绘制场景
- **不需要排序**：完全消除了排序的开销

### 缺点
- **结果是近似的**：存在随机噪声，不是精确结果
- **依赖 MSAA**：样本数受 GPU 硬件限制（通常 16x 或 32x）
- **低透明度难处理**：alpha 很小时，只有少数样本被覆盖，可能产生闪烁
- **静态噪声**：本实现中 frameID 固定，噪声不随时间平均

---

## 8. 总结

Stochastic Transparency 的核心思路可以概括为：

> **用概率替代排序，用 MSAA 替代精确混合。**

1. **gl_SampleMask**：逐样本控制片段覆盖，用 alpha 值作为概率
2. **MSAA 硬件**：自动将样本平均为最终颜色，无需手动混合
3. **单 Pass 渲染**：所有对象在一个 Pass 中绘制，深度测试处理遮挡
4. **随机种子**：frameID 区分不同对象的随机序列，避免样本竞争

下篇将深入 Shader 代码实现，逐行解析 `quad.frag` 中的随机样本掩码生成逻辑。

## 9. 核心 Shader 实现

### 1.1 quad.vert —— 顶点着色器

```glsl
#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNor;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 vertexPos;
out vec3 vertexNor;
out vec2 textureCoord;

void main() {
    textureCoord = aTexCoord;
    gl_Position = projection * view * model * vec4(aPos, 1.0f);
    vertexPos = (model * vec4(aPos, 1.0f)).xyz;
    vertexNor = mat3(transpose(inverse(model))) * aNor;
}
```

标准的 MVP 变换，输出世界空间坐标和法线（虽然本实现中 Fragment Shader 未使用光照，但保持了接口完整性）。

### 1.2 quad.frag —— 片段着色器（核心）

这是整个 Stochastic Transparency 的**唯一关键 Shader**，只有 26 行，完成了全部透明渲染逻辑：

```glsl
#version 420 core
out vec4 FragColor;

in vec2 textureCoord;

uniform int frameID;       // 随机数种子（每个对象不同）
uniform int sampleCnt;     // MSAA 样本数（如 16）
uniform sampler2D texture_diffuse;

void main() {
    vec4 color = texture(texture_diffuse, textureCoord);

    // ==== 步骤 1: 获取 alpha 值作为覆盖率 ====
    float coverage = color.w;

    // ==== 步骤 2: 为每个 MSAA 样本生成随机掩码 ====
    uint randMask = 0u;
    for (int i = 0; i < sampleCnt; i++) {
        // 伪随机数生成：基于样本索引 i 和对象 ID frameID
        vec2 seed = vec2(i, frameID);
        float r = fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);

        // 如果随机数 < 覆盖率，则启用该样本
        if (r < coverage) {
            randMask |= (1u << i);
        }
    }

    // ==== 步骤 3: 写入 sample mask ====
    gl_SampleMask[0] = int(randMask);

    // ==== 步骤 4: 输出颜色（硬件 MSAA 自动解析） ====
    FragColor = color;
}
```

---

## 10. 逐行解析

### 2.1 `gl_SampleMask[0]` —— 逐样本控制

```glsl
gl_SampleMask[0] = int(randMask);
```

这是整个 Stochastic Transparency 的**核心 API**。`gl_SampleMask` 是 GLSL 内建输出变量：

| 项目 | 说明 |
|------|------|
| 类型 | `out int gl_SampleMask[]` |
| 可用版本 | GLSL 4.0+（需 `#version 400` 或更高） |
| 含义 | 每个 bit 控制一个 MSAA 样本是否被当前片段覆盖 |
| 范围 | `gl_SampleMask[0]` 的 bit 0-31 对应样本 0-31 |

**工作原理图解**：

```
假设 MSAA 16x，alpha = 0.5

  样本编号:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
  随机数 r: 0.3 0.7 0.1 0.9 0.4 0.6 0.2 0.8 0.5 0.3 0.9 0.1 0.7 0.4 0.6 0.2
  r < 0.5?   Y   N   Y   N   Y   N   Y   N   Y   Y   N   Y   N   Y   N   Y
  randMask:  1   0   1   0   1   0   1   0   1   1   0   1   0   1   0   1
                                      ↑
                              bit 8 = 1 → 样本 8 被覆盖

  16 个样本中约 8 个被覆盖（≈ 50%），与 alpha 值匹配
```

#### 多个物体抢占同一个样本点（孔 $i$）时的硬件裁决

当像素位置重叠着近处玻璃 A ($Z=0.3$) 与远处玻璃 B ($Z=0.7$) 时：
- **若 A 和 B 都选中了样本 $i$**：触发 GPU 硬件深度测试 ($0.3 < 0.7$)，**近处玻璃 A 自动覆盖远处玻璃 B 的颜色与深度**。
- **若 A 未选中样本 $i$ 而 B 选中了**：玻璃 A 留空，远处玻璃 B 写入样本 $i$，**画面从该样本透出背后的玻璃 B**。

> 💡 **噪点与 TAA 降噪**：由于选点基于随机“掷骰子”，单帧画面在局部会产生细小噪点。在 UE5/Unity 现代引擎中，通过每帧旋转 `frameID` + 结合 TAA（时间抗锯齿）跨帧平滑，噪点可在 2~3 帧内瞬间被完全滤除！

### 2.2 伪随机数生成

```glsl
vec2 seed = vec2(i, frameID);
float r = fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
```

这是一个经典的 GLSL 伪随机数生成器（常用于 ShaderToy）：

| 步骤 | 计算 | 说明 |
|------|------|------|
| 1 | `dot(seed, vec2(12.9898, 78.233))` | 点积，将二维种子映射为一维值 |
| 2 | `sin(...)` | 三角函数，输出在 [-1, 1] |
| 3 | `* 43758.5453` | 放大（大常数放大波动） |
| 4 | `fract(...)` | 取小数部分，结果在 [0, 1) |

**为什么用 `vec2(i, frameID)` 作为种子？**
- `i`：样本索引（0..15），保证 16 个样本的随机数序列不同
- `frameID`：对象 ID（0..3），保证不同对象的随机数序列不同

### 2.3 覆盖率决策

```glsl
float coverage = color.w;  // alpha 值
if (r < coverage) {
    randMask |= (1u << i);
}
```

`(1u << i)` 将 bit i 置为 1。例如：

| i | 1u << i | 二进制 |
|---|---------|--------|
| 0 | 1       | 0000000000000001 |
| 1 | 2       | 0000000000000010 |
| 2 | 4       | 0000000000000100 |
| ... | ... | ... |
| 15 | 32768   | 1000000000000000 |

---

## 11. CPU 侧关键代码

### 3.1 初始化 —— MSAA 状态设置

```cpp
bool StochasticTransparencyApp::init() {
    // ... 窗口初始化 ...

    // 核心：启用 MSAA 和 Sample Mask
    glEnable(GL_MULTISAMPLE);   // 启用 MSAA 多重采样
    glEnable(GL_SAMPLE_MASK);   // 允许 Shader 写入 gl_SampleMask

    // 深度测试配置
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);     // 通过测试：深度 <= 当前深度
    glDepthMask(GL_TRUE);       // 写入深度缓冲

    return true;
}
```

**`GL_MULTISAMPLE` vs `GL_SAMPLE_MASK` 的区别**：

| 状态 | 作用 |
|------|------|
| `GL_MULTISAMPLE` | 启用 MSAA 硬件管线——每个像素维护多个样本 |
| `GL_SAMPLE_MASK` | 允许 Fragment Shader 通过 `gl_SampleMask` 控制每个样本的写入 |

**两者必须同时启用**。如果只启用 `GL_MULTISAMPLE` 而不启用 `GL_SAMPLE_MASK`，Shader 中的 `gl_SampleMask` 赋值将被忽略，所有样本都被覆盖。

### 3.2 窗口创建 —— MSAA 样本数配置

```cpp
bool StochasticTransparencyApp::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // 关键：请求 16x MSAA
    glfwWindowHint(GLFW_SAMPLES, 16);

    window_ = glfwCreateWindow(/* ... */);

    // 确认实际支持的样本数
    GLint maxSamples;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    std::cout << "Max supported MSAA samples: " << maxSamples << std::endl;
}
```

**几点说明**：
- `GLFW_SAMPLES` 是窗口级别的提示，实际样本数取决于 GPU 硬件支持
- `glGetIntegerv(GL_MAX_SAMPLES)` 查询 GPU 支持的最大 MSAA 样本数
- 样本数越多，随机近似越精确，但性能开销也越大

### 3.3 renderScene() —— 渲染循环

```cpp
void StochasticTransparencyApp::renderScene() {
    shaderQuad_->use();

    // 设置相机矩阵
    glm::mat4 cameraView = glm::lookAt(/* ... */);
    glm::mat4 cameraProjection = glm::perspective(/* ... */);
    shaderQuad_->setMat4("view", cameraView);
    shaderQuad_->setMat4("projection", cameraProjection);

    // 获取 MSAA 样本数
    GLint maxSamples;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);

    static int frameID = 0;
    int modelCnt = 4;

    // ===== 绘制 Spot（不透明） =====
    shaderQuad_->setMat4("model", modelMatrix(glm::vec3(0.0f, 0.0f, 0.0f)));
    shaderQuad_->setInt("sampleCnt", maxSamples);
    shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
    modelSpot_->Draw(*shaderQuad_, 0,  // 0 = 直接渲染到默认帧缓冲
                    {{"texture_diffuse", texSpot_->id}}, {},
                    GL_TRIANGLES, {false, false});

    // ===== 绘制 Blue Window（透明） =====
    shaderQuad_->setMat4("model", modelMatrix(glm::vec3(0.3f, -0.1f, -0.8f), 0.5f));
    shaderQuad_->setInt("sampleCnt", maxSamples);
    shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
    modelQuad_->Draw(*shaderQuad_, 0,
                    {{"texture_diffuse", texWindowB_->id}}, {},
                    GL_TRIANGLES, {false, false});

    // ===== 绘制 Green Window（透明） =====
    shaderQuad_->setMat4("model", modelMatrix(glm::vec3(0.6f, 0.6f, -0.6f), 0.5f));
    shaderQuad_->setInt("sampleCnt", maxSamples);
    shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
    modelQuad_->Draw(*shaderQuad_, 0,
                    {{"texture_diffuse", texWindowG_->id}}, {},
                    GL_TRIANGLES, {false, false});

    // ===== 绘制 Red Window（透明） =====
    shaderQuad_->setMat4("model", modelMatrix(glm::vec3(0.0f, 0.0f, 0.0f), 0.5f));
    shaderQuad_->setInt("sampleCnt", maxSamples);
    shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
    modelQuad_->Draw(*shaderQuad_, 0,
                    {{"texture_diffuse", texWindowR_->id}}, {},
                    GL_TRIANGLES, {false, false});
}
```

**关键观察**：
1. **直接渲染到默认帧缓冲**（`targetFbo = 0`）——不需要任何离屏 FBO
2. **深度测试自动处理遮挡**——硬件 MSAA 对每个样本独立进行深度测试
3. **frameID 递增**——每个对象有独立的随机种子

### 3.4 完整渲染循环

```cpp
void StochasticTransparencyApp::run() {
    while (!glfwWindowShouldClose(window_)) {
        processInput(window_);
        beginFrame();      // 清空颜色 + 深度缓冲
        renderScene();     // 单 Pass 渲染所有对象
        endFrame();        // glfwSwapBuffers（硬件 MSAA 解析）
    }
}
```

与 Linked List（3 Pass）和 Depth Peeling（N Pass）相比，Stochastic Transparency 的渲染循环**极其简洁**——只有一个渲染 Pass，没有 FBO 切换，没有纹理绑定。

---

## 12. 完整数据流

```
┌──────────────┐    ┌──────────────────┐    ┌─────────────────────────┐
│  CPU 侧      │    │  顶点着色器       │    │  片段着色器（核心）       │
│              │    │                  │    │                         │
│  frameID ────┼────┼──────────────────┼────┤→ 随机种子               │
│  sampleCnt ──┼────┼──────────────────┼────┤→ 循环次数               │
│              │    │                  │    │                         │
│  model ──────┼────┤→ MVP 变换       │    │                         │
│  view ───────┼────┤→ worldPos       │    │                         │
│  projection ─┼────┤→ gl_Position    │    │                         │
│              │    │                  │    │                         │
│              │    │                  │    │  texture → color.a      │
│              │    │                  │    │         ↓               │
│              │    │                  │    │  coverage = color.a     │
│              │    │                  │    │         ↓               │
│              │    │                  │    │  for i in 0..sampleCnt: │
│              │    │                  │    │    r = random(i,frameID)│
│              │    │                  │    │    if r < coverage:     │
│              │    │                  │    │      randMask |= 1<<i   │
│              │    │                  │    │         ↓               │
│              │    │                  │    │  gl_SampleMask[0] = ... │
│              │    │                  │    │  FragColor = color      │
└──────────────┘    └──────────────────┘    └───────────┬─────────────┘
                                                        │
                                                        ▼
                                          ┌─────────────────────────┐
                                          │  硬件 MSAA 管线           │
                                          │                         │
                                          │  每个样本独立深度测试     │
                                          │  每个样本按 mask 覆盖     │
                                          │  resolve: 样本平均 → 像素 │
                                          └───────────┬─────────────┘
                                                      │
                                                      ▼
                                          ┌─────────────────────────┐
                                          │  默认帧缓冲              │
                                          │  glfwSwapBuffers → 屏幕  │
                                          └─────────────────────────┘
```

---

## 13. 代码实现总结

| 组成 | 说明 |
|------|------|
| **vertex shader** | 标准 MVP 变换，无特殊逻辑 |
| **fragment shader** | 唯一核心：alpha → 随机样本掩码 → `gl_SampleMask[0]` |
| **随机数** | `fract(sin(dot(seed, ...)) * 43758.5453)` 经典 ShaderToy 随机 |
| **frameID** | 区分不同对象的随机种子，`(frameID++) % modelCnt` 循环 |
| **GL 状态** | `GL_MULTISAMPLE` + `GL_SAMPLE_MASK` + `GL_DEPTH_TEST` |
| **渲染目标** | 直接渲染到默认帧缓冲，无 FBO |
| **Pass 数量** | 1 个 Pass |

Stochastic Transparency 的代码量是三种 OIT 方案中最少的——核心 Shader 只有 26 行。它用概率论替代了精确排序，用 MSAA 硬件替代了手动混合，是"less is more"的典范。代价是结果的随机噪声，但对于许多应用场景（游戏、实时预览等），这种近似是完全可接受的。


## 14. 附录：博客初稿与要点备忘

### 14.1 摘要

传统半透明需要按深度排序再 Alpha 混合。本文说明一种 **无需排序** 的近似做法：**Stochastic Transparency**——在 MSAA 的每个子采样上按纹理 Alpha「掷骰子」，用 `gl_SampleMask` 决定写入哪些子采样，再配合 **子采样级深度测试**，最后由硬件 **Resolve** 平均得到屏幕像素。

下文聚焦 **原理、关键着色器实现**，以及本项目中 `StochasticTransparencyApp::init()` 里五行 OpenGL 状态在 **何时、何阶段** 起作用。

---

### 14.2 背景：为什么需要 OIT？

**画家算法**：透明物体从远到近绘制，每片元做

$$C_{\text{final}} = C_{\text{src}} \cdot \alpha + C_{\text{dst}} \cdot (1 - \alpha)$$

痛点：排序贵、无法处理循环重叠、与深度缓冲难协作。

**OIT（Order-Independent Transparency）** 不依赖绘制顺序。本项目采用 **Stochastic Transparency**：把 Alpha 当作「每个 MSAA 子采样被保留的概率」，而非混合权重。

| 方法 | 排序 | 本项 |
|------|------|------|
| Alpha 混合 | 需要 | — |
| Stochastic Transparency | **不需要** | **采用** |

---

### 14.3 原理：子采样掷骰子

核心思想（McGuire & Bavoil, HPG 2013）：**Alpha = coverage = 子采样保留概率**。

#### 14.31 三步直觉

**① 每个子采样掷骰子（片元着色器）**

```
  coverage = 0.6（纹理 Alpha）示意 8 个子采样

  子采样:    s0    s1    s2    s3    s4    s5    s6    s7
  随机数 r:  0.23  0.71  0.45  0.88  0.12  0.55  0.39  0.94
  r < 0.6?   ✓     ✗     ✓     ✗     ✓     ✓     ✓     ✗
  gl_SampleMask: 1  0  1  0  1  1  1  0   → 约 60% 位为 1
```

**② 深度测试：同一子采样上，近的赢**

```
  子采样 s2：远片元 A 先写 → 近片元 B 后写
  B 深度更近 → 在 s2 上覆盖 A（无需对物体排序）
```

**③ MSAA Resolve：对「亮着」的子采样求平均 → 近似半透明**

```
  [R][--][R][--][R][R][R][--]  →  Resolve  →  约 0.5×红色
```

#### 14.32 一帧内数据流（与本项目对应）

```
  initWindow: GLFW_SAMPLES=16     → 创建多采样帧缓冲（前置条件）
       ↓
  init(): 五行 GL 状态            → 整段渲染过程的全局规则（见 §4）
       ↓
  beginFrame: glClear 颜色+深度   → 每帧清空 MSAA FBO
       ↓
  renderScene: 4 次 Draw（无序）  → 片元写 gl_SampleMask + 颜色
       │                            深度测试/写入在固定管线阶段执行
       ↓
  SwapBuffers                     → MSAA Resolve → 显示器
```

---

### 14.4 关键实现：片元着色器

`resources/quad.frag` 是算法核心：

```glsl
vec4 color = texture(texture_diffuse, textureCoord);
float coverage = color.w;

uint randMask = 0u;
for (int i = 0; i < sampleCnt; i++) {
    vec2 seed = vec2(i, frameID);
    float r = fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
    if (r < coverage)
        randMask |= (1u << i);
}
gl_SampleMask[0] = int(randMask);
FragColor = color;
```

| 符号 | 来源 | 作用 |
|------|------|------|
| `coverage` | 纹理 Alpha | 伯努利试验成功概率 |
| `sampleCnt` | CPU 传 `GL_MAX_SAMPLES` | 掷骰子次数 = MSAA 采样数 |
| `frameID` | 每物体递增 `% 4` | 随机种子，避免重叠面 mask 完全相同 |
| `gl_SampleMask` | 片元输出 | 位为 1 的子采样才允许写入颜色/深度 |

CPU 侧每帧对 Spot、蓝/绿/红窗各 `Draw` 一次，**不排序**；`beginFrame` 只清一次屏，物体之间 **不清深度**（`clearColorDepth = {false,false}`），深度在四次绘制间累积。

---

### 14.5 五行 GL 状态：在流程中何时、扮演什么角色

以下代码位于 `StochasticTransparencyApp::init()`，在 **首帧绘制之前调用一次**，之后 **每帧、每个片元** 的固定管线都受这些状态约束，直到被 `glDisable` 改掉（本项目不会关掉）。

```cpp
glEnable(GL_MULTISAMPLE);
glEnable(GL_SAMPLE_MASK);
glEnable(GL_DEPTH_TEST);
glDepthFunc(GL_LEQUAL);
glDepthMask(GL_TRUE);
```

另有一处 **必须先于上述状态生效** 的配置（`initWindow`）：

```cpp
glfwWindowHint(GLFW_SAMPLES, 16);  // 创建 16× MSAA 默认帧缓冲
```

没有多采样缓冲，后面五行中的「子采样」概念不存在。下文按 **OpenGL 管线时间顺序** 说明每一项。

#### 14.51 总览：状态 × 管线阶段

| 状态 / 配置 | 主要生效阶段 | 一句话 |
|-------------|--------------|--------|
| `GLFW_SAMPLES=16` | 上下文/ FBO 创建 | 提供 N 个子采样槽位 |
| `GL_MULTISAMPLE` | 光栅化 → Resolve | 打开多采样路径 |
| `GL_SAMPLE_MASK` | 片元后、写入前 | 允许片元用 mask 筛子采样 |
| `GL_DEPTH_TEST` | 片元后、写入前 | 按深度决定能否写入某 sample |
| `glDepthFunc(GL_LEQUAL)` | 深度测试瞬间 | 通过条件：新深度 ≤ 旧深度 |
| `glDepthMask(GL_TRUE)` | 深度测试通过后 | 允许更新深度缓冲 |

#### 14.52 `glEnable(GL_MULTISAMPLE)` —— 多采样路径的总开关

**何时设置**：`init()`，窗口已带 `GLFW_SAMPLES` 创建完毕之后。

**在哪些阶段起作用**：

1. **光栅化**：三角形覆盖一个像素时，不是只影响 1 个点，而是影响该像素的 **N 个子采样**（本项 N≤16）。
2. **片元着色**：在 MSAA 模式下，片元与 **子采样** 关联（具体是否「每子采样跑一次片元」取决于驱动与是否开启 sample shading；本 Demo 未开 `GL_SAMPLE_SHADING`，但 mask/深度仍按子采样语义工作）。
3. **写入**：颜色、深度写入 **多采样颜色/深度缓冲**（每个像素 N 份）。
4. **`SwapBuffers`（Resolve）**：硬件把 N 个子采样 **平均** 成 1 个显示像素——Stochastic Transparency 的「混合」 largely 发生在这里。

**若关闭**：退化为单采样，无法「按子采样保留/丢弃」，本算法失效。

#### 14.53 `glEnable(GL_SAMPLE_MASK)` —— 允许片元改写「写入资格」

**何时设置**：`init()`，且必须在片元里写 `gl_SampleMask` **之前** 启用。

**在哪些阶段起作用**：

- 发生在 **片元着色器执行完毕之后、颜色/深度实际写入 framebuffer 之前** 的「样本遮罩」阶段。
- 片元里 `gl_SampleMask[0] = randMask`：只有 mask 中为 1 的 bit，该子采样才 **允许** 接收本片元的 `FragColor` 和深度。
- 与 `coverage` 掷骰子直接对应：**先** 用随机决定哪些 sample「有资格写」，**再** 对这些 sample 做深度测试。

**若关闭**：`gl_SampleMask` 写入被忽略，所有子采样都会尝试写入 → 半透明变成「全不透明片元」，失去随机透明度。

**依赖关系**：依赖 `GL_MULTISAMPLE`；单采样下无意义。

#### 14.54 `glEnable(GL_DEPTH_TEST)` —— 子采样上的前后关系

**何时设置**：`init()`；每帧 `beginFrame` 里 **不清** 深度开关，只 `glClear(DEPTH)`。

**在哪些阶段起作用**：

- **每个片元、每个通过 Sample Mask 的子采样**，将该子采样上的片元深度与 **多采样深度缓冲** 中对应 sample 的已存深度比较。
- 本 Demo 连续画 4 个物体：**同一子采样** 上，后绘制且更近的片元可以赢；远的被挡——这是在 **子采样粒度** 实现「谁在前」，从而 **无需对网格排序**。

**与 Stochastic 的配合**：

```
  片元到达 → Sample Mask 筛 sample → 深度测试筛 sample → 通过的 sample 写颜色+深度
```

**若关闭**：所有片元都写入，远近错乱，重叠透明完全错误。

#### 14.55 `glDepthFunc(GL_LEQUAL)` —— 深度比较规则

**何时设置**：`init()`，与 `GL_DEPTH_TEST` 同时生效。

**在哪些阶段起作用**：仅在 **深度测试执行的那一瞬间**。

- `GL_LEQUAL`：新片元深度 **≤** 缓冲中深度 → **通过**。
- 相等深度可通过（对共面或同一几何重复绘制更宽容）。
- 本 Demo 每帧从 `glClearDepth(1.0)` 开始，近处深度小，远处大。

**角色**：定义「什么叫 nearer」。Stochastic 只决定 **哪些 sample 参与竞争**；**谁赢** 由深度测试决定。

#### 14.56 `glDepthMask(GL_TRUE)` —— 是否写入深度缓冲

**何时设置**：`init()`。

**在哪些阶段起作用**：深度测试 **通过之后** 的写入阶段。

- `GL_TRUE`：通过的子采样 **更新** 多采样深度缓冲。
- 之后同一子采样上 **更远** 的片元会因深度测试失败而无法写入颜色。

**在本项目中的角色**：使「近处透明片元占住该 sample」在 **后续 Draw** 中仍成立（四物体共用同一深度缓冲、中间不清深度）。这是 **无序绘制** 仍能近似正确的前后关系的关键之一。

**若改为 `GL_FALSE`**：只测不写，后续片元无法被挡住，多层透明叠加会乱（传统透明常在对透明 pass 关深度写，本算法路径不同）。

#### 14.57 单行代码在「一帧四物体」中的时间线

```
帧开始
  glClear 颜色+深度                    ← 深度缓冲置远平面
  ─────────────────────────────────────────────────────────
  Draw Spot
    片元: 掷骰子 → gl_SampleMask       ← GL_SAMPLE_MASK + 片元 shader
          深度测试 LEQUAL              ← GL_DEPTH_TEST + glDepthFunc
          通过则写色+写深              ← GL_MULTISAMPLE 缓冲 + glDepthMask TRUE
  ─────────────────────────────────────────────────────────
  Draw 蓝窗 (frameID+1, 新随机 mask)
    同上；与 Spot 在重叠像素的同一 sample 上比深度
  ─────────────────────────────────────────────────────────
  Draw 绿窗、红窗 …
  ─────────────────────────────────────────────────────────
  SwapBuffers → MSAA Resolve           ← GL_MULTISAMPLE 解析到屏幕
帧结束
```

#### 14.58 五行与着色器分工（对照表）

| 层次 | 谁负责 | 做什么 |
|------|--------|--------|
| 窗口 | `GLFW_SAMPLES` | 创建 N 子采样缓冲 |
| 全局状态 | `GL_MULTISAMPLE` | 走多采样 + Resolve |
| 全局状态 | `GL_SAMPLE_MASK` | 允许片元筛 sample |
| 片元 shader | `gl_SampleMask = randMask` | 按 Alpha 随机保留 sample |
| 全局状态 | `GL_DEPTH_TEST` + `LEQUAL` | 近的赢 |
| 全局状态 | `glDepthMask(TRUE)` | 赢的 sample 写下深度，挡住远的 |
| 硬件 | Resolve | 子采样平均 ≈ 透明感 |

---

### 14.6 设计取舍（简短）

- **噪声**：采样数有限（16）会有颗粒；可时间累积或 TAA。
- **近似**：非物理精确混合；要精确需 Linked List / Depth Peeling。
- **Per-sample shading**：未显式 `glMinSampleShading(1.0)`，极端情况下驱动行为需实机验证。

---

### 14.7 总结

| 问题 | 答案 |
|------|------|
| 原理是什么？ | Alpha = 子采样保留概率；mask + 深度 + Resolve |
| 关键代码在哪？ | `quad.frag` 中 `gl_SampleMask` 循环 |
| 五行 GL 状态何时设？ | `init()` 一次，作用于之后每帧整条管线 |
| 各自管什么？ | MSAA 提供 sample；MASK 筛 sample；深度测/写决定远近 |
| 为何能不排序？ | 前后关系在 **每个子采样** 上由深度解决，透明度由 **随机 mask + Resolve 平均** 近似 |

---

### 14.8 参考

- McGuire & Bavoil, *Stochastic Transparency*, HPG 2013  
- 本项目：`src/StochasticTransparencyApp.cpp`（init 53–57 行）、`resources/quad.frag`



## 15. 附录：通用管线与状态机机制（摘自通用知识点汇总）

### 附录：多重采样抗锯齿开关 (glEnable(GL_MULTISAMPLE))
配合窗口初始化时的 MSAA 采样点（如 GLFW_SAMPLES=4），激活硬件级片元覆盖率多采样计算，有效消除多边形边界锯齿。

