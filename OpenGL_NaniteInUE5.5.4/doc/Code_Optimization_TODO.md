# 代码优化待办清单

> **说明**：本文档仅记录后续可做的结构与性能优化项，**不包含当前已排期的逻辑修改**。  
> 实施前请对照 [Code_Reading_Guide.md](./Code_Reading_Guide.md) 理解管线，避免误改 GPU 数据流。  
> 每项完成后在 `[ ]` 中打 `x` 并注明日期/PR。
>
> **文档分区**：**§一～§七** = 本仓库内可读性/性能优化（可按优先级做）；**§八～§十** = 从 UE 抠技术 / 转 GL·Vulkan Demo 的**长期备忘**，**是否继续抠 → 后续再考虑**，当前以读懂现有 Nanite Demo 为主。

---

## 优先级说明

| 等级 | 含义 |
|------|------|
| P0 | 可读性/可维护性收益高，风险低，建议优先 |
| P1 | 中等收益，需少量 C++ 或构建改动 |
| P2 | 性能或架构级优化，需 profiling 验证 |
| P3 | 长期演进，依赖前序项 |

---

## 一、Shader 结构与可维护性（P0）

### 1.1 抽取公共 GLSL 头文件

- [ ] 新建 `Res/Shaders/Common/` 目录
- [ ] `NaniteConstants.glsl` — BVH bitfield 宏（`NANITE_MAX_*` 等）
- [ ] `GlobalConstants.glsl` — UBO `layout(binding=0)` 声明（与 `oglcontext.h` 对齐）
- [ ] `NaniteCluster.glsl` — `ClusterInfo` + `GetClusterInfo()`（现重复于 `ClusterCull.glsl`、`HWRasterizeVS.glsl`）
- [ ] `NaniteLOD.glsl` — `GetProjectionScales()` + LOD 判据辅助函数（现重复于 `NodeAndClusterCull.glsl`、`ClusterCull.glsl`）
- [ ] 在 `CompileShader` / `RenderPass::SetCS` 中增加 `#include` 预处理（或编译前拼接）

**收益**：LOD 与 Cluster 解包逻辑只维护一份；单 Pass 文件可缩短至 ~80 行。  
**风险**：需验证 include 路径与 Windows 路径分隔符。

### 1.2 魔法数字改为命名常量

- [ ] 在公共头文件中定义 WorkArgs 下标常量：
  - `WORKARG_VERTEX_COUNT` → `mData[0]`（384）
  - `WORKARG_INSTANCE_COUNT` → `mData[1]`
  - `WORKARG_NODE_LIST_OFFSET` → `mData[5]`
  - `WORKARG_NODE_COUNT` → `mData[6]`
- [ ] 定义 `MainAndPostNodeAndClusterBatches` 分区常量：
  - `BATCH_NODE_STACK_BASE` = 0
  - `BATCH_NODE_STACK_CAPACITY` = 1024
  - `BATCH_CLUSTER_LIST_BASE` = 1024
- [ ] C++ 端 `scene.cpp` / `oglcontext.h` 用 `constexpr` 或注释引用同一套命名（无需改布局）

**收益**：CPU/GPU 布局语义一致，降低误读 offset 的概率。

### 1.3 命名与拼写统一（仅符号重命名，不改逻辑）

- [ ] `Visiable` → `Visible`（`VisiableClusterSWHW` 等，需同步 C++/GLSL/文档）
- [ ] `ShouldVisiteChild` → `IsDetailSufficientForLOD` 或 `ShouldStopAtThisNode`
- [ ] `nextCengNodeIndexOffset` → `nextLayerNodeOffset`（拼音变量名）

**注意**：全局重命名影响 RenderDoc 对象名与已有截图，单独 PR 处理。

---

## 二、C++ 侧结构与文档镜像（P0 ~ P1）

### 2.1 GPU 类型头文件

- [ ] 新建 `NaniteGpuTypes.h`，集中声明：
  - `GlobalConstants`（可从 `oglcontext.h` 迁出或 forward）
  - WorkArgs / Batches 下标 `constexpr`
  - VisBuffer64 打包格式说明（高 32 位 depth，低 32 位 payload）
- [ ] 与 [GPU_Driven_SSBO_Layout_Guide.md](./GPU_Driven_SSBO_Layout_Guide.md) 交叉引用

### 2.2 RenderPass 资源绑定表

- [ ] 在 `scene.cpp` 或单独 `NanitePasses.inl` 用表格/宏列出每个 Pass 的 binding 映射（避免与 shader 漂移）
- [ ] 可选：小型 `struct PassBindings { ... }` 仅作文档生成源，不必立刻改 `RenderPass` API

