# OpenGL Image Unit 与 `glBindImageTexture`

> 本文对应本子项目里的**头指针纹理**（`headPtrTexture_` / `uimage2D headPointers`）。  
> 目标：弄清 **Image Unit** 是什么、和 Texture Unit / Buffer binding 的关系，以及 **有 / 无 `layout(binding)`** 两种写法。  
> 可与同目录 [OpenGL_Buffer绑定与Data_SubData.md](./OpenGL_Buffer绑定与Data_SubData.md) 对照阅读（那边讲 Buffer；本文讲 Image）。

---

## 1. 这段代码在干什么

```cpp
// LinkedListOITApp.cpp — 创建头指针纹理存储后：
glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, width_, height_);

// 把纹理挂到 Shader Image Unit 0，允许读写
glBindImageTexture(0, headPtrTexture_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
```

对上片元着色器：

```glsl
// oitRender.frag
layout(binding = 0, r32ui) uniform uimage2D headPointers;

uint preHead = imageAtomicExchange(headPointers, ivec2(gl_FragCoord.xy), index);
```

**一句话**：Image Unit 0 = 第 0 号「按像素读写纹理」的绑定槽；CPU 用 `glBindImageTexture(0, ...)` 把头指针图挂上去，Shader 才能对每个像素做原子头插。

---

## 2. Image Unit 是什么？

**Image Unit** 是 OpenGL 给 **image load/store**（以及 image 原子操作）准备的一组**编号绑定点**。

和日常「采样纹理」用的 **Texture Unit** 不是同一套：

| | Texture Unit | Image Unit |
| :--- | :--- | :--- |
| GLSL 类型 | `sampler2D` 等 | `image2D` / `uimage2D` / `iimage2D` |
| 典型用法 | `texture(sampler, uv)` 滤波采样 | `imageLoad` / `imageStore` / **`imageAtomic*`** |
| CPU 绑定 | `glActiveTexture` + `glBindTexture` | **`glBindImageTexture(unit, tex, ...)`** |
| 本工程例子 | 漫反射、不透明深度图 | **每像素链表头指针**（必须原子读写） |

链表头指针必须用 Image，而不能只用 `sampler2D`：  
要对同一像素做「读旧头 → 写新头」且多片元并发，需要 **`imageAtomicExchange`**，这只挂在 Image Unit 路径上。

---

## 3. 和「绑定点」的关系

是的，**和 Buffer 的 `glBindBufferBase` 同一类模型**：

```text
Shader:  layout(binding = N) ...
CPU:     把资源挂到「索引 N」
```

| 资源 | Shader | CPU |
| :--- | :--- | :--- |
| Atomic / SSBO / UBO | `layout(binding = N)` | `glBindBufferBase(target, N, buf)` |
| Image 纹理 | `layout(binding = N, r32ui) uniform uimage2D ...` | **`glBindImageTexture(N, tex, ...)`** |
| 普通采样纹理 | `layout(binding = N) uniform sampler2D`（或 `glUniform1i`） | `glActiveTexture(GL_TEXTURE0+N)` + `glBindTexture` |

本工程里三个都是 `binding = 0` 也不冲突——分属 **Image / Atomic / SSBO** 不同绑定域：

```glsl
layout(binding = 0, r32ui) uniform uimage2D headPointers;           // Image Unit 0
layout(binding = 0, offset = 0) uniform atomic_uint nextNodeCounter; // Atomic binding 0
layout(binding = 0, std430) buffer linkedLists { ... };             // SSBO binding 0
```

---

## 4. `glBindImageTexture` 参数（对照本工程）

原型：

```cpp
void glBindImageTexture(
    GLuint unit, GLuint texture, GLint level,
    GLboolean layered, GLint layer,
    GLenum access, GLenum format);
```

本工程调用与逐参数含义：

