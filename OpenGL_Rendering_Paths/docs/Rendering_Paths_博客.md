# OpenGL_Rendering_Paths：三条渲染路径对比与实现深读

> 源码：[GitHub — OpenGL_Rendering_Paths](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_Rendering_Paths)  
> 仓库根：[HalCG/OpenGLInstance](https://github.com/HalCG/OpenGLInstance)  
> 配套：[`Rendering_Paths_代码导读.md`](Rendering_Paths_代码导读.md)（零基础概念、模块地图、阅读路线）  
> **RenderDoc 问题记录：** [`RenderDoc_问题记录.md`](RenderDoc_问题记录.md)

---

## 1. 这篇博客讲什么

`OpenGL_Rendering_Paths` 是一个 **运行时切换** 的对比 Demo：同一场景、同一套点光源、同一套 Blinn-Phong 光照公式，分别用 **Forward / Deferred / Forward+** 三条路径渲染，用窗口标题和控制台 CSV 看各 Pass 耗时差异。

本文**不**赘述窗口、CMake、资源部署等工程细节，重点放在：

- 三条路径的 **数据流与 Pass 划分**
- 应用层 **关键状态** 如何驱动切换与重绘
- **LightManager** 如何造灯、如何做 Forward+ tile 分配
- **Deferred** 两阶段管线与光照 shader 在算什么
- 学习过程中常见疑问的 **整合解答**（随机分布、彩虹色、AABB 角点、tile 登记、半程向量等）

---

## 2. 场景与「公平对比」的设计

**场景：** 12 个 spot 实例 + 地板；默认 **256 盏点光源**（`[` / `]` 在 64 / 128 / 256 / 512 间切换）。

**公平性：** 三条路径共用

- 同一 `Scene`（网格与实例变换）
- 同一 `LightManager`（SSBO 点光源布局）
- 同一套光照数学（ambient + Lambert 漫反射 + Blinn-Phong 高光 + 半径衰减）

区别只在 **「什么时候、在哪里、对多少盏灯做循环」**。

---

## 3. 应用层：关键状态与切换作用

主控在 `RenderingPathsApp`，一帧的生命周期可以概括为：

```
输入/相机变化 → markCameraDirty()
     ↓
renderFrame() 按 currentPath_ 分发
     ↓
对应 Renderer 写屏 + PerfStats 计时
     ↓
swapBuffers；cameraDirty_ = false
```

### 3.1 核心状态变量

| 状态 | 类型 | 作用 |
|------|------|------|
| `currentPath_` | `Forward / Deferred / ForwardPlus` | **决定走哪条渲染管线**；`renderFrame()` 的 `switch` 唯一分支依据 |
| `cameraDirty_` | `bool` | **是否需要渲染一帧**；事件驱动：静止时 `glfwWaitEvents` 阻塞，有输入才 poll + draw |
| `showGBufferDebug_` | `bool` | **仅 Deferred 有效**；为 true 时跳过 lighting pass，全屏显示 GBuffer（albedo 等） |
| `lightPresetIndex_` | `int` | 当前光源数量档位；`[`/`]` 改变后调用 `lights_.regenerate(...)` **重建 SSBO 数据** |
| `camera_.isDragging()` | 来自轨道球相机 | 拖拽时 **关闭 GPU timer**、降低标题刷新频率，减少测量开销造成的卡顿感 |

### 3.2 按键与状态切换

| 按键 | 修改的状态 | 实际效果 |
|------|------------|----------|
| `1` / `F1` | `currentPath_ = Forward` | 单 Pass 前向；`showGBufferDebug_ = false` |
| `2` / `F2` | `currentPath_ = Deferred` | Geometry + Lighting 两 Pass |
| `3` / `F3` | `currentPath_ = ForwardPlus` | CPU tile cull + 前向 shading |
| `[` / `]` | `lightPresetIndex_` → `regenerate` | **三路径同时** 改变循环上限；`markCameraDirty()` |
| `G` | `showGBufferDebug_` 翻转 | **仅 Deferred**；Debug 时 `light_ms` 为 0 |
| 鼠标/滚轮 | 相机 yaw/pitch/radius | `cameraDirty_ = true`，触发重绘 |

**为什么 `cameraDirty_` 重要：** 这不是 VSync 下的每帧游戏循环，而是 **按需渲染**。理解性能对比时要知道：静止画面 CPU/GPU 几乎休眠；一动相机才出帧。CSV 每 120 帧在 **有渲染的帧** 上采样。

**为什么路径切换要 `markCameraDirty()`：** 切换后即使相机不动，也必须 **立刻重画一帧**，否则画面仍停留在上一路径的结果。

---

## 4. 光源系统：LightManager 在做什么

### 4.1 点光源 GPU 布局

```cpp
struct GpuPointLight {
    glm::vec4 positionRadius;  // xyz = 位置, w = 影响半径 (3.5)
    glm::vec4 colorIntensity;  // rgb = 颜色, w = 强度
};
```

三路径的 fragment shader 都从 **binding=0 的 SSBO** 读同一数组。

### 4.2 `regenerate()`：随机分布与「彩虹色」

`LightManager::regenerate()` 用固定种子 `mt19937(1337)`，保证每次按 `[`/`]` 换档位时 **布局可复现**，便于对比路径而非对比随机场景。

**`std::uniform_real_distribution` 的特点：**

- 在 `[a, b)` 上 **均匀** 抽样（上界通常取不到）
- 必须配合 `mt19937` 等引擎使用
- 固定种子 → 可复现的 Demo 灯光布局

各分布用途：

| 分布 | 范围 | 用途 |
|------|------|------|
| `posX`, `posZ` | -4 ~ 4 | 光源在 spot 周围水平散布 |
| `posY` | 0.5 ~ 3.5 | 高度，避免贴地 |
| `hue` | 0 ~ 1 | 生成不同色相 |
| `intensity` | 0.6 ~ 1.4 | 光强 |

**彩虹色一行代码在做什么：**

```cpp
const glm::vec3 color = glm::abs(glm::vec3(h * 6.0f + 0.0f, h * 6.0f + 2.0f, h * 6.0f + 4.0f) - glm::vec3(3.0f));
```

这不是标准 HSV→RGB，而是用 **三条相位错开的折线** 从单个 `h` 快速生成饱和、彼此差异大的 RGB：

- R = `abs(h*6 - 3)`，G = `abs(h*6 - 1)`，B = `abs(h*6 + 1)`
- 一个随机数 → 一盏灯一种颜色；值可能 > 1，配合 `intensity` 当偏亮灯色使用

超出 `activeCount_` 的槽位清零，shader 里仍用 `uLightCount` 限制循环次数。

---

## 5. 三条路径：流程与瓶颈

### 5.1 Forward — 最直白的「物体 × 光源」

```
bind 默认 FBO → clear
upload Light SSBO
对每个 mesh 实例：
  forward.frag：每个 fragment 循环 uLightCount 盏灯
```

- **Pass 数：** 1
- **计时：** `forward_ms`
- **瓶颈：** fragment 内 `for (i < uLightCount)`；256 光 × 12 实例时 **最重**
- **文件：** `ForwardRenderer.cpp` + `forward.frag`

光照循环与另外两条路径 **公式相同**（见第 7 节）。

---

### 5.2 Deferred — 几何与光照解耦

#### 总流程

```
Pass 1 Geometry  →  GBuffer FBO（不写最终色）
Pass 2 Lighting  →  默认 FBO（全屏 quad）
```

或 Debug 分支：`G` → 只显示 GBuffer，**跳过 Pass 2**。

#### GBuffer 布局

| 附件 | 格式 | 内容 |
|------|------|------|
| RT0 | RGBA8 | Albedo（贴图采样） |
| RT1 | RGB16F | 世界空间法线 |
| RT2 | RGBA8 | Material：`(Ka, Kd, Ks)` |
| Depth | D32F | 深度（**未存 worldPos，靠反推**） |

Geometry Pass（`geometry.frag`）只做：

```glsl
gAlbedo = texture(...);
gNormal = normalize(vNormal);
gMaterial = vec4(0.15, 0.75, 0.35, 1.0);
```

Lighting Pass（`deferred_lighting.frag`）对每个屏幕像素：

1. 采样 albedo / normal / material / depth
2. **`reconstructWorldPos(uv, depth)`** — 逆投影 + 逆视图
3. 循环全部点光源累加颜色
4. **`glDisable(GL_DEPTH_TEST)`** — 全屏 quad 不依赖 raster depth，深度只存在于纹理

#### 为什么 Deferred 的 `light_ms` 随光源数涨、而 `geom_ms` 较稳？

- Geometry：与 **物体数** 相关，与 **光源数无关**
- Lighting：全屏像素 × **每像素 uLightCount 次循环** — 本 Demo **没有** tile/cluster 光剔

#### `G` 调试态的意义

`showGBufferDebug_ == true` 时验证 Pass 1 是否正确：法线、albedo 异常时，Lighting 必然全错。此时应 **先修 Geometry**，再查 lighting。

---

### 5.3 Forward+ — 前向 + 按 tile 减光源

#### 总流程

```
Phase 0  CPU: buildForwardPlusTiles()  →  TileCounts + TileIndices SSBO
Phase 1  GPU: forward_plus.frag       →  每 fragment 只循环本 tile 的灯
```

#### Tile 参数

- Tile 大小：**16×16** 像素
- 每 tile 最多 **64** 盏灯（`kMaxLightsPerTile`）
- SSBO：`LightBuffer`(0) + `TileCounts`(1) + `TileIndices`(2)

#### Shader 侧

```glsl
ivec2 tile = ivec2(gl_FragCoord.xy) / uTileSize;
int tileIndex = tile.y * uTilesX + tile.x;
uint localCount = counts[tileIndex];
for (uint i = 0u; i < localCount; ++i) {
    uint lightIndex = indices[tileIndex * uMaxLightsPerTile + int(i)];
    // 用 lights[lightIndex] 做与 forward 相同的光照
}
```

**关键：** 仍是 **画 mesh 时算光**（前向），但循环次数从 256 降到「该 tile 候选列表长度」。

---

## 6. Forward+ 光分配：CPU 侧两段关键代码

### 6.1 八个角点：包住点光源的 AABB

对每盏灯，取球心 `center` 与半径 `radius`，三重循环 `x,y,z ∈ {0,1}`：

```cpp
const glm::vec3 offset((x ? 1.0f : -1.0f) * radius, ...);
corners[...] = center + offset;
```

得到 **8 个角点**，即 `[center ± radius]` 的轴对齐包围盒，近似包住点光影响球（略保守，安全不漏灯）。

8 个角点投影到屏幕 → 像素矩形 `[minX,maxX]×[minY,maxY]`。

### 6.2 像素矩形 → tile 登记

```cpp
tileMinX = (int)minX / kTileSize;
tileMaxX = (int)maxX / kTileSize;
// tileMinY / tileMaxY 同理

for (ty = tileMinY; ty <= tileMaxY)
  for (tx = tileMinX; tx <= tileMaxX) {
    tileIndex = ty * tilesX + tx;
    indices[tileIndex * 64 + count++] = lightId;
  }
```

含义：**把这盏灯登记到其屏幕投影覆盖的所有 tile**。

`counts[tileIndex]` 记录该 tile 有几盏候选灯；满 64 则丢弃（防止 SSBO 溢出）。

Forward+ 的 **`cull_ms`** 主要就是这段 CPU 工作；**`shade_ms`** 通常低于 Forward 的 **`forward_ms`**，因为 fragment 循环变短。

---

## 7. 光照公式：三路径共用的一盏灯

以下在 `forward.frag`、`deferred_lighting.frag`、`forward_plus.frag` 中 **同构**（Deferred 从 GBuffer 取 `normal/albedo/materialK/worldPos`）。

### 7.1 读灯与 early-out

```glsl
vec3 lightDir = lightPos - worldPos;
float dist = length(lightDir);
if (dist > radius) continue;
lightDir = normalize(lightDir);
```

超出影响半径 **不算这盏灯**。

### 7.2 距离衰减

```glsl
float attenuation = 1.0 - smoothstep(radius * 0.7, radius, dist);
```

在 `0.7×radius ~ radius` 之间从 1 平滑降到 0，软边界，非物理精确反比平方。

### 7.3 漫反射（Lambert）

```glsl
vec3 diffuse = Kd * max(dot(normal, lightDir), 0.0) * lightColor * albedo;
```

- `dot(N, L)`：表面朝向光的程度
- `max(..., 0)`：背光不贡献
- 乘 `albedo`：贴图底色

### 7.4 镜面高光（Blinn-Phong）与半程向量

```glsl
vec3 halfway = normalize(lightDir + viewDir);  // H = normalize(L + V)
vec3 specular = Ks * pow(max(dot(normal, halfway), 0.0), 32.0) * lightColor;
result += (diffuse + specular) * attenuation;
```

**半程向量 H 是什么？**

- **L**：表面 → 光源（`lightDir`）
- **V**：表面 → 相机（`viewDir`）
- **H**：L 与 V 的 **角平分线方向**

**为什么用 N·H 而不是 Phong 的 R·V？**

| | Phong | Blinn-Phong（本 Demo） |
|--|-------|------------------------|
| 高光度量 | 反射方向 R 与 V 的夹角 | 法线 N 与半程 H 的夹角 |
| 计算 | 需 `reflect()` | 只需 `normalize(L+V)`，更快 |
| 形状 | 经典尖峰 | 略宽，实时里更常见 |

**几何直觉：** 当宏观法线 N 接近 H 时，越多微平面能把来自 L 的光反射进 V → 高光最强。

`pow(N·H, 32)` 把贡献压成 **小亮斑**；32 越大高光越锐。

**与 diffuse 的分工：**

- **N·L** → 宽缓的「被照亮」
- **N·H** → 集中的「镜面闪点」
- 两者加在一起再乘 **attenuation**，累加到 `Ka * albedo` 的环境项上

Deferred 里 `worldPos` 来自 depth 反推，因此 **L、V、H 与 Forward 几何一致**，对比才公平。

---

## 8. 三路径对照：一张表收束

| 维度 | Forward | Deferred | Forward+ |
|------|---------|----------|----------|
| Pass | 1 | 2（+ 可选 Debug） | Cull + Shading |
| 何时算光 | 画物体 | 全屏 | 画物体 |
| 每像素灯循环 | 全部 `uLightCount` | 全部 `uLightCount` | 仅 tile 内列表 |
| 表面数据 | varying | GBuffer 纹理 | varying |
| 世界坐标 | `vWorldPos` | depth 重建 | `vWorldPos` |
| 主要耗时字段 | `forward_ms` | `geom_ms` + `light_ms` | `cull_ms` + `shade_ms` |
| 256 光拖拽体感 | 往往最顿 | lighting 重 | 通常最跟手 |

**预期实验：**

1. 固定 256 光，拖相机对比三路径标题栏 ms
2. `[` 降到 64，看 Forward 是否明显好转
3. Deferred 按 `G` 看 GBuffer，再关 Debug 看 lighting
4. Forward+ 拉近/拉远，观察 `cull_ms` 几乎不变、`shade_ms` 随场景略变

控制台 CSV 示例：

```
path,Forward,lights,256,...,forward_ms,14.3,...
path,Deferred,lights,256,...,geom_ms,3.2,light_ms,18.9,...
path,ForwardPlus,lights,256,...,cull_ms,0.4,shade_ms,12.1,...
```

---

## 9. 读代码推荐顺序

1. `RenderingPathsApp::renderFrame` — 三分支 dispatch
2. `LightManager::regenerate` + `buildForwardPlusTiles` — 灯从哪来、Forward+ 数据从哪来
3. `ForwardRenderer` — 基线
4. `DeferredRenderer::render` + `deferred_lighting.frag` — 两 Pass + 重建位置
5. `ForwardPlusRenderer` + `forward_plus.frag` — tile 索引如何消费
6. 三份 frag 的 **for 循环** 并排对比 — 理解「公式相同、循环范围不同」

更细的模块地图见 [`Rendering_Paths_代码导读.md`](Rendering_Paths_代码导读.md)。

---

## 10. 与仓库其它 Demo 的关系

- **透明 / OIT：** Deferred 不能直接处理透明；见 [OpenGL_OIT_*](https://github.com/HalCG/OpenGLInstance/tree/main)
- **PBR：** 本 Demo 是 LDR Blinn-Phong；物理材质见 [OpenGL_PBR](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_PBR)
- **抗锯齿：** MSAA 对 Forward/Forward+ 友好，对 Deferred GBuffer 困难；见 [OpenGL_Anti_Aliasing](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_Anti_Aliasing)

---

## 附录 A：Deferred 单像素数据流

```
Geometry Pass:
  mesh → geometry.frag → RT0 albedo, RT1 normal, RT2 material, depth

Lighting Pass:
  fullscreen → 读 GBuffer
            → reconstructWorldPos(depth)
            → for each light: diffuse + specular(Blinn-Phong)
            → FragColor
```

---

## 附录 B：Forward+ 单盏灯登记

```
center, radius
  → 8 个 AABB 角点
  → 投影得屏幕矩形
  → 换算 tileMin..tileMax
  → 写入 counts[] / indices[]
  → GPU: fragment 只读自己 tile 的 indices
```

---

## 附录 C：源码索引

```
OpenGL_Rendering_Paths/
  src/RenderingPathsApp.cpp   # 主循环、输入、路径切换
  src/ForwardRenderer.cpp
  src/DeferredRenderer.cpp
  src/ForwardPlusRenderer.cpp
  src/LightManager.cpp        # 点光源 + Forward+ tile culling
  src/Scene.cpp
  resources/shaders/
    forward.frag
    geometry.frag
    deferred_lighting.frag
    forward_plus.frag
```

GitHub 目录：[https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_Rendering_Paths](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_Rendering_Paths)

---

## 附录 D：三种路径原理对比（扩展表）

| 维度 | Forward | Deferred | Forward+ |
|------|---------|----------|----------|
| 核心思路 | 绘制物体时直接算光 | 先存 G-Buffer，再按像素打光 | 按屏幕 tile 剔除光源，再前向打光 |
| 光源复杂度 | O(物体 × 光源) | O(像素 × 光源) | O(物体 × tile 内光源) |
| 几何复杂度 | 随物体增加 | Geometry Pass 随物体增加 | 与前向类似 |
| 透明物体 | 自然支持 | 需额外 Pass | 自然支持 |
| MSAA | 友好 | 困难 | 较友好 |
| 带宽 | 较低 | G-Buffer 读写高 | 中等 |
| 典型引擎 | 小场景、移动端 | 大场景、多光源 | 现代 PC/主机 |

---

## 附录 E：交互、性能与观察

### 拖拽卡顿：算法负载 vs 实现开销

| 路径 | 默认 256 光源下拖拽 | 建议对比方式 |
|------|---------------------|--------------|
| **Forward** | GPU 最重（每像素循环 256 盏光 × 12 实例），帧时间波动大 | 按 `3` 切 Forward+，或按 `[` 降到 64/128 |
| **Deferred** | geometry pass 稳定，lighting pass 随光源数增加 | 与 Forward 对比 `light_ms` |
| **Forward+** | 通常最跟手（tile 内光源远少于总数） | 作为多光源前向的参考体验 |

本项目已做交互优化：先 `pollEvents` 再 render、拖拽期间跳过 GPU timer 与窗口标题更新。Forward + 256 lights 在 VSync 下仍可能比 Forward+ 慢，这属于 **渲染路径特性**，不是 bug。

### 性能展示

- **窗口标题**：当前路径、FPS、帧时间、各 Pass GPU 耗时
- **控制台 CSV**（每 120 帧）：便于记录对比数据

### 预期现象

- **Forward**：光源从 64 → 512 时，`forward_ms` 明显陡增
- **Deferred**：`geom_ms` 较稳定，`light_ms` 随光源增加
- **Forward+**：`shade_ms` 增长慢于 Forward（tile 内光源远少于总数）

### 构建与运行（简）

```bash
cmake --preset x64-clang-debug
cmake --build out/build/x64-clang-debug --target OpenGL_Rendering_Paths
```

可执行文件：`out/build/x64-clang-debug/OpenGL_Rendering_Paths/OpenGL_Rendering_Paths.exe`（需在 exe 同目录运行，`resources/` 由 CMake 部署）。

---

## 附录 F：注意事项与常见坑

### Forward

- 多光源时 fragment 循环是瓶颈；SSBO 大小限制最大光源数（本项目 512）
- 动态分支 `if (dist > radius)` 在 GPU 上仍有开销

### Deferred

- **透明物体**不能直接写入标准 G-Buffer，需单独 forward pass（见 OIT 子项目）
- G-Buffer 格式与带宽：RT 越多、精度越高，fill-rate 压力越大
- 深度重建依赖 `inverse(view/proj)`，精度不足会出现光照接缝
- MSAA 与 deferred 结合困难（需 per-sample G-Buffer 或 edge AA）

### Forward+

- Tile 大小权衡：过小 → culling 开销大；过大 → tile 内光源仍多
- `kMaxLightsPerTile` 过小会丢光；过大浪费 SSBO 与循环
- 首版用 **CPU culling**；生产环境常用 compute shader
- Depth pre-pass 可减少 overdraw（本 demo 未单独实现）

### 通用

- 多光源 demo 应用 **HDR + tone mapping**；本项目为 LDR 简化
- 阴影未实现；加入 shadow map 后 deferred 更易统一处理
- GPU timer query 在 profiling 时有 driver 开销；拖拽期间已自动跳过以提升跟手性

---

## 附录 G：与 OIT 子项目的关系

| 问题 | 推荐 Demo |
|------|-----------|
| 多光源 opaque 对比 | `OpenGL_Rendering_Paths`（本项目） |
| 顺序无关透明 | `OpenGL_OIT_Linked_list` / Depth Peeling / Stochastic |

Deferred 处理透明需 hybrid：opaque 走 G-Buffer，transparent 走 linked list / depth peeling 等。

GitHub：[OpenGL_OIT_Linked_list](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_OIT_Linked_list) · [OpenGL_OIT_Depth_Peeling](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_OIT_Depth_Peeling) · [OpenGL_OIT_Stochastic_Transparency](https://github.com/HalCG/OpenGLInstance/tree/main/OpenGL_OIT_Stochastic_Transparency)

---

## 附录 H：扩展方向（Phase 2）

- **Clustered Deferred**：3D 光源 cluster，进一步减少每像素光源数
- **Tiled Deferred**：与 Forward+ 类似的 tile 思想用于 deferred lighting
- **Compute-based Forward+**：GPU light culling，替代 CPU tile 构建
- **Hybrid Rendering**：opaque deferred + transparent forward

---

## 附录 I：RenderDoc 问题记录

本子项目开发/联调中遇到过的问题（现象、RenderDoc 定位路径、修复方式）见：

- [RenderDoc_问题记录.md](RenderDoc_问题记录.md)
