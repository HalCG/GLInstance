# Front-to-Back 混合详解：src / dst、四参数与公式对应

> 对应代码：[`DepthPeelingApp.cpp`](../src/DepthPeelingApp.cpp) 中 Blend Pass 的  
> `glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA)`  
> 短文入口：[glBlendFuncSeparate.md](./glBlendFuncSeparate.md) · 五项状态：[Blend阶段_GL状态五项配置.md](./Blend阶段_GL状态五项配置.md)

说明：src / dst 定义、四参数与公式对应、以及与 Linked List Back-to-Front Over 的差异。

---

## 1. 先记住：src / dst 跟「近/远」无关

OpenGL 混合里两个名字是 **按这一次 Draw 定义的**，不是按深度远近：

| 名字 | 官方含义 | 本项目 Blend Pass |
| :--- | :--- | :--- |
| **src（源 / Source）** | **这一次绘制**，片元着色器刚输出的颜色 | `depth_peeling_blend.frag` 采样到的 **`fboPeel_.color`**（当前刚剥出的一层） |
| **dst（目标 / Destination）** | **帧缓冲里已经存在的旧颜色** | 正在写入的 **`fboAccum_.color`**（前面几层已经累进去的结果） |

```
全屏 quad Draw → 目标 FBO = fboAccum_

  片元着色器输出  ──────────►  src（新来的：本层 peel）
  fboAccum_ 旧像素 ─────────►  dst（旧的：已累积）
           │
           ▼
     result = src×srcFactor + dst×dstFactor
           │
           ▼
     写回 fboAccum_ 同一像素
```

**近 / 远** 由 Depth Peeling「先剥谁、先混谁」决定（本工程是近→远）。  
**src / dst** 只表示「新片元」和「缓冲里旧值」。不要把 src 理解成「远处」、dst 理解成「近处」。

`blend.frag` 本身几乎不做混合，只负责把 peel 颜色交出去：

```glsl
FragColor = texture(texture_diffuse, textureCoord);  // → 成为 src
```

真正的公式全在 `glBlendFuncSeparate` 的四个因子里。

---

## 2. 通用混合公式（默认 GL_FUNC_ADD）

```text
RGB' = RGB_src × srcRGB因子  +  RGB_dst × dstRGB因子
A'   = A_src   × srcAlpha因子 +  A_dst   × dstAlpha因子
```

`glBlendFuncSeparate` 的四个参数 **按顺序** 就是上面四个因子：

```cpp
glBlendFuncSeparate(
    /* srcRGB   */  ...,
    /* dstRGB   */  ...,
    /* srcAlpha */  ...,
    /* dstAlpha */  ...);
```

普通 `glBlendFunc(srcFactor, dstFactor)` 会对 **RGB 和 Alpha 用同一套** 因子，本项目做不到「RGB 一套、Alpha 另一套」，所以必须用 Separate。

---

## 3. 本项目四个参数 ↔ 公式（逐项对照）

```cpp
glBlendFuncSeparate(
    GL_DST_ALPHA,            // ① srcRGB
    GL_ONE,                  // ② dstRGB
    GL_ZERO,                 // ③ srcAlpha
    GL_ONE_MINUS_SRC_ALPHA); // ④ dstAlpha
```

| 序号 | 参数位置 | 枚举 | 实际乘数是什么 |
| :---: | :--- | :--- | :--- |
| ① | srcRGB | `GL_DST_ALPHA` | **目标**的 Alpha：\(A_{dst}\)（剩余透射） |
| ② | dstRGB | `GL_ONE` | 常数 **1**（旧颜色原样保留） |
| ③ | srcAlpha | `GL_ZERO` | 常数 **0**（结果 A 不直接等于本层 α） |
| ④ | dstAlpha | `GL_ONE_MINUS_SRC_ALPHA` | \(1 - A_{src}\)（本层挡住后还剩多少透射比例） |

代入通式得到：

\[
\begin{aligned}
C' &= C_{src}\cdot A_{dst} + C_{dst}\cdot 1 \\
A' &= A_{src}\cdot 0 + A_{dst}\cdot(1 - A_{src}) = A_{dst}\,(1 - A_{src})
\end{aligned}
\]

### 3.1 用白话拆开（避免绕晕）

**颜色 RGB：**

- 源颜色 \(C_{src}\)：乘 **目标的 Alpha** \(A_{dst}\)（前面还剩多少「能透过来的权重」），再加进缓冲。
- 目标颜色 \(C_{dst}\)：**保持累加**（乘 1）。

不是「dst 作用之后才得到源的 alpha」。源的 α 在 peel 着色器里已经写在 `FragColor.a` 里了；这里 RGB 用的是 **dst 的 α**。

