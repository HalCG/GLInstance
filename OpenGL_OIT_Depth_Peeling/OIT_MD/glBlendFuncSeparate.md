这两行是在配置 **Blend Pass** 里把「当前剥离层」叠进 `fboAccum_` 的混合方式，实现 **从近到远（Front-to-Back）** 的半透明累积。

```289:290:e:\opengl\liuhaonian\OpenGL_OIT_Depth_Peeling\src\DepthPeelingApp.cpp
    glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO,
                        GL_ONE_MINUS_SRC_ALPHA);
```

---

## `glBlendFuncSeparate` 是什么

它分别指定 **RGB** 和 **Alpha** 的混合因子（普通 `glBlendFunc` 对 RGB/Alpha 用同一套）：

```cpp
glBlendFuncSeparate(源RGB因子, 目标RGB因子, 源Alpha因子, 目标Alpha因子);

// 每个通道：
结果 = 源 × 源因子 + 目标 × 目标因子
```

这里（`src` = 当前剥离层 `fboPeel_.color`，`dst` = 累积缓冲 `fboAccum_.color`）：

| 通道      | 公式                            |
| --------- | ------------------------------- |
| **RGB**   | `C_out = C_src × A_dst + C_dst` |
| **Alpha** | `A_out = A_dst × (1 - A_src)`   |

对应：

- RGB：`GL_DST_ALPHA` × 源 + `GL_ONE` × 目标  
- Alpha：`GL_ZERO` × 源 + `GL_ONE_MINUS_SRC_ALPHA` × 目标  

---

## 在干什么（直观理解）

每一层 Peel 得到 `(C_src, A_src)`（带光照的颜色 + 纹理 alpha）。  
`fboAccum_` 帧初大致是 **黑底 + Alpha=1**（`initPeelBuffers` 里 `glClearColor(0,0,0,1)`）。

**Alpha 通道不表示“这一层的透明度”**，而是表示 **“背后还剩多少没被挡住的权重”**（类似透射率）。

### 第 1 层（近处玻璃）

- `C_out = C₁ × 1 + 0 = C₁`  
- `A_out = 1 × (1 - α₁) = 1 - α₁`  

### 第 2 层（更远）

- `C_out = C₂ × (1-α₁) + C₁`  
- `A_out = (1-α₁) × (1-α₂)`  

### 第 n 层后

\[
C_{acc} = C_1 + C_2(1-\alpha_1) + C_3(1-\alpha_1)(1-\alpha_2) + \cdots
\]

这就是 **从近到远** 的 Under 累积：近的先写上，远的乘上「前面已经挡掉多少」再加进去。  
和绘制顺序无关，因为每层是 **单独剥出来再按层混合** 的。

---

## 为什么要用 `Separate`（RGB 和 Alpha 分开）

若只用 `glBlendFunc(GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` 之类，RGB 和 Alpha 会用同一套因子，**无法**同时做到：

- RGB：`C_src * A_dst + C_dst`  
- Alpha：`A_dst * (1 - A_src)`  

Depth Peeling 需要 **用 Alpha 通道存“剩余透射”**，RGB 存 **已累积颜色**，所以必须 `glBlendFuncSeparate`。

---

## 和后面 `compositeToScreen` 的关系

Blend 结束后 `fboAccum_` 里是 `(C_acc, A_remain)`。  
`depth_peeling_final.frag` 里：

```glsl
FragColor = frontColor + vec4(background_color, 1.0) * frontColor.a;
```

`frontColor.a` 就是 **还没被玻璃挡满的权重**，用来把 **背景色** 叠到最远处。  
若 Blend 不用这套公式，最终背景和玻璃的衔接会错。

---

## 和常见 Alpha 混合的对比

常见 **从后往前** Over（画家算法）：

```cpp
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
// C = C_src*α + C_dst*(1-α)
```

本项目是 **从前往后** Under，且 **Alpha 存透射权重**，所以是：

```cpp
glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
```

---

## 一句话

这两行在 Blend Pass 里实现：**当前层颜色 × 累积缓冲的 Alpha + 已有颜色**，同时 **Alpha 乘以 (1 - 当前层 Alpha)**，从而按 **近→远** 正确叠多层半透明，并为最后一遍加背景留下 `frontColor.a`。