### 2.3 分辨率硬编码外提

- [ ] `1280`/`720` 在 `RasterClear.glsl`、`Visualization.glsl`、`HWRasterizeFS.glsl` 中与 `sCanvasWidth/Height` 统一（UBO `Misc0` 或 `uniform ivec2`）
- [ ] 当前注释已标明硬编码位置，改前先确认所有 Pass 的 dispatch 尺寸联动

---

## 三、调试与可观测性（P1）

### 3.1 Visualization 调试模式

- [ ] `GlobalConstants.Misc0.y` 或独立 uniform 作为 `DebugMode`
- [ ] Mode 1：按 BVH 深度伪彩
- [ ] Mode 2：显示 `projectionScales.x / LODError` 比值热力图
- [ ] Mode 3：仅高亮 instance 0 的 Cluster
- [ ] Mode 4：仅显示 VisBuffer 深度（高 32 位）

### 3.2 GPU 回读与统计（开发构建）

- [ ] 可选 `glGetBufferSubData` 读取 `WorkArgs[0].mData[1]` 打印可见 Cluster 数（帧率影响大，仅 Debug 宏开启）
- [ ] ImGui / 控制台输出当前 LOD Mip 与可见簇数量

### 3.3 RenderDoc 标注增强

- [ ] 已为 buffer 设置 `SetObjectName`；可补充每个 SSBO 的「当前帧语义」注释到 debug group 字符串

---

## 四、算法与管线逻辑（P2，需单独设计文档）

> 以下项会改变行为或性能特征，**不属于「纯重构」**，实施前需单开设计说明。

### 4.1 NodeAndClusterCull 固定 4 次 Ping-Pong

- [ ] 评估按 BVH 深度动态 dispatch 次数（当前固定 4 层可能不足或浪费）
- [ ] 评估多线程 workgroup（当前 `local_size_x=1` 单线程遍历）

### 4.2 ClusterCull 单线程 for 循环

- [ ] 改为 parallel reduction / 多 workgroup 分块剔除 + atomic 追加可见列表
- [ ] 与 [OpenGL_Indirect_Drawing_Guide.md](./OpenGL_Indirect_Drawing_Guide.md) 中 atomic 方案对齐（文档与实现当前不一致处需先统一）

### 4.3 SW + HW 混合光栅化

- [ ] 参考 [Nanite_SW_vs_HW_Rasterization_Guide.md](./Nanite_SW_vs_HW_Rasterization_Guide.md) 在 `ClusterCull` 分流
- [ ] 新增 SW Rasterize Compute Pass

### 4.4 视锥 / 遮挡剔除

- [ ] 当前主要依赖屏幕空间 LOD 误差；可补充 AABB/frustum 快速拒绝

---

## 五、构建与工程化（P1 ~ P2）

### 5.1 Shader 编译缓存

- [ ] 磁盘缓存 SPIR-V 或预处理后的 GLSL 字符串，缩短启动时间

### 5.2 自动化校验

- [ ] 脚本检查 `GlobalConstants` C++ 与 GLSL 成员顺序、大小
- [ ] CI 中编译全部 shader（需 headless GL 或 glslang 离线编译）

### 5.3 资源热重载（开发）

- [ ] 监视 `Res/Shaders/*.glsl` 变更并重新 `CompileShader`

---

## 六、性能 Profiling 检查项（P2，用数据驱动）

在 NSight / RenderDoc 中针对以下项建立 baseline，优化后再对比：

| 检查项 | 相关 Pass | 备注 |
|--------|-----------|------|
| `NodeAndClusterCull x4` GPU 时间 | Pass 2 | 单线程 CS 可能是热点 |
| `ClusterCull` 线性 for | Pass 3 | 候选簇很多时变慢 |
| `atomicMin` VisBuffer 竞争 | Pass 4 | 高重叠场景带宽/原子压力大 |
| `GL_ALL_BARRIER_BITS` 全屏障 | `RenderPass::Execute` | 可收窄为 `SHADER_STORAGE_BARRIER` 等 |
| 4MB WorkArgs 分配 | `scene.cpp` | 实际仅用少量 uint，可缩小并文档化 |

---

## 七、文档跟进

- [x] [Code_Reading_Guide.md](./Code_Reading_Guide.md) — 代码导读（与本清单配套）
- [ ] 优化项落地后，回写 README.md §7 目录结构（若新增 `Common/`）
- [ ] 将本文档已完成项同步到 CHANGELOG 或 README「已知限制」

---

## 八、UE 抠代码 · 长期规划总览（后续再考虑）

