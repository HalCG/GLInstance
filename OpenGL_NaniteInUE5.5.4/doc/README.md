# OpenGL_NaniteInUE5.5.4 文档索引

> 第一次读项目：**[Code_Reading_Guide.md](./Code_Reading_Guide.md)** → 按需查下表。

## 推荐阅读顺序

| 顺序 | 文档 | 何时读 |
|:--:|------|--------|
| 1 | [Code_Reading_Guide.md](./Code_Reading_Guide.md) | 第一次读源码，建立 Pass 全局图景 |
| 2 | [Nanite_Data_Structures.md](./Nanite_Data_Structures.md) | 查 BVH / NaniteMesh / SSBO 字段与寻址 |
| 3 | [GPU_Driven_SSBO_Layout_Guide.md](./GPU_Driven_SSBO_Layout_Guide.md) | 不懂 `std430`、`binding=N`、WorkArgs 时 |
| 4 | [Hardware_Rasterization_Guide.md](./Hardware_Rasterization_Guide.md) | 读 Pass 4 硬件光栅化时 |
| 5 | [Nanite_BugFixes.md](./Nanite_BugFixes.md) | 遇到类似 bug 或 VisBuffer 行为异常时 |

## 全部文档

| 文档 | 主题 |
|------|------|
| [Project_Structure.md](./Project_Structure.md) | 目录结构与构建说明 |
| [Code_Reading_Guide.md](./Code_Reading_Guide.md) | 代码导读（Pass 串联、FAQ） |
| [Nanite_Data_Structures.md](./Nanite_Data_Structures.md) | **BVH / NaniteMesh / 运行时缓冲数据结构参考** |
| [COMMENT_STYLE.md](./COMMENT_STYLE.md) | 代码注释风格与文件头模板 |
| [GPU_Driven_SSBO_Layout_Guide.md](./GPU_Driven_SSBO_Layout_Guide.md) | SSBO 布局与绑定点 |
| [OpenGL_Indirect_Drawing_Guide.md](./OpenGL_Indirect_Drawing_Guide.md) | `glDrawArraysIndirect` |
| [GLSL_ComputeShader_InvocationID_Guide.md](./GLSL_ComputeShader_InvocationID_Guide.md) | Compute 坐标内置变量 |
| [GLSL_ComputeShader_Thread_Boundary_Guide.md](./GLSL_ComputeShader_Thread_Boundary_Guide.md) | Compute 线程边界 |
| [Hardware_Rasterization_Guide.md](./Hardware_Rasterization_Guide.md) | Pass 4 硬件光栅化 |
| [GPU_Pipeline_Between_VS_and_FS.md](./GPU_Pipeline_Between_VS_and_FS.md) | VS/FS 间固定功能管线 |
| [Nanite_SW_vs_HW_Rasterization_Guide.md](./Nanite_SW_vs_HW_Rasterization_Guide.md) | SW/HW 双路径理论（SW 未实现） |
| [Nanite_BugFixes.md](./Nanite_BugFixes.md) | 已修复问题记录 |
| [Code_Optimization_TODO.md](./Code_Optimization_TODO.md) | 优化与重构计划 |

## 图示资源

| 目录 | 内容 |
|------|------|
| [figures/BVH_NaniteMesh/](./figures/BVH_NaniteMesh/) | BVH 与 NaniteMesh 结构分步图（00–07） |
| [figures/GetProjectionScales/](./figures/GetProjectionScales/) | `GetProjectionScales` 算法分步图（00–05） |
