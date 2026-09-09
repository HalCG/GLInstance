# OpenGLInstance

OpenGL 4.x 图形学 Demo 集合（完善中）。每个子项目为 **独立可执行程序**，围绕同一主题（抗锯齿、渲染路径、顺序无关透明等）提供可运行示例、分 Pass 性能统计与中文文档。

**仓库地址：** [https://github.com/HalCG/OpenGLInstance](https://github.com/HalCG/OpenGLInstance)

---

## 子项目一览

| 子项目 | 主题 | 运行时切换 / 要点 | 文档入口 |
|--------|------|-------------------|----------|
| [OpenGL_Anti_Aliasing](OpenGL_Anti_Aliasing/) | 抗锯齿 | `1`～`4`：None / MSAA / FXAA / TAA | [博客](OpenGL_Anti_Aliasing/docs/Anti_Aliasing_博客.md) · [代码导读](OpenGL_Anti_Aliasing/docs/Anti_Aliasing_代码导读.md) |
| [OpenGL_Rendering_Paths](OpenGL_Rendering_Paths/) | 渲染路径 | `1`～`3`：Forward / Deferred / Forward+；`[`/`]` 调光源数 | [博客](OpenGL_Rendering_Paths/docs/Rendering_Paths_博客.md) · [代码导读](OpenGL_Rendering_Paths/docs/Rendering_Paths_代码导读.md) |
| [OpenGL_OIT_Linked_list](OpenGL_OIT_Linked_list/) | OIT · 链表 | SSBO + 原子操作，逐像素收集透明片元 | [上篇](OpenGL_OIT_Linked_list/docs/OIT_Linked_List_上篇_原理与缓冲区设计.md) · [下篇](OpenGL_OIT_Linked_list/docs/OIT_Linked_List_下篇_Shader与渲染Pass实现.md) |
| [OpenGL_OIT_Depth_Peeling](OpenGL_OIT_Depth_Peeling/) | OIT · 深度剥离 | 多 Pass 逐层剥离，兼容性好 | [上篇](OpenGL_OIT_Depth_Peeling/docs/OIT_Depth_Peeling_上篇_原理与架构.md) · [下篇](OpenGL_OIT_Depth_Peeling/docs/OIT_Depth_Peeling_下篇_Shader与关键实现.md) |
| [OpenGL_OIT_Stochastic_Transparency](OpenGL_OIT_Stochastic_Transparency/) | OIT · 随机透明 | 单 Pass 随机 discard + 累积 | [上篇](OpenGL_OIT_Stochastic_Transparency/docs/OIT_Stochastic_Transparency_上篇_原理与架构.md) · [下篇](OpenGL_OIT_Stochastic_Transparency/docs/OIT_Stochastic_Transparency_下篇_Shader与关键实现.md) |

### RenderDoc 问题记录

开发过程中用 RenderDoc 排查过的问题归档（现象 / 定位 / 处理）：

| 子项目 | 文档 |
|--------|------|
| Anti_Aliasing | [RenderDoc_问题记录.md](OpenGL_Anti_Aliasing/docs/RenderDoc_问题记录.md) |
| Rendering_Paths | [RenderDoc_问题记录.md](OpenGL_Rendering_Paths/docs/RenderDoc_问题记录.md) |
| OIT Linked List | [RenderDoc_问题记录.md](OpenGL_OIT_Linked_list/docs/RenderDoc_问题记录.md) |
| OIT Depth Peeling | [RenderDoc_问题记录.md](OpenGL_OIT_Depth_Peeling/docs/RenderDoc_问题记录.md) |

### 按主题选读

- **多光源 / Forward vs Deferred vs Forward+** → `OpenGL_Rendering_Paths`
- **锯齿 / MSAA / 后处理 AA / TAA** → `OpenGL_Anti_Aliasing`
- **透明物体、与绘制顺序无关** → `OpenGL_OIT_*` 三个子项目横向对比

---

## 环境要求

- **系统：** Windows x64（当前 CMake 预设与依赖路径按 Windows 配置）
- **编译器：** Clang（推荐，见 `CMakePresets.json`）或 MSVC
- **构建：** CMake ≥ 3.10、Ninja（仓库自带 `tools/ninja.exe`）
- **依赖：** 位于 `modules/`（GLFW、GLAD、Assimp、polyclipping 等，由 `cmake/OpenGLWorkspaceDependencies.cmake` 统一查找）
- **OpenGL：** 核心 4.3+～4.6（各子项目要求略有不同，OIT Linked List 需 SSBO/原子操作）

---

## 构建

```bash
git clone https://github.com/HalCG/OpenGLInstance.git
cd OpenGLInstance

cmake --preset x64-clang-debug
cmake --build out/build/x64-clang-debug
```

只构建某一子项目：

```bash
cmake --build out/build/x64-clang-debug --target OpenGL_Rendering_Paths
cmake --build out/build/x64-clang-debug --target OpenGL_Anti_Aliasing
cmake --build out/build/x64-clang-debug --target OpenGL_OIT_Depth_Peeling
cmake --build out/build/x64-clang-debug --target OpenGL_OIT_Linked_list
cmake --build out/build/x64-clang-debug --target OpenGL_OIT_Stochastic_Transparency
```

VS Code / Cursor 可使用 `.vscode/tasks.json` 中的 **CMake: Configure Clang Debug** 与各 **Build: Clang Debug (...)** 任务。

---

## 运行

构建完成后，可执行文件位于：

```
out/build/x64-clang-debug/<子项目名>/<子项目名>.exe
```

**请在 exe 所在目录运行**（或在该目录启动调试），CMake 会将 `resources/` 与所需 DLL 部署到 exe 旁。各子项目的模型与 shader 位于 `<子项目>/resources/`。

通用相机（多数 Demo）：

| 操作 | 功能 |
|------|------|
| LMB | 旋转 |
| MMB | 平移 |
| RMB / 滚轮 | 缩放 |
| ESC | 退出 |

具体按键以各子项目窗口标题或 `docs/` 文档为准。

---

## 仓库结构

```
OpenGLInstance/
├── CMakeLists.txt              # 根 CMake，add_subdirectory 各 Demo
├── CMakePresets.json           # x64-clang-debug / release 预设
├── cmake/                      # 共享依赖查找、运行时资源部署
├── modules/                    # 第三方库（GLFW、Assimp、GLAD 等）
├── tools/                      # ninja 等构建工具
├── .vscode/                    # 配置、构建、调试任务
├── OpenGL_Anti_Aliasing/
├── OpenGL_Rendering_Paths/
├── OpenGL_OIT_Depth_Peeling/
├── OpenGL_OIT_Linked_list/
└── OpenGL_OIT_Stochastic_Transparency/
```

每个子项目典型布局：

```
OpenGL_<Name>/
├── main.cpp
├── CMakeLists.txt
├── include/ / src/
├── resources/          # shaders、models（构建时复制到 exe 旁）
└── docs/               # 博客、代码导读、OIT 上下篇等
```

---

## 设计约定

- **单 exe、可切换：** 对比类 Demo 尽量在一个程序内切换模式（如 AA 四种模式、三条渲染路径），便于公平对比性能。
- **分 Pass 计时：** 使用 `GL_TIME_ELAPSED` query；拖拽相机时通常关闭 GPU 计时以减少干扰。
- **事件驱动主循环：** 静止时 `glfwWaitEvents`；有输入或模式切换时再渲染，降低空转开销。
- **资源归属：** 各子项目自带 `resources/`，由 `ogl_ws_deploy_runtime()` 部署到输出目录。

---

## 文档阅读建议

1. 先打开对应子项目的 **博客 / 上篇** 建立概念与管线图。
2. 再读 **代码导读**（若有）按模块跟源码。
3. 运行 Demo，对照窗口标题 Pass 耗时与控制台 CSV 做实验记录。

---

## License

各子项目代码与文档以仓库现状为准；`modules/` 内第三方库遵循其各自许可证。
