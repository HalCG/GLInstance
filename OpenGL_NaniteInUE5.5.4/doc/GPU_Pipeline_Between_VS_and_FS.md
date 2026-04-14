# 顶点着色器与片段着色器之间的 GPU 固定功能管线

> 本文说明 **Graphics Pass**（`Draw` / `DrawIndirect`）里，VS 写 `gl_Position` 之后、FS 收到 `gl_FragCoord` 之前，GPU **自动完成** 哪些步骤。  
> 结合本 Demo 的 Pass 4（`HWRasterize`）与 Pass 6（`FSQ`）。  
> 硬件光栅化总览见 [Hardware_Rasterization_Guide.md](./Hardware_Rasterization_Guide.md)。

---

## 1. 为什么单独讲这一段？

OpenGL / Vulkan 图形管线里，**只有 VS 和 FS 是你写的 .glsl**；中间大段是 **Fixed-Function（固定功能）**：

- 没有对应 Shader 文件  
- 行为由 API 状态决定（视口、裁剪、剔除、深度/stencil 开关等）  
- FS 里看到的 `gl_FragCoord` **已经是这些步骤处理后的结果**

不理解这段，容易疑惑：

- VS 已经乘了 `ProjectionMatrix`，为什么还要「透视除法」？  
- 裁切是在 Pass 2/3 做，还是在 Pass 4 做？  
- 本 Demo 不用 Depth Buffer，深度从哪来？

---

## 2. 总览：可编程 vs 固定功能

```
┌─────────────────────────────────────────────────────────────────┐
│  【可编程】顶点着色器 VS  —  HWRasterizeVS.glsl / FSQ VS         │
│    输入：gl_VertexID, gl_InstanceID, SSBO 顶点数据               │
│    输出：gl_Position (vec4, 裁剪空间)                            │
│          varying（如 V_PassThroughValue，flat 传给 FS）          │
└────────────────────────────┬────────────────────────────────────┘
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│  【固定功能】以下顺序为逻辑模型（实现可能合并，但语义等价）        │
│    ① 图元装配 (Primitive Assembly)   每 3 顶点 → 1 三角形        │
│    ② 裁剪 (Clipping)                 视锥 / 用户裁剪面           │
│    ③ 透视除法 (Perspective Divide)  (x,y,z) /= w → NDC         │
│    ④ 视口变换 (Viewport Transform)   NDC → 窗口像素坐标          │
│    ⑤ 可选：深度范围映射、Scissor、多视口…                        │
│    ⑥ 光栅化 (Rasterization)          三角 → 片元 + 插值         │
│       · 背面/正面剔除 (Face Culling)                             │
│       · 生成 gl_FragCoord.xy / .z                                │
│       · 插值 varying（depth、flat payload 等）                   │
└────────────────────────────┬────────────────────────────────────┘
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│  【可编程】片段着色器 FS  —  HWRasterizeFS.glsl / FSQ FS         │
│    输入：gl_FragCoord, 插值后的 varying                          │
│    输出：颜色附件 / SSBO atomicMin（本 Demo Pass4）              │
└─────────────────────────────────────────────────────────────────┘
```

**Compute Pass（Pass 1/2/3/5）不经过这条链** — 没有 `gl_Position`，也没有 `gl_FragCoord`。

---

## 3. 逐步说明（结合 Pass 4）

### 3.1 图元装配 (Primitive Assembly)

- `glDrawArraysIndirect(GL_TRIANGLES, …)` 每 **连续 3 个顶点** 组成一个三角形。  
- Pass 4：`vertexCount=384`，每 instance 最多 128 个三角；`vertexIndex >= mIndexCount` 的顶点虽参与装配，但退化位置可能导致无效三角。

**本 Demo 无额外配置**，由 Draw 的 topology 决定。

---

### 3.2 裁剪 (Clipping)

