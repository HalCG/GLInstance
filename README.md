# OpenGLInstance

OpenGL 4.x 图形学 Demo 集合（完善中）。每个子项目为 **独立可执行程序**，围绕同一主题（抗锯齿、顺序无关透明 等）提供可运行示例与中文文档。

**仓库地址：** [https://github.com/HalCG/OpenGLInstance](https://github.com/HalCG/OpenGLInstance)

---

## 子项目一览

| 子项目 | 主题 | 运行时切换 / 要点 | 文档入口 |
|--------|------|-------------------|----------|
| [OpenGL_Anti_Aliasing](OpenGL_Anti_Aliasing/) | 抗锯齿 | `1`～`4`：None / MSAA / FXAA / TAA | [README](OpenGL_Anti_Aliasing/README.md) · [技术大纲](OpenGL_Anti_Aliasing/docs/README.md) |
| [OpenGL_OIT_Depth_Peeling](OpenGL_OIT_Depth_Peeling/) | OIT · 深度剥离 | 多 Pass 逐层剥离 | [README](OpenGL_OIT_Depth_Peeling/README.md) · [技术大纲](OpenGL_OIT_Depth_Peeling/docs/README.md) · [OIT_MD 笔记](OpenGL_OIT_Depth_Peeling/OIT_MD/) |
| [OpenGL_OIT_Stochastic_Transparency](OpenGL_OIT_Stochastic_Transparency/) | OIT · 随机透明 | 单 Pass 随机 discard + 累积 | [README](OpenGL_OIT_Stochastic_Transparency/README.md) · [技术大纲](OpenGL_OIT_Stochastic_Transparency/docs/README.md) |

### 按主题选读

- **锯齿 / MSAA / FXAA / TAA** → `OpenGL_Anti_Aliasing`
- **透明、深度剥离** → `OpenGL_OIT_Depth_Peeling`
- **透明、随机采样** → `OpenGL_OIT_Stochastic_Transparency`

---

## 环境要求

- **系统：** Windows x64
- **工具链：** Visual Studio 2022（MSVC）+ CMake ≥ 3.10
- **依赖：** 位于 `modules/`（GLFW、GLAD、Assimp 等，由 `cmake/OpenGLWorkspaceDependencies.cmake` 统一查找）
- **OpenGL：** 核心 4.3+～4.6（各子项目要求略有不同）

当前工程**仅维护** `x64-msvc-2022` 一套构建预设。

---

## 构建

```bash
git clone https://github.com/HalCG/OpenGLInstance.git
cd OpenGLInstance

cmake --preset x64-msvc-2022
cmake --build out/build/x64-msvc-2022 --config Debug
```

只构建某一个 Demo：

```bash
cmake --build out/build/x64-msvc-2022 --config Debug --target OpenGL_Anti_Aliasing
cmake --build out/build/x64-msvc-2022 --config Debug --target OpenGL_OIT_Depth_Peeling
cmake --build out/build/x64-msvc-2022 --config Debug --target OpenGL_OIT_Stochastic_Transparency
```

VS Code：`.vscode/tasks.json` 的 **CMake: Configure MSVC 2022** 与各 **Build: MSVC 2022 (...)**；调试用 `.vscode/launch.json`。

---

## 运行

构建完成后，可执行文件位于：

```
run/Debug/<子项目名>/<子项目名>.exe
```

POST_BUILD 会将 `resources/` 与 Assimp 等运行时 DLL 拷贝到 exe 旁。**请在 exe 所在目录运行**（或用 launch 配置启动）。Release 对应 `run/Release/<子项目名>/`。

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
├── CMakePresets.json           # 仅 x64-msvc-2022
├── cmake/                      # 共享依赖查找、run/ 输出、资源与 DLL 部署
├── modules/                    # 第三方库（GLFW、Assimp、GLAD 等）
├── run/                        # Demo 可执行文件（gitignore，构建后生成）
├── out/build/                  # CMake 中间产物（gitignore）
├── .vscode/                    # MSVC 2022 构建与调试
├── OpenGL_Anti_Aliasing/
├── OpenGL_OIT_Depth_Peeling/
└── OpenGL_OIT_Stochastic_Transparency/
```

每个子项目典型布局：

```
OpenGL_<Name>/
├── main.cpp
├── CMakeLists.txt
├── include/ / src/
├── resources/          # shaders、models（构建时复制到 exe 旁）
├── build/              # Windows MSVC Debug/Release 脚本
└── docs/ 或 doc/       # 技术大纲 / 专题笔记
```

---

## 设计约定

- **单 exe、可切换：** 对比类 Demo 尽量在一个程序内切换模式，便于公平对比性能。
- **分 Pass 计时：** 使用 `GL_TIME_ELAPSED` query；拖拽相机时通常关闭 GPU 计时以减少干扰。
- **事件驱动主循环：** 静止时 `glfwWaitEvents`；有输入或模式切换时再渲染，降低空转开销。
- **资源归属：** Demo 自带 `resources/`，构建后由 `ogl_ws_deploy_static_resources()` 拷贝到 `run/` 下 exe 旁；Assimp 等 DLL 由 `ogl_ws_link_assimp()` 自动部署。

---

## 文档阅读建议

1. 先打开对应子项目的 **README** 与 **docs/README（技术大纲）** 建立概念。
2. 再按专题文档深入源码与实验记录。