> **决策**：是否继续从 UE 源码抠模块、做成 GL/Vulkan Demo — **暂不排期，以后需要时再从本节与 §九、§十 拣项启动**。  
> 下列为已整理好的「菜单」；细节 checklist 见 §九（Nanite）、§十（非 Nanite）。

### 8.1 §九 — Nanite 后续（从 UE 抠）

| 方向 | 内容摘要 |
|------|----------|
| 光栅化 | SW 光栅化、HW/SW 分流、Shading Pass、瓦片排序减 atomic 竞争 |
| 剔除遍历 | 动态 BVH 层数、Pass2 并行化、视锥剔除、HZB 遮挡 |
| 编码数据 | 顶点压缩、DAG、Page 流式、GPU Scene 多实例 |
| 文档 | SW Rasterize / Shading 数据流 SVG（对齐 `figures/GetProjectionScales/`） |

→ 分项待办：**§九**；图示：`doc/figures/BVH_NaniteMesh/`、`doc/Nanite_SW_vs_HW_Rasterization_Guide.md`

### 8.2 §十 — 非 Nanite 的 UE 技术（转 Demo）

| 优先级 | 几条线 |
|--------|--------|
| **P0** | GPU Scene + 实例剔除 + Indirect；Deferred GBuffer + CSM；VisBuffer 泛化教程 |
| **P1** | MDI 合批、Bindless、SSR、TAA、Bloom/Tonemap |
| **P2** | VSM、Lumen 子集、Clustered Deferred、GPU Skinning、Mesh Shader（VK） |

→ 分项待办与主题表：**§十**（含 9.1～9.7 详表、学习顺序、开源对照）

### 8.3 开源对照（UE 抠不动时）

