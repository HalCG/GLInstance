`peelAndBlend()` 是整帧 **OIT 深度剥离** 的核心：在循环里反复做两件事——**剥出一层**（Peel Pass）和 **混入累积缓冲**（Blend Pass）。下面按执行顺序说明，并标出关键逻辑。

---

## 1. 函数在整帧中的位置

每帧调用顺序：

```
initPeelBuffers()   // 清空 FBO、重置 input/output 深度索引
    ↓
peelAndBlend()      // ← 本文
    ↓
compositeToScreen() // 把 fboAccum_ 合成到屏幕
```

`initPeelBuffers()` 为 `peelAndBlend()` 做准备：两个 FBO 颜色/深度清 0，深度测试 `GL_LESS`，`inputDepthIndex_=0`，`outputDepthIndex_=1`。

---

## 2. 两个 FBO 各干什么

| 对象                 | 角色                 | 本函数中的用途                                               |
| -------------------- | -------------------- | ------------------------------------------------------------ |
| `fboAccum_`（FBO_0） | **累积**最终透明颜色 | Blend 写入目标；深度纹理参与 **乒乓**                        |
| `fboPeel_`（FBO_1）  | **当前层**剥离结果   | Peel 颜色始终写 `fboPeel_.color`；深度附件随 `outputDepthIndex_` 切换 |

```cpp
GLuint depthTexture(int index) const {
  return index ? fboPeel_.depth.id : fboAccum_.depth.id;
}
// index=0 → fboAccum_.depth
// index=1 → fboPeel_.depth
```

**关键**：颜色累积只在 `fboAccum_.color`；剥离层颜色总在 `fboPeel_.color`；**深度**在两个 FBO 的深度纹理之间 **乒乓**，避免同一纹理既当 FBO 附件又被采样。

---

## 3. 循环整体结构

```cpp
for (int layer = 0; layer < AppConfig::kMaxDepthPeelLayers; ++layer) {
    // A. 准备剥离 FBO
    // B. Peel：画场景 → 得到“当前最前一层”
    // C. 统计 sampleCount，判断是否还有层
    // D. Blend：把当前层 Front-to-Back 混入 fboAccum_
    // E. 乒乓交换深度索引
    // F. sampleCount==0 → break
}
```

`kMaxDepthPeelLayers = 10` 是上限；真正层数由 **`sampleCount <= 0`** 提前结束。

---

## 4. 阶段 A：准备剥离 FBO（270–276 行）

```266:276:e:\opengl\liuhaonian\OpenGL_OIT_Depth_Peeling\src\DepthPeelingApp.cpp
    glBindFramebuffer(GL_FRAMEBUFFER, fboPeel_.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           depthTexture(outputDepthIndex_), 0);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
```

要点：

1. **重绑深度附件**到 `depthTexture(outputDepthIndex_)`，本层新深度写入 **乒乓中的“写侧”**。
2. 颜色清 `(0,0,0,0)`，深度清 **`1.0`（最远）**，配合 `GL_LESS`，硬件深度测试先接受“更近”的片元；更细的“第几层”由片元着色器 `discard` 决定。
3. 只清 `fboPeel_`，**不动** `fboAccum_` 里已累积的颜色。

---

## 5. 阶段 B：Peel Pass（278–281 行）— 最关键

```278:281:e:\opengl\liuhaonian\OpenGL_OIT_Depth_Peeling\src\DepthPeelingApp.cpp
    glBeginQuery(GL_SAMPLES_PASSED, queryId_);
    drawSceneLayer(*shaderPeel_, fboPeel_.fbo, inputDepthIndex_,
                   outputDepthIndex_);
    glEndQuery(GL_SAMPLES_PASSED);
```

`drawSceneLayer` 会：

- 渲染到 `fboPeel_.fbo`（颜色 → `fboPeel_.color`）；
- 绑定 `texture_depth` = `depthTexture(inputDepthIndex_)`（**上一层的深度历史**）；
- 绘制 Spot + 三块半透明玻璃。

片元着色器里的剥离逻辑：

```glsl
float frontDepth = texture(texture_depth, uv).r;
if (gl_FragCoord.z <= frontDepth) {
    discard;  // 已剥过或更近的层 → 丢弃
}
// 否则：这是“剩余片元里最前”的一层，输出颜色+alpha
```

| 概念     | 含义                                                         |
| -------- | ------------------------------------------------------------ |
| 第 0 层  | `input` 来自 `fboAccum_.depth`（帧初清 0），所有 `z > 0` 可参与，得到 **最前** 一层 |
| 第 k 层  | `input` 存前 k 层深度，`z <= frontDepth` 的丢弃，得到 **下一层** |
| 深度写入 | 写到 `outputDepthIndex_` 对应深度纹理，供下一层 `input` 读取 |

**剥离不依赖绘制顺序**，而依赖 **深度纹理 + discard**。

---

## 6. 阶段 C：`waitSampleCount()`（283 行）

读取 `GL_SAMPLES_PASSED`：本层 Peel 里 **通过深度测试的采样数**。

- `sampleCount > 0`：还有新几何参与剥离 → 继续循环；
- `sampleCount == 0`：没有更深层 → `break`。

（实现上是 CPU 忙等 GPU，教学用足够。）

