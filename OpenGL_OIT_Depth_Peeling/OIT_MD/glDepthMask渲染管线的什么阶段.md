`glDepthMask(GL_TRUE)` **本身不是某一 Pass 的名字**，而是告诉 OpenGL：**深度测试通过之后，要不要把片元的深度写进深度缓冲**。

---

## 1. 在本项目流程里：什么时候是 `TRUE`？

| 阶段                                            | `glDepthMask`                             | 是否写深度                             |
| ----------------------------------------------- | ----------------------------------------- | -------------------------------------- |
| **`initPeelBuffers()`**                         | `GL_TRUE`                                 | 为后面 Peel 打开写深度                 |
| **Peel Pass**（`drawSceneLayer` + peel 着色器） | `GL_TRUE`                                 | **会写** → `fboPeel_` 上绑定的深度纹理 |
| **Blend Pass**（全屏 quad 混合）                | `GL_FALSE`                                | **不写**                               |
| **Blend 结束后**                                | 恢复 `GL_TRUE`                            | 给下一层 Peel 用                       |
| **`compositeToScreen()`**                       | 一般是 TRUE，但全屏合成通常不依赖深度竞争 | 对本 demo 影响小                       |

所以：**真正“写入深度缓冲”发生在每一遍 Peel 绘制场景时**，不是 Blend、也不是 Final 合成。

对应代码关系：

```
initPeelBuffers()  → glDepthMask(GL_TRUE)
peelAndBlend 循环:
    Peel  drawSceneLayer     → 深度测试 ON + DepthMask TRUE  → 写深度
    Blend modelQuad_         → glDepthMask(GL_FALSE)           → 不写
    恢复                     → glDepthMask(GL_TRUE)
```

---

## 2. 在渲染管线的什么阶段写？

OpenGL 固定功能里，深度写入发生在 **片元阶段之后、帧缓冲操作（Per-Fragment Operations）** 里，顺序大致是：

```
顶点着色器 → 光栅化 → 片元着色器 → 【逐片元操作】→ 帧缓冲
                                      │
                                      ├─ Scissor
                                      ├─ 片元着色器 discard? → 丢弃则后面都不做
                                      ├─ 深度测试 (glDepthFunc GL_LESS)
                                      │     失败 → 不写颜色/深度（通常）
                                      │     通过 ↓
                                      ├─ glDepthMask(GL_TRUE)? 
                                      │     TRUE  → 把 gl_FragCoord.z 写入深度附件
                                      │     FALSE → 深度测试可跑，但不更新深度缓冲
                                      ├─ 模板测试（若开启）
                                      └─ 混合 (glBlend) → 写颜色
```

要点：

| 项目                  | 说明                                                         |
| --------------------- | ------------------------------------------------------------ |
| **谁产生深度值**      | 光栅化插值 + 投影后，片元深度即 **`gl_FragCoord.z`**（窗口空间 [0,1]） |
| **谁决定写不写**      | **`glDepthMask`**；`FALSE` 时深度测试仍可能执行，但 **深度缓冲不变** |
| **谁决定过不过**      | **`glDepthFunc(GL_LESS)`**；Peel 里更近的片元才能通过并写入  |
| **和 discard 的关系** | `discard` 在 **片元着色器里**，执行后 **不会** 进入深度测试/混合，也就 **不会写深度** |

Peel 里两层筛选：

- **片元着色器 `discard`**：和 `texture_depth` 比，去掉已剥层（跨 Pass）
- **深度测试 + `glDepthMask(TRUE)`**：同 Pass 同像素留最近一片，并把深度 **写进当前 output 深度纹理**

---

## 3. 写到哪里？

Peel 时 FBO 绑的是 `fboPeel_.fbo`，深度附件每轮可能是：

```cpp
depthTexture(outputDepthIndex_)  // fboPeel_.depth 或 fboAccum_.depth（乒乓）
```

通过深度测试且 `DepthMask=TRUE` 的片元 → **`gl_FragCoord.z` 写入该深度纹理对应像素** → 下一层 Peel 当 `texture_depth` 采样。

颜色写入 **同一 Peel Pass** 的 `fboPeel_.color`，和深度是 **同一片元、同一时刻** 在帧缓冲阶段分别写入两个附件。

---

## 4. 和 `glClearDepth` 的关系

- **写深度**：Peel 绘制时，通过测试的片元 **覆盖** 深度缓冲里的值  
- **清深度**：`glClear(GL_DEPTH_BUFFER_BIT)` 在 Pass **开始前** 整屏重置（如 Peel 前 `glClearDepth(1.0f)`）

`glDepthMask` 只管 **绘制过程中的增量写入**，不管 clear。

---

## 5. 一句话

**`glDepthMask(GL_TRUE)` 表示：在 Peel 阶段，片元着色器未 `discard`、且通过 `GL_LESS` 深度测试后，在管线末端的 **深度缓冲更新步骤** 把 `gl_FragCoord.z` 写入 FBO 的深度附件；Blend 阶段设为 `FALSE`，避免全屏 quad 破坏乒乓用的深度纹理。**