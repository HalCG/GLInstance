# 传统 Alpha 混合 vs 三种 OIT 方案

> 阅读本仓库三个透明 Demo（Linked List / Depth Peeling / Stochastic）前，若对「传统 alpha 混合」不熟，先看本文。  
> 三个子项目文档默认你已知道：**传统做法靠排序 + 硬件混合；OIT 是为了少依赖物体排序。**

相关工程：

| 工程 | 文档 |
| :--- | :--- |
| [OpenGL_OIT_Linked_list](../OpenGL_OIT_Linked_list/docs/README.md) | 逐像素链表收集 → 排序 → over |
| [OpenGL_OIT_Depth_Peeling](../OpenGL_OIT_Depth_Peeling/docs/README.md) | 多 Pass 按深度逐层剥离 |
| [OpenGL_OIT_Stochastic_Transparency](../OpenGL_OIT_Stochastic_Transparency/docs/README.md) | alpha 当概率 + MSAA 近似 |

---

## 1. 传统 Alpha 混合是什么？

### 1.1 一句话

开启 OpenGL **混合（Blending）** 后，新片元（源）与帧缓冲里已有颜色（目标）按公式合成；  
**透明物体必须大致按「从远到近」画**，顺序错了画面就错。

### 1.2 硬件在算什么？

常见 Back-to-Front（从后往前）配置：

```cpp
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
// 再按距离从远到近 Draw 透明物体
```

对应颜色（忽略预乘细节时的直觉形式）：

\[
C_{\text{out}} = C_{\text{src}}\cdot \alpha_{\text{src}} + C_{\text{dst}}\cdot (1 - \alpha_{\text{src}})
\]

| 符号 | 含义 |
| :--- | :--- |
| \(C_{\text{src}}, \alpha_{\text{src}}\) | 当前正在画的透明片元颜色与透明度 |
| \(C_{\text{dst}}\) | 帧缓冲里已有的颜色（更远的东西已经画进去了） |
| \(C_{\text{out}}\) | 混合后写回缓冲的结果 |

这叫 **over 算子**：新一层「盖」在旧结果上，透出一部分底下的颜色。

**关键性质：不满足交换律。**

\[
A\ \text{over}\ B \;\neq\; B\ \text{over}\ A
\]

所以「先画谁、后画谁」会改变最终颜色——这就是传统方案必须排序的根源。

### 1.3 标准 CPU 管线长什么样？

```text
1. 先画所有不透明物体（开深度写，通常关混合或按不透明处理）
2. 收集透明物体
3. 按到相机的距离排序（远 → 近）
4. glEnable(GL_BLEND)，关或慎用深度写
5. 按排序结果依次 Draw → 硬件按 blend 公式累加
```

示意：

```text
正确（远→近）：
  先画远的玻璃  →  再画近的玻璃  →  颜色对

错误（近→远）：
  先画近的玻璃  →  再画远的玻璃  →  远的被错误地「盖」在近的逻辑上 → 颜色错
```

### 1.4 深度测试怎么配？（传统）

| 状态 | 典型做法 | 原因 |
| :--- | :--- | :--- |
| 深度测试 | 常开（`GL_LESS`） | 被不透明挡住的透明片元可丢掉 |
| 深度写入 | **关**（`glDepthMask(FALSE)`） | 透明之间不要互相「写死」Z，否则后画的更远片元可能测不过 |

注意：关深度写 **不能** 代替排序；只是避免透明互相污染 Z。顺序仍要靠 CPU 排。

### 1.5 传统方案的痛点

| 问题 | 说明 |
| :--- | :--- |
| 穿插 / 交错 | 两个 mesh 互相穿过时，没有单一「物体距离」能排对每个像素 |
| 每帧重排 | 相机一转，远近关系变，排序结果变 |
| CPU 开销 | \(O(n \log n)\) 排序 + 状态切换 |
| 颗粒度粗 | 按「物体」排，不是按「像素上的片元」排，复杂网格仍易错 |

**传统方案本质** = **正确的片元顺序 + 简单的硬件 blend**。难在顺序，不在公式。