- **时机**：仍在 **齐次裁剪空间**（除 w 之前），对 `gl_Position` 做。  
- **对象**：视锥六个面（左/右/上/下/近/远），以及可选用户裁剪面。  
- **结果**：完全在外的三角丢弃；部分在外的三角 **裁成新顶点**（可能产生新三角），再进入后续步骤。

与 **Pass 2/3 Cluster 剔除** 的区别：

| | Pass 2/3 Compute | VS–FS 间 Clipping |
|--|------------------|-------------------|
| 粒度 | Cluster / BVH 节点 | **单个三角形** |
| 谁做 | Shader 读 SSBO | **GPU 固定功能** |
| 目的 | LOD、减少 Draw 量 | 只画视锥内可见部分 |

两者 **叠加**：Pass 3 决定画哪些 Cluster；Pass 4 里每个三角仍可能被硬件裁掉屏幕外部分。

---

### 3.3 `gl_Position.w` 从哪来？

`w` **不是** VS 里单独赋值的分量，而是 **4×4 矩阵乘法** 算出的第 4 个分量。

本 Demo Pass 4 完整链（`HWRasterizeVS.glsl`）：

```glsl
vec4 positionWS = ModelMatrix * vec4(positionMS, 1.0f);
positionWS.xyz -= CameraPositionWS.xyz;
vec4 positionVS = ViewMatrix * positionWS;
positionCS      = ProjectionMatrix * positionVS;
gl_Position     = positionCS;   // (x_clip, y_clip, z_clip, w_clip)
```

| 步骤 | 输入 w | 说明 |
|------|--------|------|
| `vec4(..., 1.0)` | **1.0** | 齐次坐标「点」；方向向量则用 0.0 |
| `ModelMatrix *` | 通常仍为 1 | 仿射变换（旋转/平移/缩放）不改变 w |
| `ViewMatrix *` | 通常仍为 1 | LookAt 视图矩阵为仿射 |
| `ProjectionMatrix *` | **变为 ≠ 1** | **透视投影** 把视图空间深度写进 w |

透视矩阵由 `matrix4::Perspective()` 构建（`src/Math/matrix4.cpp`），关键项 **`_34 = -1`**：

```cpp
// scene.cpp: Perspective(90°, aspect, near=1, far=10000)
_11 = near/R;  _22 = near/T;
_33 = (near+far)/(near-far);
_43 = (2*near*far)/(near-far);
_34 = -1.0f;   // 透视：w_clip 与视图空间 z 耦合
```

典型 OpenGL 透视下，可直观理解为 **`w_clip ≈ -z_view`**（相机沿 -Z 看，前方物体 z_view 为负，w 为正）。  
**近处 w 小、远处 w 大** → 后面除法 `x/w` 时近大远小。

#### 各空间 w 含义简表

| 阶段 | 典型 w | 含义 |
|------|--------|------|
| 模型/世界输入 | 1.0 | 齐次点 |
| 视图空间 | 1.0 | View 为仿射 |
| **裁剪空间 `gl_Position`** | **≠ 1**（透视时） | 供透视除法使用 |
| NDC（除法后） | 可视为 1 | 已在标准化范围 |
| FS `gl_FragCoord.z` | — | **深度标量**，不是 gl_Position.w |

#### 常规获得 / 使用 w 的方式

| 方法 | 说明 | 本 Demo |
|------|------|---------|
| **MVP / VP 矩阵链** | `gl_Position = P * V * M * vec4(p,1)`，w 自动出现 | ✅ Pass 4 |
| **分步矩阵** | 先 M→V→P，每步 vec4 乘法 | ✅ 同上 |
| **齐次 w 输入** | 点 `w=1`，方向 `w=0` | 顶点 `vec4(...,1)` |
| **正交投影** | P 常使 w 保持 1，除法几乎不改变 xy | 未用（本 Demo 为透视） |
| **手动透视** | Compute 自算 `x/z`、`y/z` 或投影公式 | Pass 2/3 `GetProjectionScales`，**无 gl_Position** |
| **直接写 gl_Position.w** | 非常规 hack | 未用 |