**Alpha 通道（在本工程里 = 剩余透射，不是「当前层透明度」）：**

- 源 Alpha \(A_{src}\)：**不当最终 A 存进去**（乘 0），但仍用来算 \(1-A_{src}\)。
- 目标 Alpha \(A_{dst}\)：乘 \(1-A_{src}\)，得到新的剩余透射。

所以：

- 「源的 alpha 不要了」——若指 **结果 A ≠ \(A_{src}\)**，对；
- 但 \(A_{src}\) 仍参与 \(A' = A_{dst}(1-A_{src})\)，不是完全没用。

### 3.2 为什么叫 Front-to-Back Under？

- **Front-to-Back**：混合顺序是 **近 → 远**（先混离眼睛近的层）。
- **Under**：新层按「剩余透射」垫到已有颜色「下面/后面」继续累加；Alpha 存的是透射权重，而不是单层透明度。

相对的另一套是 **Back-to-Front Over**（远→近，传统画家算法），见第 6 节。

---

## 4. 帧初状态与数值走查

### 4.1 累积缓冲怎么清空

```cpp
// initPeelBuffers()
glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // C=0, A=1
```

含义：

- RGB = 0：还没有任何玻璃颜色；
- **A = 1**：剩余透射 100%（后面第一层可以全权重写入）。

### 4.2 三层例子

设：

- A 最近，\(\alpha_A = 0.5\)，颜色 \(C_A\)
- B 次近，\(\alpha_B = 0.6\)，颜色 \(C_B\)
- C 更远，\(\alpha_C\)，颜色 \(C_C\)

| 步骤 | src | 运算 | 结果 \(C\) | 结果 \(A\)（剩余透射） |
| :--- | :--- | :--- | :--- | :--- |
| 初值 | — | clear | \(0\) | \(1\) |
| 混 A | 层 A | \(C=C_A\cdot1+0\)；\(A=1\cdot(1-0.5)\) | \(C_A\) | \(0.5\) |
| 混 B | 层 B | \(C=C_B\cdot0.5+C_A\)；\(A=0.5\cdot(1-0.6)\) | \(C_A+0.5\,C_B\) | \(0.2\) |
| 混 C | 层 C | \(C=C_C\cdot0.2+C_{已有}\)；\(A=0.2\cdot(1-\alpha_C)\) | … | \(0.2(1-\alpha_C)\) |

给 C 层的权重是：

\[
A_{\text{给 C}} = (1-\alpha_A)\,(1-\alpha_B) = 0.5 \times 0.4 = 0.2
\]

注意是乘 **\((1-\alpha)\)**，不是乘 \(\alpha_A\cdot\alpha_B\)。

一般形式（近→远）：

\[
\begin{aligned}
C_{acc} &= C_1 + C_2(1-\alpha_1) + C_3(1-\alpha_1)(1-\alpha_2) + \cdots \\
A_{remain} &= (1-\alpha_1)(1-\alpha_2)(1-\alpha_3)\cdots
\end{aligned}
\]

### 4.3 实体遮挡（α = 1）

若某一层 \(\alpha_{src}=1\)（完全不透明）：

\[
A' = A_{dst}\cdot(1-1) = 0
\]

之后任意更远层：\(C_{src}\cdot A_{dst} = C_{src}\cdot 0 = 0\)，自动加不进去。  
这就是 Front-to-Back 里「近处实体挡住后面」的数学来源，无需再手工剔除。

---

## 5. 和 Final Pass 怎么接上

Blend 全部结束后，`fboAccum_` 里是 \((C_{acc},\, A_{remain})\)。

```glsl
// depth_peeling_final.frag
FragColor = frontColor + vec4(background_color, 1.0) * frontColor.a;
```

- `frontColor.rgb`：已经按近→远累好的透明颜色；
- `frontColor.a`：还没被挡完的权重，用来乘背景。

背景相当于「最后一层后面的东西」，权重正好是上面的 \(A_{remain}\)。

---

## 6. 和 Linked List「从远到近 Over」对比

### 6.1 Linked List 在干什么

`composite.frag` 先按深度 **从远到近** 排好，再手写 Over（通常 **不用** 本项目这套 `glBlendFuncSeparate`）：

```glsl
// 远 → 近；color 初始为不透明底色
color.rgb = color.rgb * (1.0 - frags[i].color.a)
          + frags[i].color.rgb * frags[i].color.a;
```

对应传统：

```cpp
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
// C' = Csrc·Asrc + Cdst·(1-Asrc)
```

### 6.2 一张表对照

| 维度 | Depth Peeling（本工程） | Linked List 合成 |
| :--- | :--- | :--- |
| 混合顺序 | **近 → 远**（Front-to-Back） | **远 → 近**（Back-to-Front） |
| 公式习惯名 | Under | Over（画家算法） |
| RGB 公式 | \(C_{src}\,A_{dst} + C_{dst}\) | \(C_{src}\,\alpha_{src} + C_{dst}(1-\alpha_{src})\) |
| 缓冲 Alpha 含义 | **剩余透射**（还能透多少） | Over 过程中的不透明度累积（实现各异） |
| 典型 API | `glBlendFuncSeparate(DST_ALPHA, ONE, ZERO, ONE_MINUS_SRC_ALPHA)` | Shader 循环，或 `glBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)` |
| 起点 | `C=0, A=1`，最后再加背景 | 常从不透明底色 / 远景开始混 |
| 为何用这套 | 剥离管线 **先产出近层**，只能近→远混 | 链表收集后 **可排序**，适合远→近 Over |

两套在配对正确时都能做出合理透明；**顺序和公式必须成对**：

- 近→远 ↔ Under（本文件这套）；
- 远→近 ↔ Over（Linked List / 传统透明）。

配错（例如近→远却用 `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`）会亮错、透错。

### 6.3 使用场景与优缺点

**Front-to-Back Under（本工程）**

| | |
| :--- | :--- |
| **适合** | Depth Peeling、体积切片等「一层层从近往远生成」的管线 |
| **优点** | 与 peel 产出顺序一致；近处 α=1 时后面自动贡献为 0；可用固定管线混合 |
| **缺点** | src/dst 因子不直观；Alpha 语义是透射不是「本层透明度」；RGB 最好预乘 α（见下节） |

**Back-to-Front Over（Linked List / 传统）**

| | |
| :--- | :--- |
| **适合** | 已能按深度从远到近拿到片元（CPU 排序、GPU 排序链表） |
| **优点** | 和常见「玻璃公式」一致，好讲、好查资料 |
| **缺点** | 必须先有正确远→近顺序；不能直接套在「先剥近层」的循环上 |

---

## 7. 预乘 Alpha 与「颜色会不会溢出」

当前 peel 输出大致是：

```glsl
FragColor = vec4(litColor, alpha);  // RGB 未乘 alpha
```

而固定管线做的是 \(C' = C_{src}\,A_{dst} + C_{dst}\)，**没有再乘 \(A_{src}\)**。  
标准 Front-to-Back 在「未预乘」时更常见的是：

\[
C' = C_{src}\,\alpha_{src}\,A_{dst} + C_{dst}
\]

因此更稳妥的写法是 peel 里先预乘，再沿用现有四个因子：

```glsl
FragColor = vec4(litColor * alpha, alpha);
```

未预乘时，多层半透明亮色叠上去，RGB 更容易 > 1；本工程 `fboAccum_` 为 `RGBA16F`，可以存 HDR，最终上屏仍可能显得过曝。  
**Alpha 透射衰减** \(A' = A_{dst}(1-A_{src})\) 本身是对的，和是否预乘无关。

---

## 8. 其它等价 / 不等价写法（简表）

| 写法 | 是否等价于本工程 |
| :--- | :--- |
| 当前 `glBlendFuncSeparate(...)` | 本工程标准写法 |
| peel 预乘 + 同一套 Separate | 与标准 Front-to-Back 一致 |
| Shader 手写同一公式（关 `GL_BLEND`，能读到 accum） | 等价，实现更重 |
| 只用 `glBlendFunc` 两个参数 | **不等价**（RGB/A 无法拆开） |
| `glBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)` | **不等价**（那是远→近 Over） |

---

## 9. 和本工程调用点对照

```text
initPeelBuffers
  └─ fboAccum_ clear → C=0, A=1

peelAndBlend 每一层
  ├─ Peel  → 写入 fboPeel_.color / 深度
  └─ Blend → src = fboPeel_.color
             dst = fboAccum_.color
             glBlendFuncSeparate(DST_ALPHA, ONE, ZERO, ONE_MINUS_SRC_ALPHA)

compositeToScreen
  └─ 最终色 = C_acc + background × A_remain
```

---

## 10. 一句话总结

1. **src** = 本层 peel 新颜色；**dst** = `fboAccum_` 旧累积。  
2. 四参数 ⇒ \(C' = C_{src} A_{dst} + C_{dst}\)，\(A' = A_{dst}(1-A_{src})\)。  
3. 缓冲里的 **A 是剩余透射**；越往后的层权重越小：\((1-\alpha_1)(1-\alpha_2)\cdots\)。  
4. 这是 **近→远 Under**；Linked List 是 **远→近 Over**——顺序不同，公式不同，不要混用。
