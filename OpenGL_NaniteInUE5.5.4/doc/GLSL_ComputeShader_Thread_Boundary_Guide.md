# GLSL Compute Shader 线程调度与边界检查详解

本文档详细解析 Compute Shader（计算着色器）中的线程网格布局、内置向量比较函数（`greaterThanEqual`、`any` 等）、0-Indexed 屏幕坐标边界检查逻辑，以及防止 GPU 显存越界访问的核心设计。

---

## 1. 核心 GLSL 向量比较与逻辑函数

在 GPU 矢量计算中，GLSL 提供了专门用于向量按分量比对的内置函数，不能直接对 `ivec2` 或 `vec3` 使用 C++ 式的 `v1 >= v2` 操作符。

### 1.1 向量关系比较函数（Vector Relational Functions）

| GLSL 函数 | 比较逻辑 | 返回类型 | 说明 |
| :--- | :--- | :--- | :--- |
| **`greaterThanEqual(x, y)`** | **`x >= y`（大于等于）** | `bvec` | 按分量比对，对应分量 $x_i \ge y_i$ 时为 `true` |
| **`lessThanEqual(x, y)`** | **`x <= y`（小于等于）** | `bvec` | 按分量比对，对应分量 $x_i \le y_i$ 时为 `true` |
| **`greaterThan(x, y)`** | **`x > y`（大于）** | `bvec` | 按分量比对，对应分量 $x_i > y_i$ 时为 `true` |
| **`lessThan(x, y)`** | **`x < y`（小于）** | `bvec` | 按分量比对，对应分量 $x_i < y_i$ 时为 `true` |
| **`equal(x, y)`** | **`x == y`（等于）** | `bvec` | 按分量相等比较 |
| **`notEqual(x, y)`** | **`x != y`（不等于）** | `bvec` | 按分量不等比较 |

### 1.2 向量逻辑聚合函数

由于比较函数返回的是布尔向量（`bvec2` / `bvec3` / `bvec4`），必须搭配以下聚合函数将其化简为单个 `bool` 值：

* **`any(bvec)`**：**逻辑或 (`||`)**。只要布尔向量中有**任意一个分量**为 `true`，即返回 `true`。
* **`all(bvec)`**：**逻辑与 (`&&`)**。只有当布尔向量中**所有分量**均为 `true` 时，才返回 `true`。
* **`not(bvec)`**：**逻辑非 (`!`)**。对布尔向量按分量取反。

---

## 2. 实例剖析：`RasterClear.glsl` 中的边界检查

