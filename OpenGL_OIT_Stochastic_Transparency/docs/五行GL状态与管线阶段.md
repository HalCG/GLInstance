# 五行 GL 状态与管线阶段

> 返回：[README.md](./README.md) · 相关：[SampleMask投射与深度测试.md](./SampleMask投射与深度测试.md) · [Shader与渲染循环.md](./Shader与渲染循环.md)

`StochasticTransparencyApp::init()` 在首帧绘制前设置一次，之后每帧、每个片元的固定管线都受这些状态约束（本工程不会关掉）：

```cpp
glEnable(GL_MULTISAMPLE);
glEnable(GL_SAMPLE_MASK);
glEnable(GL_DEPTH_TEST);
glDepthFunc(GL_LEQUAL);
glDepthMask(GL_TRUE);
```

另有一处须先于上述生效（`initWindow`）：

```cpp
glfwWindowHint(GLFW_SAMPLES, 16);  // 创建 16× MSAA 默认帧缓冲
```

没有多采样缓冲，「子采样 / 投射孔位」不存在。

---

## 1. 总览：状态 × 管线阶段

| 状态 / 配置 | 主要生效阶段 | 一句话 |
|-------------|--------------|--------|
| `GLFW_SAMPLES=16` | 上下文 / FBO 创建 | 提供 N 个子采样槽位 |
| `GL_MULTISAMPLE` | 光栅化 → Resolve | 打开多采样路径 |
| `GL_SAMPLE_MASK` | 片元后、写入前 | 允许片元用 mask 筛子采样 |
| `GL_DEPTH_TEST` | 片元后、写入前 | 按深度决定能否写入某 sample |
| `glDepthFunc(GL_LEQUAL)` | 深度测试瞬间 | 通过条件：新深度 ≤ 旧深度 |
| `glDepthMask(GL_TRUE)` | 深度测试通过后 | 允许更新深度缓冲 |

---

## 2. `GL_MULTISAMPLE`

**何时**：`init()`，窗口已带 `GLFW_SAMPLES` 创建完毕之后。

**作用阶段**：

1. **光栅化**：三角形覆盖像素时影响该像素的 N 个子采样。
2. **片元着色**：片元与子采样关联（本 Demo 未开 `GL_SAMPLE_SHADING`，但 mask/深度仍按子采样语义工作）。
3. **写入**：颜色、深度写入多采样缓冲（每像素 N 份）。
4. **`SwapBuffers`（Resolve）**：N 个子采样等权平均成 1 个显示像素——Stochastic「混合」 largely 发生在这里。

**若关闭**：退化为单采样，本算法失效。

---

## 3. `GL_SAMPLE_MASK`

**何时**：`init()`，且须在片元写 `gl_SampleMask` 之前启用。

**作用阶段**：片元着色器执行完毕之后、颜色/深度实际写入之前的「样本遮罩」阶段。

- `gl_SampleMask[0] = randMask`：只有 bit=1 的子采样才允许接收本片元的 `FragColor` 和深度。
- 顺序：**先**用随机决定哪些 sample 有资格写，**再**对这些 sample 做深度测试。

**若关闭**：FS 赋值被忽略，掩码等效全开 → 近处对全部孔投射并写 Z，后面被盖住，半透明失效。详见 [SampleMask投射与深度测试.md](./SampleMask投射与深度测试.md)。

**依赖**：依赖 `GL_MULTISAMPLE`；单采样下无意义。`GL_MULTISAMPLE` 与 `GL_SAMPLE_MASK` 须同时启用。

---

## 4. `GL_DEPTH_TEST`

**何时**：`init()`；每帧 `beginFrame` 只 `glClear(DEPTH)`，不清开关。

**作用**：每个通过 Sample Mask 的子采样，将该点片元深度与多采样深度缓冲中对应 sample 比较。连续画多个物体时，同一子采样上更近者赢——**无需对网格排序**。

```text
片元到达 → Sample Mask 筛 sample → 深度测试筛 sample → 通过的 sample 写颜色+深度
```

**若关闭**：远近错乱，重叠透明错误。

---

## 5. `glDepthFunc(GL_LEQUAL)`

仅在深度测试执行瞬间生效。

- `GL_LEQUAL`：新深度 ≤ 缓冲中深度 → 通过。
- 相等深度可通过（共面更宽容）。
- 本 Demo 每帧从 `glClearDepth(1.0)` 开始，近处深度小、远处大。

Stochastic 只决定**哪些 sample 参与竞争**；**谁赢**由深度测试决定。

---

## 6. `glDepthMask(GL_TRUE)`

深度测试通过之后的写入阶段。

- `GL_TRUE`：通过的子采样更新多采样深度缓冲，之后更远片元在该点写不进。
- 四物体共用同一深度缓冲、中间不清深度——「近处透明占住该 sample」对后续 Draw 仍成立。

**若 `GL_FALSE`**：只测不写，后续片元挡不住，多层叠加会乱（传统透明常对透明 pass 关深度写；本算法路径不同）。

---

## 7. 一帧四物体时间线

```text
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

---

## 8. 五行与着色器分工

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

## 9. 简短取舍

- **噪声**：采样数有限（16）会有颗粒；可时间累积或 TAA。
- **近似**：非物理精确混合；要精确需 Linked List / Depth Peeling。
- **Per-sample shading**：未显式 `glMinSampleShading(1.0)`，极端情况下驱动行为需实机验证。

参考：McGuire & Bavoil, *Stochastic Transparency*, HPG 2013；本工程 `StochasticTransparencyApp.cpp`（`init`）、`resources/quad.frag`。
