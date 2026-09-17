# GLSL / SPIR-V Compute Shader 内置变量与线程坐标系详解指南

## 1. 核心概念与线程架构

在 OpenGL (GLSL) 与 Vulkan (SPIR-V) 的计算着色器 (Compute Shader) 中，GPU 的并行计算任务采用了 **二级分层调度架构**：

1. **Work Group（工作组 / 线程组）**：由主机端 (C++ CPU) 通过 `glDispatchCompute(num_groups_x, num_groups_y, num_groups_z)` 或 Vulkan 中的 `vkCmdDispatch` 派发。
2. **Invocation / Local Thread（组内线程）**：在 GLSL 着色器头部由 `layout(local_size_x, local_size_y, local_size_z) in;` 声明。

```
+-----------------------------------------------------------------------------------------+
|                               Global Grid (全局计算任务网格)                             |
|                                                                                         |
|  +--------------------------+  +--------------------------+                             |
|  | WorkGroup (GroupID 0,0)  |  | WorkGroup (GroupID 1,0)  |  ...                        |
|  |  [0,0] [1,0] [2,0] ...   |  |  [0,0] [1,0] [2,0] ...   |                             |
|  |  [0,1] [1,1] [2,1] ...   |  |  [0,1] [1,1] [2,1] ...   |                             |
|  +--------------------------+  +--------------------------+                             |
|               :                                          :                              |
+-----------------------------------------------------------------------------------------+
```

---

## 2. 核心公式与 `gl_GlobalInvocationID` 物理含义

`gl_GlobalInvocationID` 是 GLSL 内置的 `uvec3` 变量，代表当前线程在**整个全局计算网格 (Global Grid) 中的绝对坐标 `(x, y, z)`**。

### 2.1 换算推导公式
$$\text{gl\_GlobalInvocationID} = \text{gl\_GroupID} \times \text{gl\_WorkGroupSize} + \text{gl\_LocalInvocationID}$$

* `gl_GroupID` (`uvec3`)：当前线程所在的工作组编号（范围：`0 ~ num_groups - 1`）。
* `gl_WorkGroupSize` (`uvec3`)：常量，工作组尺寸（即 `layout(local_size_x/y/z)` 声明的值）。
* `gl_LocalInvocationID` (`uvec3`)：当前线程在**自己所在工作组内**的相对坐标（范围：`0 ~ local_size - 1`）。

---

## 3. Compute Shader 5 大内置坐标变量对比表

| 变量名称 | 数据类型 | 物理含义 | 在 1280x720 视口 (8x8 组尺寸) 中的取值示例 |
| :--- | :--- | :--- | :--- |
| `gl_NumWorkGroups` | `uvec3` | 全局调度的工作组总数量 (`dispatch` 参数) | `(160, 90, 1)` |
| `gl_WorkGroupSize` | `uvec3` | 单个工作组的尺寸 (`layout` 声明值) | `(8, 8, 1)` |
| `gl_GroupID` | `uvec3` | 当前线程所在的**工作组编号** | 像素 (100, 200) 位于组 `(12, 25, 0)` |
| `gl_LocalInvocationID` | `uvec3` | 当前线程在**所属工作组内的相对坐标** | 在 8x8 组内的坐标，范围 `(0~7, 0~7, 0)` |
| **`gl_GlobalInvocationID`** | `uvec3` | **当前线程在全网格里的绝对 3D 坐标** | **直接对应屏幕像素绝对坐标 `(100, 200, 0)`** |

---

## 4. Nanite 项目实际应用与代码剖析 (`RasterClear.glsl`)

在 Nanite 项目的 [RasterClear.glsl](../Res/Shaders/RasterClear.glsl#L23) 中：

```glsl
layout(local_size_x=8, local_size_y=8, local_size_z=1) in;

void main() {
    // 1. 提取当前 GPU 线程对应的屏幕像素二维绝对坐标 (X, Y)
    ivec2 texcoord = ivec2(gl_GlobalInvocationID.xy);
    
    // 2. 视口边界判定：防止越界访问屏幕缓冲区
    if (any(greaterThanEqual(texcoord, ivec2(1280, 720)))) {
        return;
    }

    // 3. 特殊单线程判定：仅由坐标为 (0,0) 的绝对第一个线程执行全局控制逻辑
    if (texcoord.x == 0 && texcoord.y == 0) {
        CurrentIndirectWorkArgs.mData[0] = 384;
        CurrentIndirectWorkArgs.mData[1] = 0;
        CurrentIndirectWorkArgs.mData[5] = 0;
        CurrentIndirectWorkArgs.mData[6] = 1;
    }

    // 4. 按行优先计算 1D SSBO 显存数组索引
    int pixelIndex = texcoord.y * 1280 + texcoord.x;

    // 5. 并发清空全屏 VisBuffer64 (每个线程处理 1 个像素)
    VisBuffer64.mData[pixelIndex] = 0xFFFFFFFF00000000ul;
}
```

### 为什么需要 `gl_GlobalInvocationID`？
1. **二维像素无缝映射**：屏幕是一个 $1280 \times 720$ 的二维网格。通过将 C++ 端的 Dispatch 尺寸设为 $160 \times 90$（因为 $160 \times 8 = 1280$，$90 \times 8 = 720$），每个线程通过 `gl_GlobalInvocationID.xy` 刚好唯一且不重不漏地对应**屏幕上的每一个像素**。
2. **一维显存展开**：将二维坐标转换为一维下标 `pixelIndex = y * 1280 + x`，完美实现 GPU 并行访问 SSBO。

---

## 5. 跨图形 API (GLSL vs HLSL) 变量对照

| 概念描述 | GLSL (OpenGL / Vulkan SPIR-V) | HLSL (DirectX 11/12 / Vulkan HLSL) |
| :--- | :--- | :--- |
| **工作组声明** | `layout(local_size_x=X, ...)` | `[numthreads(X, Y, Z)]` |
| **全局线程坐标** | `gl_GlobalInvocationID` | `SV_DispatchThreadID` |
| **工作组编号** | `gl_GroupID` | `SV_GroupID` |
| **组内局部坐标** | `gl_LocalInvocationID` | `SV_GroupThreadID` |
| **组内一维平坦索引** | `gl_LocalInvocationIndex` | `SV_GroupIndex` |