```cpp
glBindImageTexture(
    0,                 // unit    ：Image Unit 编号 ↔ layout(binding = 0)
    headPtrTexture_,   // texture ：要挂的纹理对象名
    0,                 // level   ：使用的 mip 层级（只要 level 0）
    GL_FALSE,          // layered ：FALSE = 不绑整个数组/3D 层集，只绑单层 2D
    0,                 // layer   ：layered=FALSE 时忽略；TRUE 时表示绑哪一层
    GL_READ_WRITE,     // access  ：着色器访问权限（见下表）
    GL_R32UI           // format  ：image 视图格式，须与纹理内部格式兼容
);
```

| 参数 | 本工程值 | 说明 |
| :--- | :--- | :--- |
| `unit` | `0` | Image Unit 索引，必须对上 `layout(binding = 0)` |
| `texture` | `headPtrTexture_` | 已用 `glTexStorage2D` 分配好的头指针纹理 |
| `level` | `0` | 只使用第 0 级 mip |
| `layered` | `GL_FALSE` | 普通 2D，不是 array/3D/cube 整层绑定 |
| `layer` | `0` | `layered=FALSE` 时无效 |
| `access` | `GL_READ_WRITE` | 头插既要读旧头又要写新头；还可选 `GL_READ_ONLY` / `GL_WRITE_ONLY` |
| `format` | `GL_R32UI` | 与 `TexStorage` 的 `GL_R32UI`、shader 的 `r32ui` 一致，否则未定义行为 |

前面创建存储（只做一次，resize 时重建）：

```cpp
glGenTextures(1, &headPtrTexture_);
glBindTexture(GL_TEXTURE_2D, headPtrTexture_);
glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, width_, height_); // 分配不可变存储（定格式/尺寸）
```

`glTexStorage2D` 是**分配存储**，不是采样 wrap/filter；  
`glBindImageTexture` 才是**挂到 Image Unit** 给 shader 读写。

---

## 4.1 每帧清空头指针（摘要）

Transparent Pass 前用 PBO + `glTexSubImage2D` 把头指针刷成 `0xFFFFFFFF`。  
**`glBindImageTexture` 与清空无关**；`GL_PIXEL_UNPACK_BUFFER` 为何是上传源、`glTexSubImage2D` 全参数与顺序，见专题：

→ **[OpenGL_PBO与TexSubImage2D清头指针.md](./OpenGL_PBO与TexSubImage2D清头指针.md)**

---

## 5. 绑定号 N 怎么定：两种等价约定

可以这么理解：

1. **不在 Shader 里指定 binding** → 在 **CPU** 里定号（`glUniform1i` 等），再 `BindImageTexture(N, …)` 绑定；  
2. **在 Shader / GLSL 里写死 `layout(binding = N)`** → **CPU 侧用同一个 N 同步绑定**即可。

两边最终约定的都是同一个编号 N；**不是可以不绑**，只是「谁先定号」。本工程用写法 2。

```text
路径 A：Shader 不写 binding  →  CPU 指定 N  →  BindImageTexture(N, ...)
路径 B：Shader 写 binding=N  →  CPU 对齐同一 N → BindImageTexture(N, ...)
```

### 5.1 路径 B：Shader 写死 binding（本工程，推荐）

```glsl
layout(binding = 0, r32ui) uniform uimage2D headPointers;  // GPU/GLSL 侧先定 N=0
```

```cpp
// CPU 侧同步用同一个 0，再绑定
glBindImageTexture(0, headPtrTexture_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
```

### 5.2 路径 A：Shader 不写 binding（编号全由 CPU 定）

```glsl
layout(r32ui) uniform uimage2D headPointers; // 没有 binding =
```

```cpp
GLint loc = glGetUniformLocation(program, "headPointers");
const GLint imageUnit = 2;        // CPU 决定 N=2

glUniform1i(loc, imageUnit);      // 告诉程序：这个 uniform 用 Image Unit 2
glBindImageTexture(imageUnit, headPtrTexture_, 0, GL_FALSE, 0,
                   GL_READ_WRITE, GL_R32UI);  // 再绑到 2
```

和 UBO/SSBO「没写 binding → `glUniformBlockBinding` / `glShaderStorageBlockBinding` + `BindBufferBase`」同思路。

