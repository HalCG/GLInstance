# Nanite OpenGL 实现 - 核心 Bug 修复与技术细节记录

本文档记录了项目在 Nanite 渲染管线、Shader 逻辑与数学计算中修复的核心渲染缺陷与技术细节。

---

## 1. VisBuffer 清理索引错位（解决画面闪烁）

- **文件位置**：[Res/Shaders/RasterClear.glsl](../Res/Shaders/RasterClear.glsl#L29)
- **问题代码**：
  ```glsl
  int pixelIndex=texcoord.x*720+texcoord.y;
  ```
- **正确代码**：
  ```glsl
  int pixelIndex=texcoord.y*1280+texcoord.x;
  ```
- **技术原理**：
  - `HWRasterizeFS.glsl` 和 `Visualization.glsl` 中的 VisBuffer 64位无符号整型数组布局均为**行优先**存储 `y * 1280 + x`。
  - 原 `RasterClear.glsl` 错误使用了**列优先**存储 `x * 720 + y`。
  - 由于索引映射错位，每帧开始时 99% 以上像素的 VisBuffer 深度并没有被成功重置为默认值 `0xFFFFFFFF00000000ul`，保留了历史帧的深度。
  - 硬件光栅化阶段片段着色器执行 `atomicMin(VisBuffer64.mData[pixelIndex], pixelValue64)` 时，新三角形的深度与残留的历史深度发生碰撞，导致画面在镜头静止或移动时产生严重闪烁。

---

## 2. GPU 间接绘制命令屏障缺失（解决 GPU 读写竞态条件）

- **文件位置**：[RenderPass.cpp](../src/Render/RenderPass.cpp#L124)
- **问题代码**：
  ```cpp
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  ```
- **正确代码**：
  ```cpp
  glMemoryBarrier(GL_ALL_BARRIER_BITS);
  ```
- **技术原理**：
  - 在 Compute Shader（`ClusterCull.glsl`）执行完毕后，生成的可见簇数量 `visiableClusterCount` 写入 SSBO 缓冲区 `sWorkArgs[0]`。
  - `sWorkArgs[0]` 紧接着被作为 `glDrawArraysIndirect` 的命令缓冲区传入。
  - 规范要求 `glDrawArraysIndirect` 读取间接命令前必须触发 `GL_COMMAND_BARRIER_BIT` 屏障。原本仅使用的 `GL_SHADER_STORAGE_BARRIER_BIT` 无法保证硬件绘制命令队列（Command Processor）同步完成数据读取。
  - 将屏障升级为 `GL_ALL_BARRIER_BITS` 后，消除了 GPU Command Fetch 与 CS SSBO 写入之间的 Race Condition，确保间接绘制参数稳定。

---

## 3. 透视投影近裁剪面过大（解决近距离放大时的方形裁剪）

- **文件位置**：[scene.cpp](../src/Scene/scene.cpp#L42)
- **问题代码**：
  ```cpp
  sProjectionMatrix.Perspective(90.0f, float(inCanvasWidth) / float(inCanvasHeight), 10.0f, 10000.0f);
  ```
- **正确代码**：
  ```cpp
  sProjectionMatrix.Perspective(90.0f, float(inCanvasWidth) / float(inCanvasHeight), 1.0f, 10000.0f);
  ```
- **技术原理**：
  - 透视投影矩阵设置的近裁剪平面 `ZNear = 10.0f` 过大。
  - 当使用 Trackball 相机拉近视距观察网格体时，相机近裁面穿透球体几何表面，将三维球体切出一个与 Viewport 近裁面平行的二维平面截面，表现为“周围多了一个方形几何”。
  - 将 `ZNear` 缩小为 `1.0f` 后，近距离观察时不再触发几何切面。

- **遗留不一致（未修复）**：
  - `ClusterCull.glsl` / `NodeAndClusterCull.glsl` 的 `GetProjectionScales()` 内仍硬编码 `ZNear = 10.0f`，与 C++ `near=1.0f` 不一致。
  - 影响：近距离 LOD 屏幕尺度计算可能与 C++ 投影假设略有偏差；不影响 VisBuffer 主流程。
  - 目标：将 `ZNear` 写入 `GlobalConstants` UBO，C++ 与 Shader 共用同一值。

---

## 4. GLSL 位运算符优先级错误（解决 Cluster 颜色映射失真）

- **文件位置**：[Res/Shaders/Visualization.glsl](../Res/Shaders/Visualization.glsl#L38)
- **问题代码**：
  ```glsl
  uint clusterIndex=pixelValue&0xFFu - 1;
  ```
- **正确代码**：
  ```glsl
  uint clusterIndex=(pixelValue&0xFFu) - 1;
  ```
- **技术原理**：
  - 在 GLSL 语言标准中，算术运算符 `-` 的优先级高于按位与运算符 `&`。
  - 原代码 `pixelValue & 0xFFu - 1` 被编译器隐式解析为 `pixelValue & (0xFFu - 1)`，即 `pixelValue & 0xFEu`。
  - 这清除了 Cluster ID 的最低位（Bit 0），导致相邻的奇数 Cluster ID 和偶数 Cluster ID 被映射到了完全相同的颜色调色板。
  - 加上显式括号后，解开了位提取与减法运算，能准确恢复唯一的 Cluster ID。

---

## 5. 四元数转 3x3 矩阵公式修正（解决绕 X 轴旋转变形）

- **文件位置**：[quaternion.cpp](../src/Math/quaternion.cpp#L68)
- **问题代码**：
  ```cpp
  m._32 = 2.0f * yz + 2.0f * wx;
  ```
- **正确代码**：
  ```cpp
  m._32 = 2.0f * yz - 2.0f * wx;
  ```
- **技术原理**：
  - 在单位四元数 $q = (w, x, y, z)$ 转 3x3 旋转矩阵的标准数学推导中，非对角线镜像元素 $(m_{23}, m_{32})$ 的 $wx$ 交叉项符号必须相反：
    $$m_{23} = 2yz + 2wx, \quad m_{32} = 2yz - 2wx$$
  - 原代码误将 `m._32` 错写为加号 `+`（与 `m._23` 误复制重复），导致绕 X 轴旋转矩阵的正弦项 $\sin\theta$ 符号非对称，引发旋转时的非刚体拉伸变形。
  - 修正为 `-` 减号后，恢复了标准 X 轴旋转矩阵 $R_x(\theta)$ 的对称性与正确的旋转向量计算。

