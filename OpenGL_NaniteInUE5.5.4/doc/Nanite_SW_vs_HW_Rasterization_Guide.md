# UE5 Nanite 双路径光栅化：硬件光栅化 (HW) vs 计算着色器软件光栅化 (SW) 架构设计与指南

> **结合本 Demo 的 Pass 4 直观理解「硬件光栅化是什么」**：见 [Hardware_Rasterization_Guide.md](./Hardware_Rasterization_Guide.md)（DrawIndirect、VS/硬件光栅器/FS 分步说明）。  
> 本文侧重 **HW vs SW 为何需要双路径** 与 UE 扩展路线。

## 1. 背景与核心痛点

在传统的 GPU 图形渲染管线中，所有的几何渲染都必须通过硬件顶点着色器（VS/Mesh Shader）和硬件光栅化器（Hardware Rasterizer Fixed-Function Block）生成片元，再进入片元着色器（FS）。

但在 UE5 Nanite 的超高模（数百甚至数亿多边形）场景下，这种传统的“纯硬件光栅化”面临极其严重的性能瓶颈。

### 1.1 微三角形（Sub-pixel / Micro-triangles）与 Quad Overdraw 灾难

GPU 硬件光栅化芯片的底层调度单位是 **$2 \times 2$ 像素块（Quad）**。为了在片元着色器中通过偏导数（$dFdx / dFdy$）计算纹理 Mipmap 采样层级，GPU 必须保证每次着色器调度都包含完整的 4 个邻近像素点。

```
传统 GPU 硬件光栅化 2x2 Quad 调度机制：
┌─────────┬─────────┐
│ 像素 (0,0)│ 像素 (1,0)│  <-- 只要三角形覆盖了其中任意 1 个像素，
├─────────┼─────────┤      GPU 必须强制激活全部 4 个像素线程（Quad）
│ 像素 (0,1)│ 像素 (1,1)│  <-- 未被覆盖的像素成为辅助线程 (Helper Lanes)
└─────────┴─────────┘
```

当场景模型极度精细时，绝大多数三角形在屏幕上的投影面积只有 **小于 1 像素或几像素（微三角形）**：
- **辅助线程开销爆表**：一个微三角形仅覆盖了 1 个像素，但 GPU 仍需付出 4 个像素线程的计算资源。其余 3 个线程被标记为 Helper Lanes 并在最后丢弃，**GPU 浪费高达 75% 的硬件算力**。
- **光栅化单元吞吐量饱和**：硬件光栅化器的 Setup/Rasterize 单元在处理海量微小三角形时出现严重管道堵塞，GPU 运算核心（ALU）陷入饥饿状态。

---

## 2. UE5 Nanite 解决方案：HW + SW 双路径光栅化 (Dual-Path Rasterization)

为了攻克微三角形性能灾难，UE5 Nanite 创新性地引入了 **Compute Shader 软件光栅化 (Software Rasterizer)**，并与 **硬件光栅化 (Hardware Rasterizer)** 共同组成双路径光栅化系统。

```
                             [ Cluster Culling 剔除 Pass ]
                                           │
                        ┌──────────────────┴──────────────────┐
                        │ 三角形投影面积 / 包围盒与阈值对比    │
                        └──────────────────┬──────────────────┘
                                           │
                  ┌────────────────────────┴────────────────────────┐
                  ▼                                                 ▼
     【微小三角形 (< 32 像素)】                               【大三角形 (≥ 32 像素)】
                  │                                                 │
                  ▼                                                 ▼
    [ SWRasterize.comp (Compute Pass) ]             [ HWRasterize.vert / frag (Graphics Pass) ]
  • GPU 核心 Compute Wave 并行处理                • 调用 vkCmdDrawIndirect / glDrawArraysIndirect
  • 2D 边缘方程 (Edge Function) 光栅化              • GPU 硬件 Fixed-Function 光栅化芯片
  • 无 2x2 Quad 辅助线程浪费                       • 高效并行处理大面积连续像素
                  │                                                 │
                  └────────────────────────┬────────────────────────┘
                                           │
                                           ▼
                            [ 64-Bit VisBuffer64 (SSBO) ]
                    • 高 32 位：Depth (floatBitsToUint 保持顺序)
                    • 低 32 位：Visible ClusterID
                    • 统一使用 atomicMin64 保证正确深度测试
```

---

## 3. 两种光栅化方案深度对比

| 对比维度 | 硬件光栅化路径 (HW Rasterize) | 计算着色器软件光栅化路径 (SW Rasterize) |
| :--- | :--- | :--- |
| **底层实现依赖** | GPU 硬件 Fixed-Function 光栅化芯片 + VS/FS 管线 | 纯 Compute Shader (GLSL/HLSL) + 2D 几何数学方程 |
| **API 调用方式** | `vkCmdDrawIndirect` / `glDrawArraysIndirect` | `vkCmdDispatch` / `glDispatchCompute` |
| **适合的三角形类型** | 大三角形 (覆盖几十至几千像素) | 微小三角形 (Sub-pixel / 面积 < 32 像素) |
| **2x2 Quad 辅助线程浪费** | 严重（微三角形下浪费高达 75%） | **零浪费**（按需逐像素/逐线程计算，无 Quad 约束） |
| **硬件 Setup 吞吐限制** | 受限于 GPU 硬件光栅化 Fixed-Function 芯片数量 | 无限制（全显卡所有 Compute Unit (CU/SM) 自由调度） |
| **Z-Buffer 深度写入** | 硬件 Z-Buffer 测试 / FS 中 `atomicMin` | Compute Shader 内部 64 位 `atomicMin` |
| **代码实现复杂度** | 简单（依赖标准 3D 渲染管线） | 较复杂（需手写 2D 边缘方程、包围盒裁切与重心坐标插值） |