#### 与 Pass 2/3 的对比

- **Pass 4**：`ProjectionMatrix * positionVS` → `gl_Position.w` → GPU **硬件** `/ w`  
- **Pass 2/3**：无 `gl_Position`，用包围球 + 投影公式估算屏幕尺度，**不经过齐次 w 链**

---

### 3.4 透视除法 (Perspective Divide)

VS 输出 clip space 的 `(x, y, z, w)` 后，硬件自动（**VS 里不写 `/ w`**）：

```
x_ndc = x_clip / w_clip
y_ndc = y_clip / w_clip
z_ndc = z_clip / w_clip
```

- **除法的 w** 就是上一节 **`gl_Position.w`**（由投影矩阵产生）。  
- 近大远小：同一 `x_clip` 在远处 w 更大 → `x_ndc` 更小。  
- 深度 `z_ndc` 也经除法，再经视口映射成为 FS 里的 `gl_FragCoord.z`。

---

### 3.5 视口变换 (Viewport Transform)

把 NDC 映射到 **窗口像素坐标**（本 Demo 默认 1280×720，由 `Init` / 视口设置决定）：

```
像素 x ≈ viewport.x + (x_ndc + 1) * 0.5 * viewport.width
像素 y ≈ viewport.y + (y_ndc + 1) * 0.5 * viewport.height
深度   → 映射到 [0, 1] 区间（供 gl_FragCoord.z 使用）
```

之后光栅化在 **像素网格** 上判断三角覆盖哪些 `(x, y)`。

---

### 3.6 光栅化 (Rasterization)

核心：**三角形 → 片元（Fragment）列表**。

对每个被覆盖的像素中心（或样本）：

1. 生成一个 **片元**（会跑一次 FS `main()`）  
2. 设置 **`gl_FragCoord`**  
   - `.xy`：窗口空间像素坐标（含 0.5 中心偏移约定）  
   - `.z`：插值后的深度（已透视除法 + 深度范围映射）  
   - `.w`：通常为 1 / 插值 w（本 Demo FS 未用）  
3. **插值 varying**  
   - `V_PassThroughValue` 声明为 **`flat`** → 三个顶点同值，不插值，整三角同一 Payload  
   - 若未 flat 的 varying，会按重心坐标插值

示意：

```
        V0
       /  \
      / ■ ■\        ■ = 硬件判定在三角内 → 启动 FS
     /■ ■ ■\
    V1-------V2
```

#### 背面剔除 (Face Culling)

- 在光栅化前/中，根据三角 winding 与 `gl_CullFace` 丢弃背向三角。  
- **本 Demo Pass 4**：`cullMode = NONE`（不剔除），双面都画。

#### 深度 / Stencil 测试（传统管线）

- 可在 FS 前后用 **Depth Buffer** 做 Z-test。  
- **本 Demo Pass 4**：**未绑定深度附件**；深度竞争在 FS 内 **`atomicMin(VisBuffer64)`** 完成（Visibility Buffer 方案）。

---

## 4. FS 里用到的量从哪来？

以 `HWRasterizeFS.glsl` 为例：

| FS 变量 | 来源 |
|---------|------|
| `gl_FragCoord.xy` | 视口变换 + **光栅化** 给出的像素位置 |
| `gl_FragCoord.z` | 三顶点 clip z/w **插值**后再映射到 [0,1] |
| `V_PassThroughValue.x` | VS 输出，**flat** 传递，光栅化不插值 |

```glsl
float depth = gl_FragCoord.z;
uint payload = V_PassThroughValue.x;
atomicMin(VisBuffer64.mData[pixelIndex], ...);
```

**`pixelIndex`** 由 FS 用 `gl_FragCoord.xy` 自己算（本文件写死 1280 宽），与固定管线输出的坐标一致即可。

---

## 5. Pass 6（FSQ）同样走这条链

