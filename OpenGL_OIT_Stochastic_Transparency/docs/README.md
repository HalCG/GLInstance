# OpenGL OIT 之 Stochastic Transparency 实现

> 源码地址：[GitHub 仓库](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_OIT_Stochastic_Transparency)

---

## 0. 前言

在前两篇中，我们分别介绍了 [Linked List OIT](../../OpenGL_OIT_Linked_list/docs/README.md)（GPU 端链表收集排序）和 [Depth Peeling OIT](../../OpenGL_OIT_Depth_Peeling/docs/README.md)（多次 Pass 逐层剥离）。这两种方案都是**确定性**的——每个像素的最终颜色是精确计算的。

Stochastic Transparency（随机/各向同性透明度）走了一条完全不同的路：**用随机化替代排序**。它不精确，但实现极其简单，只需要一个 Pass 和一个 Shader。

> 参见：[OpenGL_OIT_传统Alpha混合与三种方案对比.md](../../docs/OpenGL_OIT_传统Alpha混合与三种方案对比.md)。

### 专题文档（本目录）

| 文档 | 内容 |
| :--- | :--- |
| [SampleMask投射与深度测试.md](./SampleMask投射与深度测试.md) | **核心**：半透明=往 MSAA 孔位投射；为何深度开着仍半透；同孔 Z 裁决 |
| [五行GL状态与管线阶段.md](./五行GL状态与管线阶段.md) | `init()` 五行状态何时、在哪一阶段起作用 |
| [Shader与渲染循环.md](./Shader与渲染循环.md) | `quad.frag` / App 绘制循环与数据流 |

### 与 Depth Peeling / Linked List 的最终效果差异（原因）

| | Depth Peeling / Linked List | Stochastic（本工程） |
|--|--|--|
| 叠色 | Over / Front-to-Back，带**透射权重** | MSAA Resolve：**采样点等权平均** |
| α 相同时 | 近处按公式更「压得住」远处，更接近物理半透明 | 各层同概率抢孔；近远只在**同一孔**上靠 Z 裁决 |
| 半透明来源 | 混合公式中的 α | α → 随机覆盖率，再平均 |
| 画面差异根因 | 多层颜色按 \((1-\alpha)\) 链式衰减合成 | 每孔只留一个赢家，再对孔**算术平均**——没有透射链 |

**为何最终看起来不一样（数值直觉）**：两层都 α=0.5，近 \(C_n\)、远 \(C_f\)（忽略更远背景）。

- DP / LL（Over 近→远简化）：\(C \approx \alpha C_n + (1-\alpha)(\alpha C_f)=\mathbf{0.5}\,C_n+\mathbf{0.25}\,C_f\)（远处还被近处透射压掉一截）。
- Stochastic（期望）：约一半孔是 \(C_n\)、一半是 \(C_f\) → Resolve \(\approx \mathbf{0.5}\,C_n+\mathbf{0.5}\,C_f\)。远处贡献更大，且孔上「全有或全无」→ **颗粒噪声**；α 再大，也只是多占孔，仍不是 Over 那种连续加权。

一句话：前两种是**加权合成（近优先）**；本方案是**覆盖率统计 + 等权 Resolve**（便宜近似，易噪、远处偏亮/偏抢眼）。

软件 / 硬件分工：`quad.frag` 按 α 写 `gl_SampleMask`（**GLSL 初值全 1**，此处改成随机子集）；硬件按采样点做 Z 与写色，出示前 Resolve 平均。`GL_SAMPLE_MASK` 管**绘制时**覆盖（关闭则 FS 赋值无效）；Resolve 是**上屏前**降采样——二者不是互相替代。Mask 定覆盖，Z 定同点胜者。

半透明可理解为往 MSAA 采样点**投射**：掩码为 1 才能投到该点；不做掩码处理则近处对全部点投射并写深，后面被直接盖住。深度仍开着能半透，是因为只投射一部分点——详见 [SampleMask投射与深度测试.md](./SampleMask投射与深度测试.md)。

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

