# OpenGL 五大演示：近期简化 / 优化 / Bug 修复梳理

> 覆盖工程：`OpenGL_Anti_Aliasing`、`OpenGL_Rendering_Paths`、`OpenGL_OIT_Depth_Peeling`、`OpenGL_OIT_Linked_list`、`OpenGL_OIT_Stochastic_Transparency`  
> 对应提交（`develop`）：`0ecd1e4` → `b9d7088` → `73c704c` → `3c5456a` → `eaee91e` → `6c36d38`  
> 用途：熟悉本次改动；后续 Vulkan 版可按「跨项目原则 + 各演示专项」逐项对齐。

---

## 1. 总览

| 工程 | 主要动作 | 性质 |
|------|----------|------|
| Anti-Aliasing | `IAaTechnique` 策略拆分四种 AA；删 TextOverlay / 重复资源；本地 glm → modules | 架构简化 + 资源统一 |
| Rendering_Paths | 清死代码；HDR / 材质 / normalMatrix 三路径对齐 | 对比公平性修复 + 简化 |
| OIT Depth Peeling | 清死代码；resize 重建 FBO；相机眼点；去掉无效 MSAA；`glDrawBuffers` 修正 | Bug 修复 + 清理 |
| OIT Linked List | 清死代码；resize 重建 FBO/SSBO；`uResolution`；采样器名；轨道眼点；无效 MSAA | Bug 修复 + 清理 |
| OIT Stochastic | 清死代码 / 空缓冲；本地 stb/glm → modules；**保留** MSAA（算法依赖） | 清理（算法相关资源不动） |

### 1.1 跨项目统一原则（Vulkan 也应先做）

1. **依赖不进子工程**：删掉各 demo 内嵌的 `include/glm`、本地 `stb_image` / 重复 `glad`，统一走仓库 `modules/`。  
2. **删未接线代码**：SkyBox / Sphere / global_fun / TextOverlay / 未用 shader / 未用模型拷贝。  
3. **离屏资源跟分辨率走**：FBO、深度图、head-pointer、SSBO 节点池在 resize 时必须销毁并重建；禁止只改 `viewport`。  
4. **相机位置与 view 同源**：specular / Blinn-Phong 的 `cameraPos` 必须等于 `lookAt` 的 eye，禁止写死常量。  
5. **MSAA 只在真正用到时开启**：画到非 MSAA 离屏目标却 `GLFW_SAMPLES` + `GL_MULTISAMPLE` 属于误导；Stochastic 依赖 sample mask，必须保留。  
6. **多路径对比时统一后处理与材质输入**：否则 FPS/画质差异混有「路径差异」和「实现不一致」。

---

## 2. OpenGL_Anti_Aliasing

**提交**：`0ecd1e4`（策略拆分）、`b9d7088`（去本地 glm）

### 2.1 问题在哪

| 问题 | 位置（改前） |
|------|----------------|
| 四种 AA 逻辑挤在 `AntiAliasingApp` 大 switch，难读难扩 | `src/AntiAliasingApp.cpp` |
| TextOverlay、line shader、根目录重复 Mesh/Model/Shader、未用 spot 模型 | `TextOverlay.*`、`resources/shaders/line.*`、工程根 hpp、spot 资源 |
| 子工程自带一整份 `include/glm`，与 CMake 已链接的 `modules/glm` 重复 | `OpenGL_Anti_Aliasing/include/glm/**` |

**后续简化**：已删除 Wipe（`baselineFbo_` / `wipe.frag` / 同机位左右对比）；场景仍为棋盘地板 + DamagedHelmet。

### 2.2 处理方案

- 引入 `IAaTechnique` + `AaFrameContext`，按模式工厂创建：`NoneAa` / `MsaaAa` / `FxaaAa` / `TaaAa`。  
- App 只负责：切模式时 `createAaTechnique`、每帧 `beginFrameJitter` → 渲染 → `resolveFbo_` blit 上屏。  
- 切模式 `onEnter` 重置 TAA history，避免旧模式残影。  
- 删除 TextOverlay、Wipe 与重复资源；glm 只保留 modules。

### 2.3 处理前后

| 维度 | 改前 | 改后 |
|------|------|------|
| AA 分发 | App 内大型 switch / 分散状态 | `technique_->renderToResolve(...)` |
| 扩展新 AA | 改 App 多处 | 新加一个 `IAaTechnique` 实现 + factory case |
| 仓库体积 | 内嵌 glm 数千文件 | 依赖 modules |
| 阅读路径 | 混杂 UI 叠加与线框 | 主路径：`AaTechnique.cpp` + 各 Pass |

### 2.4 关键文件

- `include/AaTechnique.hpp`、`src/AaTechnique.cpp`（新建）  
- `src/AntiAliasingApp.cpp`（瘦身）  
- `CMakeLists.txt`（登记新源文件）

