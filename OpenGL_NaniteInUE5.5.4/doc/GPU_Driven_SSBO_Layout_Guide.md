# Nanite OpenGL GPU-Driven 管线 SSBO 缓冲设计与 GLSL 语法详解

本文档详细解析 Nanite GPU-Driven 渲染管线中核心缓冲区（`sWorkArgs` 等）在 GLSL 中的声明写法、背后的设计意图，内存对齐图解，与传统 OpenGL 传参/缓冲写法的对比，以及 C++ 端与 Shader 绑定的深层底层机制。

---

## 1. 核心 GLSL 声明语法

在 [Res/Shaders/RasterClear.glsl](../Res/Shaders/RasterClear.glsl#L8-L10) 及 [Res/Shaders/NodeAndClusterCull.glsl](../Res/Shaders/NodeAndClusterCull.glsl#L52-L54) 中，针对间接绘制参数缓冲区的声明如下：

```glsl
layout(binding=0, std430) buffer FCurrentIndirectWorkArgs {
    uint mData[];
} CurrentIndirectWorkArgs;
```

### 逐项语法拆解

1. **`buffer` 关键字（Shader Storage Buffer Object, SSBO）**：
   - 区别于 `uniform` 关键字（只读且容量受限），`buffer` 声明的是 SSBO。
   - **全双工读写 (Read-Write)**：允许 Compute Shader 执行写入、更新及 `atomicAdd` / `atomicMin` 等原子操作。
   - **大容量存储**：支持几百 MB 甚至 GB 级别的显存存储（受显卡内存限制，而非 OpenGL 规范硬性限制）。

2. **`std430` 内存对齐限定符**：
   - **标量紧凑对齐 (Packed Layout)**：在 `std430` 规则下，基础标量数组（如 `uint` / `int` / `float`）严格按自身字节大小（4 字节）连续排列，**绝无多余 Padding（内存填充字节）**。
   - **C++ 1:1 内存映射**：保证了 GLSL 中 `uint mData[]` 的内存分布与 CPU 端 C++ `uint32_t[]` 数组完美吻合。

3. **`uint mData[];` 动态运行时数组 (Unsized Array)**：
   - 数组空括号 `[]` 表示这是一个 **Unsized/Flexible Array Member**。
   - **动态显存适配**：Shader 编译阶段无需硬编码数组长度。CPU 端（如在 `scene.cpp` 中）通过 `glBufferData` 分配 4MB 或更大内存，Shader 端即可直接索引访问，极具拓展性。

4. **`layout(binding=0)` 显式绑定点**：
   - OpenGL 4.2+ 引入，在 GLSL 内部直接硬编码绑定的 Slot 槽位，避免了 CPU 端频繁调用 `glGetProgramResourceIndex` 查询 Index 的开销。

---

## 2. 与传统写法（普通写法）的全方位对比

| 对比维度 | 传统写法（普通 Uniform / UBO / 固定结构） | 现代 GPU-Driven 写法 (`std430 buffer uint mData[]`) |
| :--- | :--- | :--- |
| **传参/修改主体** | **CPU 主导**：CPU 每帧计算参数并通过 `glUniform*` 上传 | **GPU 主导**：Compute Shader 自行计算并原子写入 SSBO |
| **读写权限** | **只读 (Read-Only)**：Shader 只能读取 CPU 传来的值 | **可读可写 (R/W)**：支持 GPU 线程并发读写与原子操作 |
| **缓冲区容量** | **非常小**：UBO 规范保证上限通常仅 16KB~64KB | **极高**：SSBO 轻松支持数百 MB 至 GB 级显存 |
| **内存对齐与 Padding** | **`std140` 对齐**：`uint` 数组会被强行补齐到 16 字节（`vec4` 边界），造成 75% 显存浪费 | **`std430` 紧密排列**：`uint` 严格 4 字节紧凑排列，与 CPU 端 `uint32_t[]` 内存完全一致 |
| **灵活性与通用性** | **固定 `struct` / 固化用途**：强类型结构体只能用于单一功能 | **扁平 `uint` 数组**：兼作 `DrawIndirectCommand` 与任务队列 |
| **数组长度** | **固定长度**：如 `uint data[1024];`，修改需重编译 Shader | **动态长度**：`uint data[];` 编译时不指定大小，显存由 CPU 动态分配 |

### 统一场景案例：10,000 个 Mesh Cluster 的动态剔除与间接绘制

为了最直观地凸显区别，我们设定 **同一个具体功能需求**：
> **需求**：场景中有 10,000 个 Cluster，每帧需要进行视锥体剔除，计算出通过剔除的可见 Cluster 数量（假设本次通过了 1250 个），并把参数传给图形 API 进行绘制。

#### 1. 传统 1.0 写法（CPU-Driven + 传统 Uniform）

```glsl
// GLSL 代码 (只读)
uniform int u_InstanceCount;              // 可见实例数量
uniform mat4 u_ModelMatrices[100];        // 受 Uniform 容量限制(64KB)，甚至装不下 10000 个矩阵！
```

* **C++ 端处理逻辑**：
  1. **CPU 逐个计算**：CPU 用单线程开 `for` 循环遍历 10,000 个 Cluster，逐个计算 MVP 视锥体剔除（占用大量 CPU 资源）。
  2. **CPU 反射查询**：`GLint loc = glGetUniformLocation(program, "u_InstanceCount");`（字符串 Hash 查询）。
  3. **CPU 逐帧上传**：`glUniform1i(loc, visibleCount);`
  4. **CPU 发起绘制**：`glDrawArrays(GL_TRIANGLES, 0, count);`
* **痛点**：CPU 成为严重瓶颈；Uniform 容量极小装不下海量数据；无法在 Shader 中直接写回数据。

---

#### 2. 传统 2.0 写法（UBO / 旧版 SSBO + `std140` 布局 + 强类型 Struct）

```glsl
// GLSL 代码 (隐式绑定，未写 layout(binding=N))
struct DrawCommand {
    uint count;          // 4 字节
    uint instanceCount;  // 4 字节
    uint firstIndex;     // 4 字节
    uint baseInstance;   // 4 字节
};

layout(std140) uniform FDrawArgsBlock {
    DrawCommand cmd;
    uint data[1024];     // 固定硬编码数组长度 1024！
} DrawArgs;
```

* **C++ 端处理逻辑**：
  1. **CPU 3步握手绑定**：
     ```cpp
     GLuint idx = glGetProgramResourceIndex(program, GL_UNIFORM_BLOCK, "FDrawArgsBlock"); // 字符串查询
     glUniformBlockBinding(program, idx, 0); // 映射到 Slot 0
     glBindBufferBase(GL_UNIFORM_BUFFER, 0, buffer); // 挂载显存
     ```
  2. **内存 Padding 踩坑**：
     在 `std140` 下，`uint data[1024]` 数组中 **每一个 `uint` 元素都会被强制补齐到 16 字节 (vec4 边界)**！
     如果 C++ 端定义了一个连续的 `uint32_t c_data[1024]` 并用 `glBufferSubData` 上传，当 Shader 读取 `DrawArgs.data[1]` 时，实际上会读取到 **Offset 16**（即 C++ 数组中的 `c_data[4]`），导致数据严重错位！
* **痛点**：显存浪费 75%；内存对齐极易踩坑错位；硬编码定长数组缺乏弹性；需 CPU 反射查询绑定点。

---

#### 3. 现代 3.0 写法（Nanite 项目现用写法：`layout(binding=0, std430)` + 扁平 `uint mData[]`）

```glsl
// GLSL 代码 (显式绑定 Slot 0 + std430 紧密无 Padding 布局 + 动态 Unsized 数组 + 可读写 SSBO)
layout(binding = 0, std430) buffer FCurrentIndirectWorkArgs {
    uint mData[];
} CurrentIndirectWorkArgs;
```

* **C++ 与 GPU 端协同逻辑**：
  1. **C++ 1 步极速挂载**：
     ```cpp
     // 彻底省略 glGetProgramResourceIndex 和 glUniformBlockBinding！
     glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sWorkArgs);
     ```
  2. **GPU 并发剔除与原子写入**：
     在 Compute Shader 中，GPU 上千个线程并发执行剔除，通过原子加指令直接递增统计：
     ```glsl
     atomicAdd(CurrentIndirectWorkArgs.mData[1], 1); // 瞬间完成 10,000 个 Cluster 的并行统计！
     ```
  3. **零 CPU 拷贝间接绘制**：
     因为 `std430` 保证了 `mData[0..3]` 严格按 4 字节无 Padding 排列，正好 1:1 完美契合 OpenGL 的 `DrawArraysIndirectCommand` 结构体：
     - `mData[0]` = `384`（每簇顶点数）
     - `mData[1]` = `instanceCount`（GPU 原子加得出的最终可见 Cluster 数量）
     - `mData[2]` = `0`
     - `mData[3]` = `0`
     CPU 端直接触发 `glDrawArraysIndirect(GL_TRIANGLES, nullptr)`，硬件命令引擎直接从 `sWorkArgs` 读取渲染参数发起绘制！
* **优势**：零 CPU 计算开销、零 CPU 显存拷贝开销、内存 1:1 紧密排列无浪费、显式绑定清晰无反射开销！

---

## 3. `std140` 与 `std430` 对齐规则深度对比（具体内存图解）

OpenGL 规范中，`std140` 和 `std430` 在标量、向量上的基础对齐相同，但在 **数组 (Array)** 和 **结构体 (Struct)** 的打包规则上有天壤之别：

> [!IMPORTANT]
> - **`std140`**：UBO (Uniform Buffer Object) 的**强制标准**（也可以用于 SSBO）。原则是**偏向硬件读取效率，代价是大量内存 Padding**。
> - **`std430`**：**仅适用于 SSBO**（在 UBO 中使用会导致编译报错！）。原则是**紧凑打包 (Packed)，内存利用率极高**。

### 案例 1：基础标量数组（以 `uint data[4]` 为例）

假设在 GLSL 中声明了一个包含 4 个 `uint` 的数组：

```glsl
// std140 声明
layout(std140) buffer TestSTD140 { uint data[4]; };

// std430 声明
layout(std430) buffer TestSTD430 { uint data[4]; };
```

#### 内存偏移对比图（字节 Offset）：

```text
【std140 布局 - 总共占用 64 字节】：
字节 Offset:  0   4           16  20          32  36          48  52          64
            +---+-----------+---+-----------+---+-----------+---+-----------+
            |D0 |  Padding  |D1 |  Padding  |D2 |  Padding  |D3 |  Padding  |
            +---+-----------+---+-----------+---+-----------+---+-----------+
            |<-- 16 字节 -->|<-- 16 字节 -->|<-- 16 字节 -->|<-- 16 字节 -->|
            (规则：std140 中数组每一个元素的步长/Stride 必须强行对齐到 16 字节/vec4！)

【std430 布局 - 总共占用 16 字节】：
字节 Offset:  0   4   8   12  16
            +---+---+---+---+
            |D0 |D1 |D2 |D3 |
            +---+---+---+---+
            |<-- 16 字节 -->|
            (规则：std430 中数组元素的步长/Stride 等于标量自身大小 4 字节，紧密连续排列！)
```

* **C++ 端传参后果**：
  在 C++ 端声明 `uint32_t c_data[4] = {10, 20, 30, 40};`（连续占用 16 字节）。
  - 在 `std430` 下：GLSL 的 `data[0]=10, data[1]=20, data[2]=30, data[3]=40`，完美读取！
  - 在 `std140` 下：GLSL 的 `data[0]=10`（读 Offset 0），但 `data[1]` 会去读取 **Offset 16**（实际上读取到了 C++ 端未定义的垃圾内存），导致数值严重错位崩溃！

---

### 案例 2：标量与向量混合结构体（以 `struct` 为例）

```glsl
struct Particle {
    float mass;     // 4 字节
    vec2  pos;      // 8 字节 (要求 8 字节对齐)
    float lifetime; // 4 字节
};

// 声明 Particle particles[2];
```

#### 内存对齐分析：

1. `mass`：Offset 0..3 字节。
2. `pos` (`vec2`)：基本对齐要求为 8 字节，由于 `mass` 占了 0..3，系统必须在此处填充 **4 字节 Padding (Offset 4..7)**，使 `pos` 放置在 Offset 8..15。
3. `lifetime`：放置在 Offset 16..19。

#### `std140` vs `std430` 数组步长 (Stride) 区别：

* **在 `std140` 下**：
  `std140` 规定结构体在数组中的每一个元素必须强行向上补齐到 **16 字节的倍数**。
  因此整个 `Particle` 结构体的大小从 20 字节被强行扩展为 **32 字节**！
  - `particles[0]` 占用 Offset 0..31
  - `particles[1]` 从 Offset 32 开始

* **在 `std430` 下**：
  `std430` 规定结构体大小只需补齐到 **其内部最大成员的对齐要求**（`vec2` 的对齐要求为 8 字节）。
  因此整个 `Particle` 结构体的大小被补齐到 8 的倍数（**24 字节**）：
  - `particles[0]` 占用 Offset 0..23（其中 Offset 20..23 填充 4 字节以满 8 倍数）
  - `particles[1]` 从 Offset 24 开始

---

## 4. `layout(binding=N)` 显式绑定点原理与三层绑定架构

在 OpenGL 4.2 之前（以及传统的 Shader 写法中），GLSL 并不支持在代码中直接硬编码 `binding=0`。了解 `layout(binding=N)` 的演进对于理解现代 API (如 Vulkan / DX12 Descriptor Sets) 至关重要。

### 1. OpenGL 缓冲区的三层绑定架构

在 OpenGL 中，一个 SSBO 或 UBO 从 GPU 显存映射到 GLSL Shader 变量，需要经过 **三层绑定关系**：

```text
+-------------------------------------------------------+
|  层级 1：C++ CPU 端显存 Handle                         |
|  GLuint sWorkArgs[0] (通过 glBufferData 创建的 GPU 显存) |
+-------------------------------------------------------+
                           |
                           |  [桥梁 2: glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sWorkArgs[0])]
                           v
+-------------------------------------------------------+
|  层级 2：OpenGL 上下文硬件绑定槽位 (Binding Slot)      |
|  GL_SHADER_STORAGE_BUFFER 绑定点 0                    |
+-------------------------------------------------------+
                           ^
                           |  [桥梁 1: 确定 Shader 变量对应哪个 Slot 槽位]
                           |
+-------------------------------------------------------+
|  层级 3：GLSL 着色器中的 Buffer 块声明                 |
|  buffer FCurrentIndirectWorkArgs                      |
+-------------------------------------------------------+
```

---

### 2. 传统隐式绑定 (3步握手) vs 现代显式绑定 (1步挂载)

#### A. 传统隐式绑定 (Implicit Binding - 需 3 步握手)：
在传统写法中，GLSL 代码里没有 `layout(binding=0)`：

```glsl
// GLSL 代码（旧版）
buffer FCurrentIndirectWorkArgs {
    uint mData[];
} CurrentIndirectWorkArgs;
```

C++ 端必须繁琐地执行 **3 步握手**：
```cpp
// 1. 用字符串名称在 Program 中查询该 Buffer 块的内部索引 Index (开销大，需字符串 Hash 计算)
GLuint blockIndex = glGetProgramResourceIndex(program, GL_SHADER_STORAGE_BLOCK, "FCurrentIndirectWorkArgs");

// 2. 将 Program 内部索引 Index 关联绑定到 OpenGL 上下文的 Slot 0 槽位
glShaderStorageBlockBinding(program, blockIndex, 0);

// 3. 将 CPU/GPU 显存句柄 sWorkArgs[0] 挂载到 Slot 0 槽位
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sWorkArgs[0]);
```

#### B. 现代显式绑定 (`layout(binding=0)` - 仅需 1 步绑定)：
引入 `layout(binding=0)` 后，GLSL 开发者直接在 Shader 声明时 **硬编码绑定槽位**：

```glsl
// GLSL 代码（现代版）
layout(binding=0, std430) buffer FCurrentIndirectWorkArgs {
    uint mData[];
} CurrentIndirectWorkArgs;
```

C++ 端 **彻底省略了前 2 步** 的反射查询与映射代码（无需 `glGetProgramResourceIndex` 和 `glShaderStorageBlockBinding`），只需在 [RenderPass.cpp:L123](../src/Render/RenderPass.cpp#L123) 直接执行最关键的最后一步：
```cpp
// C++ 端只需这一行！直接将 Buffer 句柄连接到 GLSL 中指定的 N 号槽位
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, shaderResource->mBinding, shaderResource->mBuffer);
```

---

### 3. `layout(binding=N)` 的三大技术优势

1. **消除 CPU 字符串反射查询开销**：
   避免了在运行时使用字符串 `"FCurrentIndirectWorkArgs"` 去调用 `glGetProgramResourceIndex` 查询，省去了着色器反射 (Shader Reflection) 和 Hash 查找的开销。

2. **对接现代 API (Vulkan / Direct3D 12) 的设计思想**：
   Vulkan 和 DirectX 12 严格要求显式绑定（Vulkan 中的 `layout(set = M, binding = N)`）。使用 `layout(binding = N)` 能让 OpenGL 代码的体系架构与现代 API 的 **Descriptor Set（描述符集）** 和 **Root Signature（根签名）** 心智模型保持高度一致。

3. **提高代码可读性与前后端解耦**：
   图形程序员只要打开 `.glsl` 文件，一眼就能知道这个 Shader 依赖哪些槽位（Slot 0 是 WorkArgs，Slot 1 是 BVH，Slot 2 是 VisBuffer）。C++ 端与 GLSL 端的接口约定变得极度清晰，不易出错。

---

## 5. C++ 端与 Shader 缓冲绑定的底层映射机制

### 1. UBO (Uniform) 缓冲绑定的“双重映射”与两行 API 代码

在 [RenderPass.cpp:L115-L118](../src/Render/RenderPass.cpp#L115-L118) 中，绑定 UBO 统一常量缓冲使用了 **两行 API 调用**：

```cpp
// 行 1 (搭起桥梁 1)：将 Shader 中指定的 Block 映射绑定到硬件 UBO 槽位 uboSlot
OGL_CALL(glUniformBlockBinding(mComputeShader, shaderResource->mBinding, uboSlot));

// 行 2 (搭起桥梁 2)：将 CPU/GPU 端物理 UBO 缓冲句柄 mBuffer 挂载到对应的 uboSlot 槽位
OGL_CALL(glBindBufferBase(GL_UNIFORM_BUFFER, uboSlot++, shaderResource->mBuffer));
```

#### 覆盖权限 (Override) 与幂等性：
- **不会与 `layout(binding=0)` 发生冲突**：如果 Shader 中写了 `layout(binding=0)`，C++ 端调用 `glUniformBlockBinding(prog, 0, 0)` 只是再次确认绑定为 Slot 0，属于**幂等操作**，无副作用且绝不报错。
- **动态覆盖最高权限**：OpenGL 规范规定 C++ 端的 `glUniformBlockBinding` 具有动态覆盖权限。若 C++ 传入槽位 5，会直接覆盖 Shader 中的默认配置，强行将该 UBO 重定向连接至 5 号 Slot。
- **框架兼容性考虑**：保留两行代码保证了框架对于未写 `layout(binding=N)` 的旧版 Shader 具备 100% 的兼容性。

---

### 2. SSBO (Storage Buffer) 缓冲绑定的“单行 API”优化

对于 SSBO（如 `FCurrentIndirectWorkArgs`、`FBVH`、`FVisBuffer64`），请看 [RenderPass.cpp:L121-L125](../src/Render/RenderPass.cpp#L121-L125)：

```cpp
// 4. 循环绑定所有 SSBO
for (auto shaderResource : mBuffers) {
    // 仅需单行代码！完全不需要调用 glShaderStorageBlockBinding！
    OGL_CALL(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, shaderResource->mBinding, shaderResource->mBuffer));
}
```

因为 SSBO 在 GLSL 中全部声明了 `layout(binding=N)`，OpenGL 驱动在编译链接 Shader 时已自动构建好“桥梁 1”。C++ 端只需要调用单行 `glBindBufferBase` 完成“桥梁 2”的挂载，不存在任何多余冗余。

---

### 3. C++ 端识别与绑定 Uniform Block 的底层 API 参数机制

针对 [RenderPass.cpp:L116](../src/Render/RenderPass.cpp#L116) 中的 `glUniformBlockBinding(mComputeShader, shaderResource->mBinding, uboSlot)` 语句，其函数原型为：
```cpp
void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding);
```

* **第二个参数 `uniformBlockIndex`**：代表该 Uniform Block 在 Shader 内部被 OpenGL 编译器自动分配的**整数索引**（从 0 开始递增）。
* **在本项目中的精确匹配原因**：
  在 [NodeAndClusterCull.glsl](../Res/Shaders/NodeAndClusterCull.glsl)、[ClusterCull.glsl](../Res/Shaders/ClusterCull.glsl) 及 [HWRasterizeVS.glsl](../Res/Shaders/HWRasterizeVS.glsl) 中，**有且仅有一个 `uniform` Block（即 `GlobalConstants`）**。
  因此，OpenGL 编译器分配给 `GlobalConstants` 的 `uniformBlockIndex` 刚好就是 **`0`**。在 C++ 端 `scene.cpp` 中传入 `inBindingPoint = 0`，使 `shaderResource->mBinding` 为 **`0`**，正好精确匹配中了第 0 号 `uniformBlockIndex`！

#### 两种确定与映射 Uniform Block 的标准方式：
1. **标准反射定位 (`glGetUniformBlockIndex`)**：
   在复杂多 Block 的引擎中，C++ 拿着字符串 `"GlobalConstants"` 调用 `glGetUniformBlockIndex(program, "GlobalConstants")`，获取到其真实的整数 `blockIndex`，再传给 `glUniformBlockBinding` 的第二个参数。
2. **编号槽位契约 (`layout(binding=N)`)**：
   在现代 API 契约设计中，C++ 与 GLSL 遵守 **`binding = N` 的数字槽位约定**（例如 Slot 0 表示 GlobalConstants，Slot 1 表示 BVH，Slot 2 表示 WorkArgs），与 Shader 源码中的字符串物理行号解耦。

---

### 4. 项目中不同 Shader 的缓冲依赖映射关系

针对 RenderPass 执行时的资源绑定状态：
- **[RasterClear.glsl](../Res/Shaders/RasterClear.glsl)**：仅包含 `buffer`（SSBO），无 `uniform` Block。在 `sRasterClearPass->Execute()` 时，[RenderPass.cpp:L113-L119](../src/Render/RenderPass.cpp#L113-L119) 的 UBO 循环在运行时执行 0 次，SSBO 在 L121-L125 循环中通过单行 `glBindBufferBase` 完成绑定。
- **[NodeAndClusterCull.glsl](../Res/Shaders/NodeAndClusterCull.glsl)** / **[ClusterCull.glsl](../Res/Shaders/ClusterCull.glsl)** / **[HWRasterizeVS.glsl](../Res/Shaders/HWRasterizeVS.glsl)**：包含 `layout(binding=0) uniform GlobalConstants`，在 Pass 执行时建立与 CPU 端 `sGlobalConstants` 缓冲的无缝连接。

---

## 6. 核心设计意图：GPU-Driven 混合控制缓冲区

在 Nanite 渲染管线中，`sWorkArgs` 缓冲区（即 `CurrentIndirectWorkArgs` / `NextIndirectWorkArgs`）不仅是一个单纯的数据容器，而是一个 **GPU-Driven 混合控制缓冲区**：

1. **兼容 OpenGL 原生间接绘制命令 (`glDrawArraysIndirect`)**：
   OpenGL 的 `glDrawArraysIndirect` 要求显存的前 4 个 `uint32_t` 必须严格按照以下结构排列：
   - `mData[0]` = `count`（每簇顶点数，Nanite 中为 $128 \text{ Triangles} \times 3 = 384$）
   - `mData[1]` = **`instanceCount`（当前累积收集到的可见 Cluster 数量）**
   - `mData[2]` = `first`
   - `mData[3]` = `baseInstance`

2. **兼作 Compute Shader 的任务队列控制参数**：
   在紧接着该绘制命令之后的索引位置：
   - `mData[5]`：当前节点列表偏移量 (`currentNodeOffset`)
   - `mData[6]`：当前待遍历的 BVH 节点数量 (`currentNodeCount`)

3. **零 CPU 介入与 Ping-Pong 双缓冲**：
   - 每帧开始时在 [RasterClear.glsl](../Res/Shaders/RasterClear.glsl#L33-L42) 中由 GPU (0,0) 线程初始化：
     ```glsl
     CurrentIndirectWorkArgs.mData[0] = 384; // 每簇 384 顶点
     CurrentIndirectWorkArgs.mData[1] = 0;   // 初始可见 Cluster 数量清零
     CurrentIndirectWorkArgs.mData[5] = 0;   // 根节点 offset
     CurrentIndirectWorkArgs.mData[6] = 1;   // 待遍历根节点数 1
     ```
   - 在 [NodeAndClusterCull.glsl](../Res/Shaders/NodeAndClusterCull.glsl) 多次 Ping-Pong 迭代中，GPU 线程通过 `atomicAdd(NextIndirectWorkArgs.mData[6], childCount)` 动态追加下一层的节点。
   - 在 [ClusterCull.glsl](../Res/Shaders/ClusterCull.glsl) 中写入最终 `instanceCount`：

     | | **当前 Demo** | **目标架构** |
     |---|--------------|-------------|
     | Pass 3 并行 | `local_size_x=1` 单线程 `for` | 多 workgroup + `atomicAdd` |
     | `mData[1]` 写入 | 循环结束后一次性赋值 | 每可见簇 `atomicAdd(mData[1], 1)` |

   - 最后 CPU 端无缝调用 `glDrawArraysIndirect`，零 CPU 开销直接将 GPU 生成的 `instanceCount` 发送给图形硬件进行光栅化！