- [Filament](https://github.com/google/filament) — PBR、GL/VK 分层清晰  
- [The-Forge](https://github.com/ConfettiFX/The-Forge) — 多 API、GPU Driven  
- [nanite-webgpu](https://github.com/Scthe/nanite-webgpu) — Nanite 思路 WebGPU  
- SIGGRAPH Advances — Nanite / Lumen / VSM 官方幻灯片  

### 8.4 与下方「建议实施顺序」的关系

| 阶段 | 范围 | 状态 |
|------|------|------|
| A～C | §一～§七 本仓库优化 | 需要时可做 |
| **D** | §九 Nanite 扩展（SW 光栅化 → 分流 → Shading …） | **搁置，后续再考虑** |
| **E** | §十 广谱 UE Demo（P0 → P1 → P2 选一条） | **搁置，后续再考虑** |

---

## 九、Nanite 管线扩展（从 UE 抠出 · 明细待办）

> **状态**：同 §八，仅规划。参考 `doc/Nanite_SW_vs_HW_Rasterization_Guide.md`、`doc/figures/BVH_NaniteMesh/`。  
> UE 源码入口：`Engine/.../Nanite*`、`NaniteEncode`（本仓库 `Res/Tools/` 为摘录）。

### 9.1 光栅化与 VisBuffer

- [ ] **SW 软件光栅化 Compute Pass** — 微三角形边缘方程光栅化，写同一 VisBuffer64（`atomicMin`）
- [ ] **HW/SW 分流** — 用 `mEdgeLength * LODScaleHW` 或屏幕包围盒像素面积阈值（UE 约 32px）决定路径
- [ ] **Shading Pass** — VisBuffer `(page,cluster)` → 读法线/UV/材质 ID → 延迟着色（替代纯 Visualization 伪彩）
- [ ] **瓦片/排序优化** — 降低 VisBuffer `atomicMin` 竞争（UE 按 tile 排序后光栅化）

### 9.2 剔除与遍历

- [ ] **动态 BVH 层数** — 替代固定 4 次 Ping-Pong，按 `mData[6]` 为 0 提前结束或按树深 dispatch
- [ ] **Pass2 并行化** — `local_size_x=1` → 多 workgroup 处理节点栈 / 叶子展开
- [ ] **视锥剔除** — AABB vs frustum 快速拒绝（在 LOD 之前）
- [ ] **HZB / 深度金字塔遮挡** — 简化版单 mip HZB test（`NaniteCulling` 思路）

### 9.3 编码与数据（离线 Tools，可不进运行时）

- [ ] **顶点压缩/相对编码** — 深挖 `NaniteEncode.cpp` Page 内 bitstream
- [ ] **DAG 多父节点** — 本仓库简化为 BVH；UE 为 DAG cut
- [ ] **Page 流式** — 大世界按页加载 `nanitemesh`（需 GPU 页表 + IO）
- [ ] **实例级 GPU Scene** — 多物体 Nanite 实例 + 每实例变换（扩展 `ModelMatrix` 为 SSBO 数组）

### 9.4 文档与图示（随功能补齐）

- [ ] SW Rasterize 分步 SVG（对齐 `GetProjectionScales/` 目录风格）
- [ ] Shading Pass 数据流图（VisBuffer → GBuffer 或 Forward）

---

## 十、UE 渲染技术学习路线图（非 Nanite · 转 OpenGL/Vulkan Demo）

> **状态**：同 §八，长期备忘。  
> **目标**：从 UE 源码「抠」算法与数据流，用最小 Demo 在 GL/Vulkan 复现；不必移植整个引擎。  
> **建议**：每个主题独立小仓库或 `demos/<name>/` 子目录；先 OpenGL 验证再迁 Vulkan（descriptor、barrier 更规范）。

### 10.0 最值得「抠」成 GL/Vulkan Demo 的几条线（优先级待办）

> 与 §九（Nanite 扩展）并列；摘要见 **§8.2**。

#### P0 — 与本项目同族，优先做

- [ ] **GPU Scene + 实例剔除 + Indirect Draw** — UE：`GPUScene`、`InstanceCulling`；SSBO 实例数据 → CS 剔除 → `multiDrawIndirect` / `DrawIndexedIndirect`
- [ ] **Deferred GBuffer + 单方向光 CSM** — UE：`DeferredShading`、`ShadowSetup`；MRT 写 GBuffer → 阴影贴图 → 延迟光照 Pass（经典 UE 渲染主干）
- [ ] **VisBuffer / ID Buffer 泛化教程** — 基于本仓库 VisBuffer64 抽象成通用「先光栅化 ID+深度，再着色」Demo（不绑定 Nanite）

#### P1 — 性价比高，第二波

- [ ] **Indirect Draw / MDI 多子网格合批** — UE：`MeshDrawCommands`；一次 dispatch 多 mesh 的 indirect 参数
- [ ] **Bindless / 大 SSBO 资源表** — UE：`BindlessResources`；多纹理/多缓冲用索引在 shader 里取（GL ARB / VK descriptor indexing）
- [ ] **SSR（屏幕空间反射）** — UE：`ScreenSpaceReflections`；Hi-Z 步进 + 粗糙度混合
- [ ] **TAA / 简化 TSR** — UE：`TemporalSuperResolution`；History + jitter + velocity + 邻域混合
- [ ] **Bloom + Tonemap 后处理链** — UE：`PostProcess`；入门 FS 链，可与任意 Demo 叠加

#### P2 — 有价值但复杂，选一条深挖

- [ ] **Virtual Shadow Maps (VSM)** — UE：`VirtualShadowMap`；页表 + 深度 atlas（建议先做 P0 的 CSM 再碰）
- [ ] **Lumen 子集（SSGI 或 Distance Field AO 二选一）** — UE：`Lumen/`、`DistanceFieldAO`；勿整包 Lumen
- [ ] **Clustered / Tiled Deferred 多光源** — UE：`ClusteredDeferred`；3D 光源簇 + CS 构建 tile 光照列表
- [ ] **GPU Skinning** — UE：`GpuSkinCache`；骨骼 SSBO + VS/CS skin
- [ ] **Mesh Shader 路径（仅 VK/DX12）** — UE：`MeshDrawShader`；OpenGL 无原生对应，可用 CS 模拟或只做 Vulkan Demo

#### 暂不优先（抠思路即可，不宜先做完整 Demo）

- **整包 RDG (`RenderGraph`)** — 体量大，可手写 3～5 Pass 的 barrier 图代替  
- **完整 Material 图 / 节点编辑器** — 与图形算法学习弱相关  
- **Chaos 物理、网络复制、Gameplay 框架** — 非渲染 Demo 范围  

#### 每条 Demo 的交付物（统一模板，便于复习）

- [ ] `README`：对应 UE 模块路径 + 数据流 ASCII/SVG  
- [ ] 最小 GL 版 + 可选 VK 版（同一算法）  
- [ ] RenderDoc 截帧说明（哪个 Pass 写哪个 buffer）  

### 10.1 GPU Driven 基础（与本项目同族，优先）

| 主题 | UE 参考（引擎内大致路径） | Demo 要点 | API |
|------|---------------------------|-----------|-----|
| Indirect Draw / MDI | `MeshDrawCommands`、`FRHIDrawIndexedIndirect` | 多子网格一次 `multiDrawIndirect` | GL 4.3+ / VK |
| GPU Scene / 实例剔除 | `GPUScene`、`InstanceCulling` | SSBO 实例变换 + CS 剔除 + indirect args | GL/VK CS |
| Bindless / 大资源表 | `BindlessResources`、`ResourceCollection` | `GL_ARB_shader_storage_buffer` 或 VK descriptor indexing | GL/VK |
| 64-bit VisBuffer / ID Buffer | Nanite、部分 Deferred | 已实现雏形，可泛化为「可见性缓冲」教程 | GL/VK |

### 10.2 阴影与光照

| 主题 | UE 参考 | Demo 要点 | 难度 |
|------|---------|-----------|------|
| **Virtual Shadow Maps (VSM)** | `VirtualShadowMap` | 页表 + 单方向光深度 atlas + 采样时页映射 | 高 |
| **CSM 传统级联** | `ShadowSetup` | 先做这个再学 VSM | 中 |
| **Deferred Shading (GBuffer)** | `DeferredShading` | MRT：BaseColor/Normal/Roughness/Depth | 中 |
| **Clustered / Tiled Deferred** | `ClusteredDeferred` | 3D 光源簇 + CS 光照列表 | 中高 |

### 10.3 全局光照与后处理

| 主题 | UE 参考 | Demo 要点 | 难度 |
|------|---------|-----------|------|
| **Lumen（简化）** | `Lumen/` | 先做 **SSGI / 单 bounce SDF/距离场** 子集，勿一上来全套 | 很高 |
| **Distance Field AO** | `DistanceFieldAO` | 3D 纹理 DF + CS 锥追踪 | 高 |
| **SSR** | `ScreenSpaceReflections` | Hi-Z 步进 + 粗糙度混合 | 中 |
| **TSR / Temporal AA** | `TemporalSuperResolution` | History + jitter + velocity buffer | 中高 |
| **Bloom / Tonemap** | `PostProcess` | 纯 FS 链，入门后处理 | 低 |

### 10.4 网格与几何（Nanite 之外）

| 主题 | UE 参考 | Demo 要点 | 难度 |
|------|---------|-----------|------|
| **Mesh Shader 路径** | `MeshDrawShader`、Nanite MS | **仅 Vulkan/DX12**；GL 无对应，可用 CS 模拟 | 高 |
| **Procedural Mesh / Landscape** | `Landscape`、`GeometryScript` | 高度图 + tessellation 或 CS 细分 | 中 |
| **Skeletal Mesh GPU Skinning** | `GpuSkinCache` | SSBO 骨骼矩阵 + VS skin | 中 |
| **HLOD / Impostor** | `HLOD` | 远距离代理网格切换 | 中 |

### 10.5 引擎基础设施（抠思路，不必抠全引擎）

| 主题 | UE 参考 | 学习价值 |
|------|---------|----------|
| **Render Dependency Graph (RDG)** | `RenderGraph` | Pass 依赖、资源生命周期；Vulkan 用 subpass + barrier 手写简化版 |
| **Shader Permutation** | `GlobalShader`、`.usf` | 宏变体管理；Demo 可用少量 `#define` |
| **RHI 抽象** | `RHI` | 对照 GL 与 VK 资源创建/同步 API |
| **Asset  Cook 管线** | `DerivedData` | 离线 bake → 运行时只读（与 `NaniteEncode` 同思路） |

### 10.6 建议学习顺序（跨主题）

```
第 1 波（已有基础）: §九 Nanite 扩展 → SW 光栅化 → 简单 Shading
第 2 波（GPU Driven）: GPU Scene 实例剔除 → MDI 多 Draw
第 3 波（经典延迟）: GBuffer + 单方向光 CSM
第 4 波（屏幕空间）: SSAO/SSR → TAA 简化版
第 5 波（高级）: VSM 或 Lumen 子集（选一条深挖）
```

### 10.7 开源对照

见 **§8.3**（与上文列表相同，集中维护一处）。

---

## 建议实施顺序（路线图）

```
【当前可做】
阶段 A（低风险）: §1.1 公共头文件 → §1.2 命名常量 → §2.1 NaniteGpuTypes.h
阶段 B（开发体验）: §3.1 DebugMode → §5.3 Shader 热重载
阶段 C（需 profiling）: §4.2 并行 ClusterCull → 第六章屏障与分配优化

【后续再考虑 · 见 §八 总览】
阶段 D（Nanite）: §九 → SW 光栅化 → HW/SW 分流 → Shading → 剔除/编码/文档
阶段 E（广谱 UE）: §十 10.0 P0 → P1 → P2 选一条（各独立 Demo）
```

如有疑问，以 [Code_Reading_Guide.md](./Code_Reading_Guide.md) 中的「单 Pass 职责」为准，避免在优化时混淆 BVH 阶段与 Cluster 阶段的 LOD 比较方向。