### 2.5 Vulkan 对照

| OpenGL | Vulkan 建议 |
|--------|-------------|
| `IAaTechnique` | 同样策略接口；每模式一套 pipeline / render pass 组合 |
| MSAA resolve `glBlitFramebuffer` | `vkCmdResolveImage` 或 multisample attachment resolve |
| FXAA / TAA 全屏 pass | 独立 graphics/compute pipeline + 描述符集 |
| TAA history reset | 切模式时清 history image / 重置 `hasPrevViewProj` 等价标志 |
| （已无 Wipe） | 对比靠切模式即可；不必另渲基线 |

---

## 3. OpenGL_Rendering_Paths

**提交**：`73c704c`

### 3.1 问题在哪

| 问题 | 位置（改前） | 影响 |
|------|----------------|------|
| TextOverlay、根目录重复 Mesh/Model/Shader、本地 glm、未用 spot | 工程根与 `include/` | 噪音、体积 |
| Deferred 材质系数与 Forward 不一致 | `geometry.frag` 曾硬编码 / 未走 `AppConfig::materialCoeffs()` | 三路径画质不可比 |
| HDR（Reinhard + Gamma）未在三路径统一开关 | Forward / Forward+ frag 缺 `uEnableHDR` 或行为不一致 | ImGui「HDR」只对部分路径生效 |
| `normalMatrix` 在 VS 里每顶点 `inverse(transpose)` | `mesh.vert` | 多余 ALU；且与 CPU 一次性计算不对齐 |
| 标题栏 CSV / `cameraDirty` 等陈旧状态 | `RenderingPathsApp` | 已无实际价值的复杂度 |

### 3.2 处理方案

1. **清理**：TextOverlay、重复资源、本地 glm；App 去掉无用标题/脏标记逻辑。  
2. **材质对齐**：三路径都 `setVec3("uMaterialK", AppConfig::materialCoeffs())`；Deferred G-Buffer `gMaterial` 写入同一套 Ka/Kd/Ks。  
3. **HDR 对齐**：`forward.frag` / `forward_plus.frag` / Deferred lighting 共用 `uEnableHDR` → Reinhard + Gamma。  
4. **normalMatrix**：`Scene.cpp` CPU `glm::inverseTranspose(mat3(model))` 上传；`mesh.vert` 只做矩阵-向量乘。

### 3.3 处理前后

| 维度 | 改前 | 改后 |
|------|------|------|
| 材质 | Deferred 可能常数，Forward 走 config | 全部 `AppConfig::materialCoeffs()` |
| HDR | 路径间不一致 | H 键对三条路径同语义 |
| 法线变换 | VS 每顶点求逆转置 | CPU 每物体一次 |
| App 职责 | 叠加标题字符串 / 脏标记 | 路径切换 + ImGui + 分发 renderer |

### 3.4 关键文件

- `src/Scene.cpp`、`resources/shaders/mesh.vert`  
- `resources/shaders/forward.frag`、`forward_plus.frag`、`geometry.frag`  
- `src/ForwardRenderer.cpp`、`ForwardPlusRenderer.cpp`、`DeferredRenderer.cpp`  
- `src/RenderingPathsApp.cpp`、`include/RenderingPathsApp.hpp`

### 3.5 Vulkan 对照

| 原则 | Vulkan 落地 |
|------|-------------|
| 三路径公平对比 | 同一套 material UBO、同一 tone-mapping pass（或同一 frag 宏） |
| G-Buffer 材质 | 与 Forward 同一 material buffer；勿在 geometry shader 写死 |
| normalMatrix | push constant / UBO 上传，避免每顶点 inverse |
| 切路径 | 切换「活跃 renderer」对象，而不是复制三套后处理 |

---

## 4. OpenGL_OIT_Depth_Peeling

**提交**：`3c5456a`

### 4.1 问题在哪

| 问题 | 位置（改前） | 现象 |
|------|----------------|------|
| resize 只改 viewport / aspect，不重建 peel/accum FBO | `framebufferSizeCallback` | 拉伸、深度 UV 错位、剥离层错乱 |
| `cameraPos_` 固定，`lookAt` 已绕轨 | `DepthPeelingApp` + 光照 uniform | 相机转了高光仍像固定视点 |
| `GLFW_SAMPLES=4` + `GL_MULTISAMPLE`，但主路径画非 MSAA 离屏 FBO | `initWindow` | 误导；窗口 MSAA 对 peel 几乎无效 |
| `glDrawBuffers` 把 `GL_DEPTH_ATTACHMENT` 当 draw buffer | `GlFramebuffer.hpp` | 不合法用法（深度应只挂 attachment） |
| 未用 init/quad/composite 旧 shader、SkyBox/Sphere、本地 glm/stb | resources / include | 死代码 |

