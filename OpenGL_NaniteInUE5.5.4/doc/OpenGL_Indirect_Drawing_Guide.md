# OpenGL 间接绘制技术 (`glDrawArraysIndirect`) 全景解析

本文档详细解析 OpenGL 间接绘制命令 `glDrawArraysIndirect` 的工作原理、与传统直接绘制 API `glDrawArrays` 的核心区别、底层 C 语言结构体内存布局，以及在 Nanite GPU-Driven 渲染管线中的实战应用。

---

## 1. 核心概念与驱动模式区别

在传统 OpenGL 渲染中，绘制参数由 CPU 计算并作为函数实参传递给 GPU；而在现代 GPU-Driven 渲染管线中，绘制参数直接由 GPU 内部的 Compute Shader 生成并存放在显存中，CPU 仅需下发一条“间接绘制指令”。

* **`glDrawArrays` (直接绘制 / CPU 驱动)**：
  - 绘制参数（如绘制顶点起点 `first`、顶点数量 `count`）由 CPU 算好后，通过 **C++ 函数实参直接传递**。
  - CPU 必须清楚知道要绘制多少个顶点。若数据由 GPU 计算得出，则必须从 GPU 显存回传到 CPU 内存（引起严重的 CPU-GPU 管线停顿 Stall）。
* **`glDrawArraysIndirect` (间接绘制 / GPU 驱动)**：
  - 绘制参数**不通过 C++ 函数实参传递**，而是存储在 **GPU 显存缓冲区 (`GL_DRAW_INDIRECT_BUFFER`)** 中。
  - Compute Shader 可在 GPU 端并发计算剔除，直接使用原子操作更新显存中的绘制参数。CPU 无脑下发指令，硬件命令处理器 (Command Processor) 直接从显存中读取参数发起绘制。

---

## 2. API 函数原型与 C 语言结构体布局

### 2.1 `glDrawArrays` 原型（传统）

```cpp
void glDrawArrays(
    GLenum mode,   // 图元类型，如 GL_TRIANGLES
    GLint first,   // 起始顶点偏移
    GLsizei count  // 绘制顶点数量
);
```

### 2.2 `glDrawArraysIndirect` 原型与数据结构（现代）

OpenGL 规范在中定义了间接绘制所需的命令结构体：

```cpp
// 1. OpenGL 规范定义的间接绘制参数结构体 (共 16 字节)
struct DrawArraysIndirectCommand {
    GLuint count;         // 每个实例的顶点数量 (如 Nanite 中 128 三角形 * 3 = 384)
    GLuint instanceCount; // 绘制的实例数量 (如动态剔除后存活的 Cluster 数量)
    GLuint first;         // 起始顶点偏移 (通常为 0)
    GLuint baseInstance;  // 基础实例 ID 偏移 (通常为 0)
};

// 2. 间接绘制 API 原型
void glDrawArraysIndirect(
    GLenum mode,           // 图元类型，如 GL_TRIANGLES
    const void* indirect   // 显存缓冲区中的字节偏移量 (传 (void*)0 表示从绑定 Buffer 0 字节开始)
);
```

---

## 3. 核心维度全方位对比

| 对比维度 | `glDrawArrays` (直接绘制) | `glDrawArraysIndirect` (间接绘制) |
| :--- | :--- | :--- |
| **参数存储位置** | **CPU 内存**（作为 C++ API 实参传递） | **GPU 显存**（存储在 `GL_DRAW_INDIRECT_BUFFER` 中） |
| **参数修改主体** | **CPU 主导**：每帧由 CPU 重新计算并上传 | **GPU 主导**： Compute Shader 自行计算并原子写入 SSBO |
| **CPU-GPU 回传停顿** | 若 GPU 算出了可见数量，必须回传 CPU，导致 **CPU-GPU 流水线停顿 (Stall)** | **零回传**！GPU 算完直接存显存，CPU 无脑发指令，硬件直接读 |
| **实例化支持** | 不支持多实例（需要改用 `glDrawArraysInstanced`） | **原生内置 `instanceCount`**，天然支持 GPU 实例化绘制 |
| **批处理能力** | 1 次 API 调用只能画 1 个 Mesh / Batch | 配合 `glMultiDrawArraysIndirect`，**1 次 API 调用可画上万个不同 Mesh** |
| **显存 Padding 需求** | 无要求 | 4 个 `uint32_t` 严格按 4 字节紧凑排列，完美匹配 `std430` SSBO 布局 |

---

## 4. 在 Nanite 项目中的三步极速闭环实战

在 Nanite 渲染管线中，`glDrawArraysIndirect` 与 `sWorkArgs` 缓冲区形成了完美闭环：

### 步骤 1：GPU 初始化 (`RasterClear.glsl`)
在每帧开始时，[RasterClear.glsl](../Res/Shaders/RasterClear.glsl)（约 L33–L42）由 GPU `(0,0)` 线程重置绘制参数：
```glsl
// mData[0] 对应 DrawArraysIndirectCommand.count
CurrentIndirectWorkArgs.mData[0] = 384; // 128 个三角形 * 3 顶点

// mData[1] 对应 DrawArraysIndirectCommand.instanceCount
CurrentIndirectWorkArgs.mData[1] = 0;   // 初始可见 Cluster 数量清零
```

### 步骤 2：GPU 剔除与实例数写入 (`ClusterCull.glsl`)

在 [ClusterCull.glsl](../Res/Shaders/ClusterCull.glsl) 中，Pass 3 遍历 Pass 2 写入的候选 Cluster 列表，通过 LOD 判据筛选可见簇，并写回 `instanceCount`。

| | **当前 Demo 实现** | **目标架构（UE 风格）** |
|---|-------------------|------------------------|
| 并行方式 | `local_size_x=1`，单线程 `for` 循环 | 多 workgroup 并行处理候选列表 |
| 可见计数 | 局部变量 `visiableClusterCount++`，最后一次性写 `mData[1]` | 每线程 `atomicAdd(WorkArgs.mData[1], 1)` |
| 调试友好性 | 高（逻辑线性，易断点） | 低（需处理竞争与同步） |

**当前代码（`ClusterCull.glsl` `main()`）：**
```glsl
uint visiableClusterCount = 0;
for (uint i = 0; i < clusterCount; i++) {
    // ... LOD 判断 ...
    if (projectionScales.x > clusterInfo.mLODError * lodScale) {
        VisiableClusterSWHW.mData[visiableClusterCount] = uvec2(pageIndex, clusterIndex);
        visiableClusterCount++;
    }
}
IndirectWorkArgs.mData[1] = visiableClusterCount;  // 最终 instanceCount
```

**目标架构示意（尚未实现）：**
```glsl
atomicAdd(WorkArgs.mData[1], 1);  // 多线程并发递增可见 Cluster 数
```

### 步骤 3：CPU 零开销触发硬件绘制 (`RenderPass.cpp`)
在 [RenderPass.cpp](../src/Render/RenderPass.cpp)（约 L184–L187）中，CPU 端**完全无需回传任何数据**，直接绑定 Buffer 发起绘制：
```cpp
// 绑定包含了 DrawArraysIndirectCommand 结构的 GPU 缓冲区 sWorkArgs[0]
glBindBuffer(GL_DRAW_INDIRECT_BUFFER, inIndirectBuffer);

// 触发硬件间接绘制：CPU 零计算/零等待，全由 GPU 显存中的 instanceCount 决定绘制多少个 Cluster！
glDrawArraysIndirect(GL_TRIANGLES, nullptr);
```