---

## 2. OIT 想解决什么？

**OIT = Order-Independent Transparency（与顺序无关的透明度）**。

目标：透明物体 **不必**（或大大减弱）在 CPU 上按物体排序，仍尽量得到正确或可接受的透明效果。  
做法通常是：在 GPU 上按**像素**处理多层片元（排序 / 剥层 / 随机近似），而不是依赖「先画哪个 mesh」。

本仓库三个 Demo 都是 OIT，**都有别于**上一节的传统流程。

---

## 3. 三种 OIT 与传统对比

| | 传统 Alpha | Linked List | Depth Peeling | Stochastic |
| :--- | :--- | :--- | :--- | :--- |
| **是否依赖 CPU 物体排序** | **强依赖**（远→近） | 不依赖 | 不依赖 | 不依赖 |
| **核心手段** | `glBlend` + 排序 Draw | 逐像素链表 → 合成时排序 over | 多 Pass 按深度剥层再混合 | alpha→覆盖概率 + MSAA |
| **结果** | 顺序对则精确 | 精确（受层数/显存上限） | 精确（受剥层数） | **近似**（有噪点） |
| **实现复杂度** | 低（但排序难） | 高（SSBO/原子/Image） | 中（多 FBO Pass） | 低～中（MSAA/sample mask） |
| **典型代价** | CPU 排序 | 显存大 | Draw 次数多 | 噪声 / 需足够样本 |

### 3.1 Linked List（本仓库 `OpenGL_OIT_Linked_list`）

- 透明 Pass：**不按物体排序**，片元头插入 SSBO 链表  
- 合成 Pass：每像素取出节点 → 按 depth 排序 → over 混合  
- 相对传统：把「排序」从 CPU 物体级挪到 **GPU 像素级**

### 3.2 Depth Peeling（`OpenGL_OIT_Depth_Peeling`）

- 像剥洋葱：第 1 次取最近层，第 2 次取次近层……  
- 各层再按 front-to-back 等规则混合  
- 相对传统：用**多次深度剥离**代替一次排序绘制

### 3.3 Stochastic Transparency（`OpenGL_OIT_Stochastic_Transparency`）

- 用 alpha 当「占住 MSAA 子样本」的概率  
- 子样本内仍靠硬件 Z 裁决；整体上**不显式排序多层颜色**  
- 相对传统：用**统计近似**换实现简单与性能，不保证每帧每像素精确  
- **与 LL/DP 最终观感为何不同**：LL/DP 按 Over 透射加权（远处乘 \((1-\alpha)\)）；ST 是每 sample 一个赢家再**等权平均**，无透射链 → 同 α 下远处往往更抢、且有颗粒。详见该工程 README「最终效果差异」。

---

## 4. 一张图建立直觉

```text
传统 Alpha
  不透明 → CPU 按距离排透明物体 → 远到近 Draw + glBlend
                    ↑
              顺序错了就错

OIT（三种 Demo）
  不透明 → 透明片元用「链表 / 剥层 / 随机样本」在 GPU 侧处理
                    ↑
              不靠（或弱化）物体级排序
```

---

## 5. 读本仓库时怎么对照

1. 先建立本节的传统图景：`Enable(BLEND)` + 远→近。  
2. 看 Linked List：找「没有按物体 sort，却在 `composite.frag` 里按 depth 排」。  
3. 看 Depth Peeling：找「一层一层 peel 的循环 Pass」。  
4. 看 Stochastic：找「sample mask / MSAA，而不是链表排序」。

有了传统方案的认知，三个 OIT 文档里的「无需排序」「与顺序无关」才有对照物。

---

## 6. 一句话收束

- **传统 alpha**：公式简单（srcα + dst(1−α)），**必须排好绘制顺序**。  
- **三种 OIT**：都是为了减轻/去掉物体级排序；链表与剥层偏精确，随机透明偏近似。  
- 本仓库没有单独实现「纯传统排序透明」Demo；传统方案是对照基线，OIT 是改进路径。