### 4.2 处理方案

- resize：`width/height` 更新后调用 `initFramebuffers()`（`GlFramebuffer::create` 内部先 `destroy`）。  
- `OrbitCamera::eye()` 与 `view()` 共用；`setVec3("cameraPos", camera_.eye())`。  
- 去掉窗口 MSAA hint / enable。  
- `glDrawBuffers` 仅 `COLOR_ATTACHMENT0`。  
- 删除死资源；Mesh 改 `#include <stb_image.h>`（modules）。

### 4.3 处理前后

| 维度 | 改前 | 改后 |
|------|------|------|
| 改窗口大小 | 仍用旧分辨率纹理 | peel/accum 按新尺寸重建 |
| specular | 固定 `cameraPos_` | 跟轨道眼点 |
| MSAA | 开了但离屏没用 | 不请求、不 enable |
| FBO draw buffers | 颜色+深度错误列入 | 仅颜色 |

### 4.4 关键文件

- `src/DepthPeelingApp.cpp`、`include/DepthPeelingApp.hpp`  
- `include/OrbitCamera.hpp`、`include/GlFramebuffer.hpp`、`include/AppConfig.hpp`

### 4.5 Vulkan 对照

| OpenGL | Vulkan |
|--------|--------|
| resize → recreate FBO/纹理 | recreate color/depth image + framebuffer / 或动态 viewport 但 **attachment 尺寸必须匹配** |
| `eye()` | 同一 camera UBO 的 `viewPos` 与 `view` 矩阵同源 |
| 非法 draw buffer | 创建 framebuffer 时只挂需要的 color；depth 用 depth attachment 用法 |
| 多层 peel | 多层 color/depth 或 ping-pong image，resize 一并重建 |

---

## 5. OpenGL_OIT_Linked_list

**提交**：`eaee91e`

### 5.1 问题在哪

| 问题 | 位置（改前） | 现象 |
|------|----------------|------|
| resize 只改宽高，**不**重建 opaque FBO / headPtr / SSBO / clear PBO | `framebufferSizeCallback` | 链表头图与视口不一致、节点池容量错误、深度裁切错 |
| 深度采样 UV 写死 `800×600` | `oitRender.frag` | 非默认分辨率下 opaque depth 采样错位 |
| Draw 绑定键 `diffuse_texture`，shader uniform 是 `texture_diffuse` | `LinkedListOITApp.cpp` vs frag | 采样器未绑到正确 unit（依赖运气/残留状态） |
| `cameraPos_` 固定，view 用轨道公式 | 不透明/透明 Pass | 高光与视线不一致 |
| 无效窗口 MSAA；printf 调试；未用 quad shader | `initWindow` / processInput / shaders | 噪音与误导 |

### 5.2 处理方案

- 增加 `destroyOITBuffers` / `destroyFramebuffers` / `resizeGpuResources()`；回调里调用。  
- frag 增加 `uniform vec2 uResolution`；CPU 每帧 `setVec2`。  
- 绑定键改为 `texture_diffuse`（与 `blinnPhong.frag` / `oitRender.frag` 一致）。  
- `orbitEye()` 同时喂 `lookAt` 与 `cameraPos`。  
- 去掉 MSAA；shutdown 释放 GPU 资源。

### 5.3 处理前后

| 维度 | 改前 | 改后 |
|------|------|------|
| resize | 头指针纹理仍是旧尺寸 | FBO + SSBO + headPtr + PBO 全重建，`maxNodes = w*h*20` |
| 深度 UV | `/ vec2(800,600)` | `/ uResolution` |
| 漫反射采样 | 名不对齐 | `texture_diffuse` |
| 相机 | 固定 pos + 转动 view | `orbitEye()` 统一 |

### 5.4 关键文件

- `src/LinkedListOITApp.cpp`、`include/LinkedListOITApp.hpp`  
- `resources/oitRender.frag`、`include/AppConfig.hpp`、`include/Mesh.hpp`

### 5.5 Vulkan 对照（本 demo 最关键）

| OpenGL | Vulkan |
|--------|--------|
| `imageAtomicExchange` head pointer | `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE` + atomic；注意 queue barrier |
| SSBO 节点池 | `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`，resize 时按 `extent` 重算 `maxNodes` 并重建 |
| Atomic counter | 可用 storage buffer atomic 或专用 counter 方案 |
| `glMemoryBarrier`（image/SSBO/atomic） | `vkCmdPipelineBarrier`：fragment → 合成 pass 的正确 access/stage mask |
| resize | **必须**重建 storage image/buffer；只改 viewport 不够 |
| 采样器名 | 描述符绑定是索引，无「名字」；但 layout 与 CPU 写入必须一致（等价于本次对齐 bug） |

