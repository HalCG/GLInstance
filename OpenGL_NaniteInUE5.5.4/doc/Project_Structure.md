# 项目目录结构

> 源码按模块分目录；`Res/` 为运行时资源；`ThirdParty/` 为第三方头文件；不参与编译的备份与工具代码单独放置。

```
OpenGL_NaniteInUE5.5.4/
├── src/                          # C++ 源码（按模块分类）
│   ├── App/                      # 程序入口（Win32 窗口、主循环）
│   │   └── main.cpp
│   ├── Core/                     # 通用工具（文件加载等）
│   │   ├── utils.cpp
│   │   └── utils.h
│   ├── Math/                     # 数学库
│   │   ├── float4.*
│   │   ├── matrix4.*
│   │   └── quaternion.*
│   ├── Platform/                 # OpenGL 上下文、扩展、GlobalConstants
│   │   ├── oglcontext.cpp
│   │   └── oglcontext.h
│   ├── Camera/                   # 轨迹球相机
│   │   ├── trackball_camera.cpp
│   │   └── trackball_camera.h
│   ├── Render/                   # RenderPass、网格、全屏四边形
│   │   ├── RenderPass.*
│   │   ├── StaticMesh.*
│   │   └── fullscreenquad.*
│   └── Scene/                    # Nanite 管线编排（Init / RenderOneFrame）
│       ├── scene.cpp
│       └── scene.h
│
├── ThirdParty/
│   ├── GL/                       # GLEW 头文件（#include "GL/glew.h"）
│   └── GLEW/
│       ├── lib/x64/glew32.lib    # 链接库（x64）
│       └── bin/x64/glew32.dll    # 运行时 DLL（构建后自动拷贝到输出目录）
│
├── scripts/
│   └── CopyRuntimeDeps.cmd       # Post-Build：检查并部署 glew32.dll、Res/
│
├── Res/                          # 运行时资源（构建后复制到输出目录 Res/）
│   ├── Shaders/                  # GLSL 着色器
│   ├── mitsuba.bvh               # BVH 数据（若存在）
│   ├── mitsuba.nanitemesh        # Nanite 网格数据（若存在）
│   └── Tools/                    # 离线工具源码（不纳入 Nanite.vcxproj）
│       ├── NaniteEncode.cpp
│       └── AssetDefinition_StaticMesh.cpp
│
├── legacy/                       # 历史备份，不参与编译
│   └── RenderPass_20251228_165222.cpp
│
├── doc/                          # 设计与导读文档
├── Nanite.sln
├── Nanite.vcxproj
└── README.md
```

## 编译与运行

- **IDE**：Visual Studio 2026（工具集 v145 / VS 18）
- **配置**：`Debug | x64` 或 `Release | x64`
- **输出**：`x64/Debug/Nanite.exe`（或 `x64/Release/`）
- **工作目录**：调试时设为 `$(OutDir)`（与 exe 同目录，可直接加载 `glew32.dll` 与 `Res/`）
- **Post-Build**：`scripts/CopyRuntimeDeps.cmd` 检查输出目录，缺失则拷贝 `glew32.dll` 与 `Res/`

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  Nanite.sln /p:Configuration=Debug /p:Platform=x64
```

## Include 路径

`Nanite.vcxproj` 已将 `src` 下各模块目录与 `ThirdParty` 加入 `AdditionalIncludeDirectories`，  
源文件之间仍可使用 `#include "scene.h"` 等短名包含（无需写 `src/Scene/scene.h`）。