| | 路径 B：GLSL 定 N | 路径 A：CPU 定 N |
| :--- | :--- | :--- |
| Shader | `layout(binding = N, r32ui) uniform uimage2D ...` | `layout(r32ui) uniform uimage2D ...` |
| CPU | 直接 `glBindImageTexture(N, ...)`（与 GLSL 对齐） | `GetUniformLocation` + **`glUniform1i(loc, N)`** + `glBindImageTexture(N, ...)` |

**无论哪种，最后都要 `glBindImageTexture`。**  
差别只是 N 写在 GLSL 里，还是只在 CPU 里指定——**不是可以不绑，只是谁来定号。**

> 注意：`glGetUniformLocation` 得到的是 **uniform location**，  
> `glUniform1i` 写入的值才是 **Image Unit 编号**。不要把 location 和 unit 混成一个数。

Buffer（Atomic / SSBO / UBO）同一套「路径 A / B」约定，见 [OpenGL_Buffer绑定与Data_SubData.md](./OpenGL_Buffer绑定与Data_SubData.md) §5.0。

---

## 6. 和 Texture Unit 路径对比（本工程里两种都有）

```glsl
uniform sampler2D texture_diffuse;  // 普通采样
uniform sampler2D texture_depth;

layout(binding = 0, r32ui) uniform uimage2D headPointers; // image 读写
```

| 需求 | 用哪条路 |
| :--- | :--- |
| 读漫反射、读不透明深度 | Texture Unit + `texture()` |
| 每像素原子更新链表头 | **Image Unit** + `imageAtomicExchange` |

---

## 7. 头插法里 Image 扮演的角色

```text
片元到达像素 (x,y)
    │
    ├─ atomicCounterIncrement → 全局唯一 node index
    │
    └─ imageAtomicExchange(headPointers, (x,y), index)
           读出旧头 preHead，写成新头 index
           节点.next = preHead   ← 无锁头插
```

没有 Image Unit 绑定，这一步无从谈起；  
没有 `GL_READ_WRITE`，也不能又读又写。

每帧还要用 PBO / clear 把整张头指针图刷成 `0xFFFFFFFF`（空链表），那是内容重置，和「绑到哪个 Unit」是另一件事。

---

## 8. 常见踩坑

| 现象 | 原因 |
| :--- | :--- |
| `imageAtomicExchange` 无效 / 花屏 | 漏了 `glBindImageTexture`，或 unit 与 `binding` 不一致 |
| 格式报错 | `glBindImageTexture` 的 format 与 `TexStorage` 的内部格式不匹配（此处都是 `GL_R32UI` / `r32ui`） |
| 和 `sampler2D` 搞混 | 原子读写头指针必须走 image，不能靠 `texture()` |
| 以为 `location` 能代替 | image 走的是 **binding / Image Unit**，不是顶点那种 `layout(location)` |

---

## 9. 速查

```text
分配头指针纹理存储     glTexStorage2D(..., GL_R32UI, w, h)
挂到 Image Unit N      glBindImageTexture(unit, tex, level, layered, layer, access, format)
Shader 声明            layout(binding = N, r32ui) uniform uimage2D ...
无 binding 时           glUniform1i(loc, N) + BindImageTexture(N, ...)
每帧清空头指针         Bind PBO + BindTexture + TexSubImage2D(..., nullptr)

Texture Unit           采样用
Image Unit             按像素 load/store / 原子操作用
```

---

## 10. 相关文件

| 文件 | 内容 |
| :--- | :--- |
| `src/LinkedListOITApp.cpp` | `glTexStorage2D`、`glBindImageTexture`、PBO 清头指针 |
| `resources/oitRender.frag` | `uimage2D headPointers`、`imageAtomicExchange` |
| [OpenGL_Buffer绑定与Data_SubData.md](./OpenGL_Buffer绑定与Data_SubData.md) | Atomic / SSBO 的 `BindBufferBase` 与 `SubData` |
| `docs/README.md` | 链表 OIT 全流程 |

---

## 11. 一句话

**Image Unit = shader 按像素读写（含原子）纹理的编号槽。**  
绑定号 N 可在 Shader 写死再 CPU 对齐，或只由 CPU `glUniform1i` 指定；**最后都要 `glBindImageTexture(N, …)`。**