FSQ 也是 VS + FS + `Draw(3)`：

- VS 输出全屏三角形的 `gl_Position`  
- 中间 **同样** 裁剪 → 透视除法 → 视口 → 光栅化  
- FS 采样纹理输出到 **颜色附件**（与 Pass 4 写 SSBO 不同）

区别只在 FS **写什么**，**中间固定功能步骤相同**。

---

## 6. 与 Compute 的对比（为何没有「中间步骤」）

| | Graphics (Pass 4/6) | Compute (Pass 1/2/3/5) |
|--|---------------------|-------------------------|
| 驱动 | `gl_GlobalInvocationID` 等 | 无 |
| 透视除法 | 硬件自动 | 无；需自写投影数学（如 `GetProjectionScales`） |
| 三角覆盖像素 | 光栅器 | 无三角；线程自己选 `(x,y)` |
| 深度 | `gl_FragCoord.z` | 无；Pass 2/3 用包围球投影公式 |

---

## 7. 常见问题

**Q：Pass 2/3 已经剔过了，Pass 4 还要裁剪吗？**  
要。Pass 2/3 是 **Cluster 级**；硬件裁剪是 **三角级** 视锥裁切，粒度更细。

**Q：`w` 要自己算吗？**  
一般不用。`vec4(位置,1)` 乘 View、Projection 后 `gl_Position.w` 自动出现；透视除法由 GPU 用该 w 完成。详见 §3.3。

**Q：透视除法在 Shader 里能看到吗？**  
不能。只能在 VS 输出带 w 的 `gl_Position`，在 FS 用 `gl_FragCoord` 间接确认结果。

**Q：不用 Depth Buffer，还需要透视除法吗？**  
需要。除法 + 视口 + 插值才得到正确的 **屏幕位置** 和 **`gl_FragCoord.z`**；只是 Z 测试结果写进 SSBO 而非深度附件。

**Q：Helper / Quad 2×2 线程在哪？**  
属于光栅化 / FS 调度实现细节（见 [Nanite_SW_vs_HW_Rasterization_Guide.md](./Nanite_SW_vs_HW_Rasterization_Guide.md) §1.1）。微三角时 Helper 浪费是 UE 引入 SW 光栅的原因之一；本 Demo 未实现 SW。

---

## 8. 相关文件

| 文件 | 关联 |
|------|------|
| `Res/Shaders/HWRasterizeVS.glsl` | 输出 `gl_Position`（clip space） |
| `Res/Shaders/HWRasterizeFS.glsl` | 消费 `gl_FragCoord` |
| `src/Math/matrix4.cpp` | `Perspective()`，`_34=-1` 与 w 来源 |
| `src/Scene/scene.cpp` | Pass 4 `ExecuteIndirect`、投影参数 |
| `doc/Hardware_Rasterization_Guide.md` | HW 光栅化与 Draw 语义 |
| `doc/Code_Reading_Guide.md` §5.3 | Pass 4 导读 |

---

## 9. 小结

| 步骤 | 位置 | 本 Demo Pass 4 |
|------|------|----------------|
| 图元装配 | VS 后 | ✅ 自动（TRIANGLES） |
| 裁剪 | VS 后 | ✅ 视锥硬件裁剪 |
| `gl_Position.w` | VS 内（矩阵乘） | ✅ `Projection * positionVS`，见 §3.3 |
| 透视除法 | VS 后 | ✅ 自动 `xyz /= w_clip` |
| 视口变换 | VS 后 | ✅ 1280×720 |
| 光栅化 | VS 与 FS 之间 | ✅ 三角 → 片元 |
| 深度附件 Z-test | FS 前后 | ❌ 改用 FS `atomicMin` |

**记住**：VS 负责 **clip space**；FS 负责 **每个片元的逻辑**；中间 **裁剪、除法、视口、光栅化** 是 GPU 固定管线，Pass 4 依赖它才能把 Nanite 三角变成 VisBuffer 里的像素。