---

## 6. OpenGL_OIT_Stochastic_Transparency

**提交**：`6c36d38`

### 6.1 问题在哪

| 问题 | 位置 | 说明 |
|------|------|------|
| SkyBox / Sphere / global_fun / 本地 glm/stb | include | 未使用 |
| 空的 `initBuffers`、无用 vao/vbo 成员 | `StochasticTransparencyApp` | 死结构 |
| —— | —— | **未改** `GLFW_SAMPLES` / `GL_MULTISAMPLE` / sample mask：这是算法本体 |

### 6.2 处理方案

- 只做清理与 modules 对齐；保留 16x MSAA + sample mask 路径。  
- 注释收紧为「MSAA sample-mask 随机覆盖率 OIT」。

### 6.3 处理前后

| 维度 | 改前 | 改后 |
|------|------|------|
| 工程杂质 | 大量未用头/本地库 | 仅演示所需 |
| MSAA | 16x（正确） | 仍为 16x（正确） |

### 6.4 Vulkan 对照

- 需要 **multisample color+depth attachment** 与 per-sample 覆盖控制（`SampleMask` 或等价扩展）。  
- resize 时重建 MSAA image（样本数不变、extent 变）。  
- 不要按 DP/LL 的「去掉 MSAA」清单误删本 demo 的多重采样。

---

## 7. 按主题对照表（方便查「改了什么类问题」）

| 主题 | AA | RP | DP | LL | ST |
|------|----|----|----|----|----|
| 删本地 glm/stb / 死头文件 | ✓ | ✓ | ✓ | ✓ | ✓ |
| 架构策略化 / App 瘦身 | ✓ | ✓（轻） | | | |
| 三路径渲染一致性 | | ✓ | | | |
| resize 重建离屏资源 | （原有逻辑保留） | GBuffer 等原有 | ✓ | ✓ | MSAA 窗体依赖，未另加逻辑 |
| 相机 eye 与 view 同源 | | | ✓ | ✓ | （已有轨道则保持） |
| 去掉无效窗口 MSAA | | | ✓ | ✓ | **保留** |
| Shader/采样器/分辨率硬编码修复 | | HDR/材质 | drawBuffers | UV+sampler | |

---

## 8. Vulkan 迁移检查清单（建议顺序）

把 OpenGL 这次经验迁到 Vulkan 时，建议按下面顺序做，而不是先抄 shader：

1. **工程骨架**  
   - 共享 glm / stb / 资源根；子 demo 不嵌第三方树。  
   - 删 SkyBox 等未接线类型，避免「看起来有模块实际没跑」。

2. **Swapchain / resize**  
   - 凡是按像素分配的：color/depth、G-Buffer、OIT head image、SSBO 节点池 → 全部随 `extent` 重建。  
   - 检查：最小化窗口（extent 0）要 early-out，与本次 `width<=0` 守卫一致。

3. **Camera UBO**  
   - `view`、`proj`、`viewPos` 同一更新函数写出；禁止 specular 用另一套常量。

4. **对比类 demo（AA / Rendering Paths）**  
   - 策略对象切换；共享 tone-map / material。  
   - 切模式清 history（TAA）或清缓存描述符。

5. **OIT**  
   - DP：ping-pong depth/color + 层循环；非法 attachment 用法不要迁。  
   - LL：storage image + storage buffer + barrier；resize 重算 `maxNodes`。  
   - ST：**保留** MSAA sample 路径，不要套用「去掉 MSAA」条目。

6. **验收**  
   - 改窗口大小后画面与深度裁切仍正确。  
   - 转相机后高光随视线变。  
   - RP 三路径在同 light preset + HDR 开关下观感接近（路径差异应主要来自算法，而非材质/HDR 漏接）。  
   - LL 在非 800×600 下 opaque 遮挡透明仍正确。

---

## 9. 提交索引（便于 `git show`）

```
0ecd1e4  OpenGL_Anti_Aliasing：用策略模式拆分四种 AA，并清理无用代码
b9d7088  OpenGL_Anti_Aliasing：移除本地 glm，改用 modules
73c704c  OpenGL_Rendering_Paths：清理死代码并对齐三路径对比
3c5456a  OpenGL_OIT_Depth_Peeling：清理死代码并修复 resize/相机/MSAA
eaee91e  OpenGL_OIT_Linked_list：清理死代码并修复 resize/采样器/分辨率
6c36d38  OpenGL_OIT_Stochastic_Transparency：清理死代码，改用 modules 资源
```

更细的算法说明仍以各工程 `docs/README.md` 为准；本文只记录**本次简化与修复**的意图、问题位点与前后差异，方便对照 Vulkan 调整。