硬件 MSAA 自动将这 16 个样本混合成最终颜色，近似得到正确结果。**不需要排序**——每个采样点上至多留下一个胜者（由覆盖掩码 + 深度裁决），再等权平均。同孔冲突时的规则见 [SampleMask投射与深度测试.md](./SampleMask投射与深度测试.md)。

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

- `gl_SampleMask` 为数组：`[0]` 管 sample 0..31，`[1]` 管 32..63……本工程 16x 只用 `[0]`
- `[0]` 内 bit i = 1 → 采样点 i 被当前片元覆盖；= 0 → 本片元不写该点

**这正是 Stochastic Transparency 的核心**：用 alpha 值作为概率，决定每个样本是否被覆盖。

为何「投孔」+ 深度开着仍半透、同孔 Z 裁决：见 [SampleMask投射与深度测试.md](./SampleMask投射与深度测试.md)。`init()` 五行状态：见 [五行GL状态与管线阶段.md](./五行GL状态与管线阶段.md)。

---

## 3. 算法流程

### 3.1 整体流程

```
┌─────────────────────────────────────────────────────────────────────┐
│                        每帧渲染循环                                  │
│                                                                     │
│  Step 1: 初始化 MSAA / Sample Mask / 深度（见专题「五行 GL 状态」）   │
│                            │                                        │
│                            ▼                                        │
│  Step 2: 逐个对象渲染（同一 Pass）                                   │
│    for each object:                                                 │
│      设 MVP、sampleCnt、frameID                                     │
│      FS: α → 随机掩码 → gl_SampleMask[0]；FragColor = color        │
│                            │                                        │
│                            ▼                                        │
│  Step 3: glfwSwapBuffers → 硬件 MSAA Resolve（样本等权平均）         │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.2 随机数与覆盖率

```glsl
float r = fract(sin(dot(vec2(sampleIdx, frameID), vec2(12.9898, 78.233))) * 43758.5453);
if (r < coverage)  // coverage = color.a
    randMask |= (1u << i);
```

例如 alpha = 0.5，16 个样本中大约一半会被覆盖。实现细节见 [Shader与渲染循环.md](./Shader与渲染循环.md)。

---

## 4. 深度与投射（摘要）

Stochastic **不排序，但深度测试/写入仍开启**。半透明靠「只投射一部分采样点」；深度只在投中的点上比远近。

不做 Sample Mask → 近处投满全部孔并写 Z → 像不透明盖住。完整说明与裁决表：[SampleMask投射与深度测试.md](./SampleMask投射与深度测试.md)。状态配置：[五行GL状态与管线阶段.md](./五行GL状态与管线阶段.md)。

```cpp
glEnable(GL_MULTISAMPLE);
glEnable(GL_SAMPLE_MASK);
glEnable(GL_DEPTH_TEST);
glDepthFunc(GL_LEQUAL);
glDepthMask(GL_TRUE);
// glfwWindowHint(GLFW_SAMPLES, 16);
```

---

## 5. 随机种子（frameID）的作用

### 5.1 为什么需要不同的种子？

如果所有对象用相同的种子，每个样本的随机数序列就完全一样——所有对象会在**完全相同的样本**上竞争，结果不正确。不同 `frameID` 给出独立序列。

### 5.2 本实现中的 frameID 策略

```cpp
static int frameID = 0;
int modelCnt = 4;
shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
```

- 4 个对象（1 Spot + 3 Window），frameID 在 0–3 循环
- 种子按物体固定 → 结果**静态**（不随时间变）。若要时间噪声，可每帧随机化 `frameID`

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

> **用概率替代排序，用 MSAA 替代精确混合。**

1. **gl_SampleMask**：逐样本控制片段覆盖，用 alpha 作为概率（往 MSAA 孔位投射）
2. **MSAA 硬件**：采样点深度裁决 + Resolve 等权平均
3. **单 Pass**：所有对象同 Pass 绘制，深度处理同点遮挡
4. **frameID**：区分对象随机序列

实现与逐行说明：[Shader与渲染循环.md](./Shader与渲染循环.md)。