---

## 4. Compute Shader 软件光栅化的底层数学与算力实现

软件光栅化本质上是在 Compute Shader 中用纯数学公式替代硬件光栅化芯片的工作，分为以下 4 个核心步骤：

### 4.1 2D 屏幕投影与包围盒 (Bounding Box) 提取
首先将三角形 3 个顶点变换到屏幕像素坐标点 $P_0(x_0, y_0), P_1(x_1, y_1), P_2(x_2, y_2)$，并计算其紧致像素包围盒：
$$X_{min} = \max(0, \lfloor \min(x_0, x_1, x_2) \rfloor), \quad X_{max} = \min(Width-1, \lceil \max(x_0, x_1, x_2) \rceil)$$
$$Y_{min} = \max(0, \lfloor \min(y_0, y_1, y_2) \rfloor), \quad Y_{max} = \min(Height-1, \lceil \max(y_0, y_1, y_2) \rceil)$$

若 $(X_{max} - X_{min}) \times (Y_{max} - Y_{min}) < 32$，则标记为微三角形，送入软件光栅化。

### 4.2 2D 叉乘边缘方程 (Edge Functions)
对于屏幕上的任意像素点 $P(x,y)$，三条边的 2D 叉乘方程定义为：
$$E_{01}(x,y) = (x - x_0)(y_1 - y_0) - (y - y_0)(x_1 - x_0)$$
$$E_{12}(x,y) = (x - x_1)(y_2 - y_1) - (y - y_1)(x_2 - x_1)$$
$$E_{20}(x,y) = (x - x_2)(y_0 - y_2) - (y - y_2)(x_0 - x_2)$$

**覆盖测试判据**：当且仅当 $E_{01}(x,y) \ge 0 \land E_{12}(x,y) \ge 0 \land E_{20}(x,y) \ge 0$ 时，像素中心点 $(x+0.5, y+0.5)$ 位于三角形内部。

### 4.3 重心坐标与深度插值 (Barycentric Interpolation)
三角形二维面积的 2 倍为：
$$\text{Area2D} = E_{01}(x_2, y_2)$$
像素点 $P(x,y)$ 处的重心坐标 $\lambda_0, \lambda_1, \lambda_2$ 为：
$$\lambda_0 = \frac{E_{12}(x,y)}{\text{Area2D}}, \quad \lambda_1 = \frac{E_{20}(x,y)}{\text{Area2D}}, \quad \lambda_2 = \frac{E_{01}(x,y)}{\text{Area2D}}$$
插值计算该像素处的 NDC 深度值 $Z$：
$$Z = \lambda_0 \cdot Z_0 + \lambda_1 \cdot Z_1 + \lambda_2 \cdot Z_2$$

### 4.4 64 位 VisBuffer64 原子测试与写入 (`atomicMin`)
将深度浮点数与 Cluster ID 打包为 64 位整数：
```glsl
uint64_t depthUint64 = uint64_t(floatBitsToUint(Z));
uint64_t pixelValue64 = (depthUint64 << 32) | uint64_t(clusterID);

// 通过 64 位原子 Min 操作直接写入 VisBuffer64
atomicMin(VisBuffer64.mData[pixelIndex], pixelValue64);
```

---

## 5. 从现有项目扩展至软光栅的实装路线图 (Roadmap)

如果您未来希望在本项目中加入软光栅 Pass，可按以下三步扩操：

### 第一步：修改 `ClusterCull.comp` 增加分流逻辑
- 分配两个独立 SSBO：`VisiableClusterHW` 与 `VisiableClusterSW`。
- 在 `ClusterCull.comp` 中计算 Cluster 的屏幕包围盒像素面积。
- 面积小于阈值（如 32 像素）时，写入 `VisiableClusterSW`，并递增更新 `SWWorkArgs` 的 dispatch 维度；否则写入 `VisiableClusterHW`。

### 第二步：编写 `SWRasterize.comp` Compute Shader
- 以线程组为单位（如 64 个线程）读取 `VisiableClusterSW` 列表。
- 线程并行解包三角形顶点，执行 Edge Function 像素覆盖测试。
- 执行 `atomicMin` 将结果写入 `VisBuffer64`。

### 第三步：管线屏障 (Memory Barrier) 与可视化管线整合
- 在 HW 渲染 Pass 和 SW 渲染 Pass 之后插入 `VkMemoryBarrier`（Vulkan）或 `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)`（OpenGL）。
- 由于 HW 和 SW 两个 Pass 都统一将结果写入同一个 64位 `VisBuffer64` 缓冲区，后端的 `Visualization` 解码管线与 `FSQ` 管线**完全无需修改**即可原生支持！

---

> 💡 **总结**：当前 `OpenGL_NaniteInUE5.5.4` 项目结构清晰、模块化较高。理解透彻 GPU-Driven 剔除与 HW 渲染流程后，后续加入 SW 光栅化只需新增 Compute Shader 并在 `ClusterCull` 处做分流即可升级。