---

## 7. 阶段 D：Blend Pass（285–294 行）— 第二关键

```285:294:e:\opengl\liuhaonian\OpenGL_OIT_Depth_Peeling\src\DepthPeelingApp.cpp
    shaderBlend_->use();
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO,
                        GL_ONE_MINUS_SRC_ALPHA);

    modelQuad_->Draw(*shaderBlend_, fboAccum_.fbo,
                    {{"texture_diffuse", fboPeel_.color.id}}, {},
                    GL_TRIANGLES, {false, true});
```

### 7.1 状态含义

| 设置                                   | 原因                               |
| -------------------------------------- | ---------------------------------- |
| `GL_BLEND` 开启                        | 做 Front-to-Back 累积              |
| `glDepthMask(GL_FALSE)` + 关闭深度测试 | 全屏 quad 覆盖写入，不参与深度竞争 |
| `glBlendFuncSeparate(...)`             | 专用 Under 累积公式                |

### 7.2 混合公式（重点）

设累积为 `(C_dst, A_dst)`，当前剥离层为 `(C_src, A_src)`：

- **RGB**：`C_dst' = C_src * A_dst + C_dst`
- **Alpha**：`A_dst' = A_dst * (1 - A_src)`

`fboAccum_` 帧初为 `(0,0,0,1)`，逐层从 **近到远** 叠半透明色；**Alpha 通道表示“还剩多少未覆盖的透射权重”**，不是单层的 alpha。

Blend 着色器只采样 `fboPeel_.color`，混合由固定管线完成。

### 7.3 `{false, true}`

- **不清颜色**：保留已累积的 RGBA；
- **清深度**：`fboAccum_` 的深度附件每轮被清掉；深度历史在 **另一张** 深度纹理（乒乓的 read 侧），不依赖 accum 深度里旧数据。

---

## 8. 阶段 E：乒乓交换（296–297 行）

```296:297:e:\opengl\liuhaonian\OpenGL_OIT_Depth_Peeling\src\DepthPeelingApp.cpp
    inputDepthIndex_ = (inputDepthIndex_ + 1) % 2;
    outputDepthIndex_ = (outputDepthIndex_ + 1) % 2;
```

下一层：**读** 刚写入的深度，**写** 另一侧深度纹理。

### 前两层的具体数据流

**Layer 0**（`input=0`, `output=1`）：

```
读 depth: fboAccum_.depth (初值 0)
写 depth: fboPeel_.depth
写 color: fboPeel_.color → Blend → fboAccum_.color
```

**Layer 1**（交换后 `input=1`, `output=0`）：

```
读 depth: fboPeel_.depth (Layer 0 写入)
写 depth: fboAccum_.depth (Blend 时已清，Peel 前再清 1.0)
写 color: fboPeel_.color → 再 Blend 到 fboAccum_
```

```
        ┌─────────────┐     Peel      ┌─────────────┐
        │ fboAccum_   │ ──读 depth──► │ 片元 discard │
        │  .depth     │               │  比较        │
        └─────────────┘               └──────┬──────┘
        ┌─────────────┐ ◄──写 depth──────────┘
        │ fboPeel_    │
        │  .color     │ ──Blend──► fboAccum_.color（累积）
        └─────────────┘
              每层结束 swap 读/写索引
```

---

## 9. 阶段 F：恢复状态 + 提前退出（299–305 行）

```299:305:e:\opengl\liuhaonian\OpenGL_OIT_Depth_Peeling\src\DepthPeelingApp.cpp
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    if (sampleCount <= 0) {
      break;
    }
```

恢复深度测试，避免影响后续 `compositeToScreen()`。无新层则退出，否则继续直到 10 层上限。

---

## 10. 一帧内时间线（简图）

```
layer=0: [清 Peel] → [Peel 第0层] → [Blend→Accum] → swap
layer=1: [清 Peel] → [Peel 第1层] → [Blend→Accum] → swap
...
layer=n: [Peel] → sampleCount==0 → break

结果: fboAccum_.color = 所有透明层 Front-to-Back 累积
      compositeToScreen() 再 + 背景色
```

---

## 11. 三个易混点

**① Peel 与 Blend 分工**

- Peel：解决 **“哪一层”**（深度剥离）；
- Blend：解决 **“怎么叠色”**（顺序无关的累积）。

**② `discard` vs 深度测试**

- 硬件深度：`GL_LESS`，同层竞争；
- `discard`：与 **历史剥离深度** 比较，实现“剥下一层”。

**③ 为何 Blend 时关深度测试**

全屏 quad 若开深度测试可能写不进去；累积缓冲只需颜色混合，深度在 Peel 阶段维护。

---

## 12. 一句话总结

`peelAndBlend()` 在循环中反复执行：**用上一层的深度纹理剥出当前最前的一层 → 用特殊混合方程把该层并入 `fboAccum_` → 乒乓交换深度读写 → 直到没有新层**。透明物体的正确顺序由 **多层剥离 + Front-to-Back 混合** 保证，而不是由 `draw` 调用顺序决定。

若要对照单步调试，可在 `layer==0/1` 时用 RenderDoc 看 `fboPeel_.color` 和 `texture_depth`，会最直观。