在 [Res/Shaders/RasterClear.glsl:L26-L28](../Res/Shaders/RasterClear.glsl#L26-L28) 中：

```glsl
ivec2 texcoord = ivec2(gl_GlobalInvocationID.xy);

// 越界检查：超出 1280x720 像素范围的线程直接返回
if(any(greaterThanEqual(texcoord, ivec2(1280, 720)))){
    return;
}
```

### 2.1 逐行转换逻辑

上述 GLSL 代码在逻辑上完全等价于以下 C/C++ 代码：

```cpp
if (texcoord.x >= 1280 || texcoord.y >= 720) {
    return; // 越界线程退出，不执行后续显存写入
}
```

### 2.2 为什么必须是 `>=`（大于等于）？

1. **0-Indexed 屏幕坐标系统**：
   对于一个 $1280 \times 720$ 分辨率的渲染画布：
   - $X$ 轴有效像素坐标范围为 `0, 1, 2, ..., 1279`（共 1280 个像素）
   - $Y$ 轴有效像素坐标范围为 `0, 7, 2, ..., 719`（共 720 个像素）
2. **越界临界点**：
   当 `texcoord.x == 1280` 或 `texcoord.y == 720` 时，索引已经来到了第 1281 个像素或第 721 行，**超过了合法界限**。
3. **防止显存越界崩溃**：
   后续代码按行优先公式计算 1D 数组索引：`pixelIndex = texcoord.y * 1280 + texcoord.x`。如果不对 `>= 1280` 和 `>= 720` 做拦截，非法线程会写入 `VisBuffer64` 缓冲区之外的显存区域，导致严重画质异常或 GPU 驱动崩塌。

---

## 3. 为什么 GPU 会产生越界线程？（工作组对齐向上取整）

在 CPU 启动 Compute Shader 调度时（如在 [scene.cpp:L101-L104](../src/Scene/scene.cpp#L101-L104)）：

```cpp
// CPU 端调度派发工作组
sRasterClearPass->SetComputeDispatchArgs(
    int(ceilf(float(inCanvasWidth) / 8.0f)),   // DispatchX = ceil(1280 / 8) = 160
    int(ceilf(float(inCanvasHeight) / 8.0f)),  // DispatchY = ceil(720 / 8) = 90
    1                                          // DispatchZ = 1
);
```

Shader 内部定义每个 Workgroup 尺寸为 $8 \times 8$：
```glsl
layout(local_size_x=8, local_size_y=8, local_size_z=1) in;
```

### 线程总数计算与边界补齐

* **当屏幕尺寸恰好能整除 8 时**（如 $1280 \times 720$）：
  - 启动 $160 \times 90 = 14,400$ 个工作组。
  - 总线程数为 $(160 \times 8) \times (90 \times 8) = 1280 \times 720 = 921,600$ 个线程。
* **当屏幕尺寸不能整除 8 时**（例如画布调整为 $1285 \times 723$）：
  - CPU 端使用 `ceilf(1285 / 8.0f)` 计算得到 **161** 个工作组。
  - CPU 端使用 `ceilf(723 / 8.0f)` 计算得到 **91** 个工作组。
  - GPU 实际启动的线程覆盖范围为 $(161 \times 8) \times (91 \times 8) = 1288 \times 728$。
  - **产生冗余线程**：$X \in [1285, 1287]$ 和 $Y \in [723, 727]$ 的额外线程就是多出来的“填充线程”。
  - **`greaterThanEqual` 的必要性**：这些多出来的线程在执行 `any(greaterThanEqual(texcoord, ivec2(1285, 723)))` 时会被成功拦截并瞬间 `return`，保证了计算安全。

---

## 4. GLSL Compute Shader 线程内置坐标体系

Compute Shader 中有 4 个核心内置变量协同决定当前线程的位置：

```text
+---------------------------------------------------------------------------------+
|  全局 Dispatch 网格 (Dispatch Mesh)                                             |
|                                                                                 |
|   +--------------------------+  +--------------------------+                    |
|   | Workgroup (0,0)          |  | Workgroup (1,0)          | ...                |
|   |  LocalThreads 8x8        |  |  LocalThreads 8x8        |                    |
|   +--------------------------+  +--------------------------+                    |
|   +--------------------------+  +--------------------------+                    |
|   | Workgroup (0,1)          |  | Workgroup (1,1)          | ...                |
|   +--------------------------+  +--------------------------+                    |
+---------------------------------------------------------------------------------+
```

| 内置变量名 | 类型 | 说明 | 计算公式 / 示例 |
| :--- | :--- | :--- | :--- |
| **`gl_NumWorkGroups`** | `uvec3` | CPU 派发的工作组总数 | `uvec3(160, 90, 1)` |
| **`gl_WorkGroupID`** | `uvec3` | 当前工作组在网格中的 3D 索引 | `uvec3(wx, wy, wz)`（范围 `0..159`, `0..89`） |
| **`gl_LocalInvocationID`** | `uvec3` | 当前线程在所属工作组内部的局部坐标 | `uvec3(lx, ly, lz)`（范围 `0..7`, `0..7`） |
| **`gl_GlobalInvocationID`** | `uvec3` | **当前线程在全局屏幕空间的唯一像素坐标** | `gl_WorkGroupID * gl_WorkGroupSize + gl_LocalInvocationID` |
