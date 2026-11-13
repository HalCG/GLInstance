# PBO 与 `glTexSubImage2D`：每帧清空头指针纹理

> 对应代码：`LinkedListOITApp.cpp` Transparent Pass 前（约 486–491 行）。  
> Image Unit 绑定细节见 [OpenGL_ImageUnit与BindImageTexture.md](./OpenGL_ImageUnit与BindImageTexture.md)。

---

## 1. 这段代码在干什么

```cpp
glBindImageTexture(0, headPtrTexture_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI); // A：挂 Image
glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);   // B1：上传源 = PBO
glBindTexture(GL_TEXTURE_2D, headPtrTexture_);     // B2：上传目标
glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_,
                GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr); // B3：整幅覆盖
glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
```

目标：把 `headPtrTexture_` 每个像素写成 `0xFFFFFFFF`（空链表头），再画透明片元做头插。

| 步骤 | API | 分工 |
| :--- | :--- | :--- |
| A | `glBindImageTexture` | 挂 Image Unit 0，供后续 `imageAtomicExchange`；**不参与清空** |
| B1 | `glBindBuffer(GL_PIXEL_UNPACK_BUFFER, …)` | 指定像素**上传源**为 `clearBuf_` |
| B2 | `glBindTexture` | 指定**上传目标**纹理 |
| B3 | `glTexSubImage2D(..., nullptr)` | 从 PBO 整幅写入 level 0 |
| 收尾 | `glBindBuffer(..., 0)` | 解开 PBO，避免影响后续像素命令 |

```text
clearBuf_ (PBO, 全 0xFFFFFFFF)
        │  PIXEL_UNPACK_BUFFER
        ▼
glTexSubImage2D ──► headPtrTexture_（每像素头 = 空）
```

---

## 2. 行为何是「上传源」：`GL_PIXEL_UNPACK_BUFFER`

### 2.1 这是 OpenGL 的 target 约定

`glTexSubImage2D` / `glTexImage2D` 规定：

- **没绑** `GL_PIXEL_UNPACK_BUFFER`：最后一参是 **CPU 内存指针**；
- **绑了** `GL_PIXEL_UNPACK_BUFFER`：最后一参是 **PBO 内字节偏移**（`nullptr` / `0` = 从开头读）。

因此「上传源」不是额外声明的，而是：

```text
往纹理装像素的命令（Tex*Image）
  + 当前 PIXEL_UNPACK_BUFFER
  = 从这块 buffer 读像素
```

### 2.2 PACK / UNPACK

| Target | 方向 | 典型命令 |
| :--- | :--- | :--- |
| **`GL_PIXEL_UNPACK_BUFFER`** | Buffer → 纹理（上传） | `glTexImage2D`、`glTexSubImage2D` |
| **`GL_PIXEL_PACK_BUFFER`** | 纹理/FBO → Buffer（下载） | `glReadPixels`、`glGetTexImage` |

**UNPACK** = 把紧凑像素数组解开写进纹理；**PACK** = 从纹理打包读回 buffer。

### 2.3 PBO 是什么

**PBO（Pixel Buffer Object）**= 普通 `glGenBuffers` 的 buffer，绑到像素传输 target，当像素搬运中转站。

本工程初始化（只做一次，resize 重建）：

```cpp
std::vector<GLuint> headPtrClearBuf(width_ * height_, 0xffffffff);
glGenBuffers(1, &clearBuf_);
glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);
glBufferData(GL_PIXEL_UNPACK_BUFFER,
             headPtrClearBuf.size() * sizeof(GLuint),
             headPtrClearBuf.data(), GL_STATIC_COPY);
```

之后每帧不必再从 CPU 指针灌一整幅，只需 `TexSubImage2D(..., nullptr)` 走 PBO→纹理。

| 方式 | 每帧 |
| :--- | :--- |
| 不用 PBO | `TexSubImage2D(..., cpuPtr)`，客户端路径更重 |
| 用 PBO | 数据已在 GPU 侧 buffer，适合每帧整幅刷 NULL |

和其他 target 对比：都是 Buffer Object，**角色由当前 target + 后续命令**决定。

| Target | 角色 |
| :--- | :--- |
| `GL_ARRAY_BUFFER` | 顶点数据 |
| `GL_SHADER_STORAGE_BUFFER` | SSBO |
| `GL_ATOMIC_COUNTER_BUFFER` | 原子计数器 |
| **`GL_PIXEL_UNPACK_BUFFER`** | **像素上传源** |

---

## 3. `glTexSubImage2D` 参数（本工程）

原型：

```cpp
void glTexSubImage2D(
    GLenum target, GLint level,
    GLint xoffset, GLint yoffset,
    GLsizei width, GLsizei height,
    GLenum format, GLenum type,
    const void *pixels);
```

本工程：

```cpp
glTexSubImage2D(
    GL_TEXTURE_2D,   // target  ：2D 纹理
    0,               // level   ：mip 0
    0, 0,            // xoffset, yoffset：从左下角开始写
    width_, height_, // 覆盖整幅
    GL_RED_INTEGER,  // format  ：单通道整数像素布局
    GL_UNSIGNED_INT, // type    ：每通道 32 位无符号
    nullptr          // pixels  ：PBO 绑定时 = 偏移 0
);
```

| 参数 | 本工程值 | 说明 |
| :--- | :--- | :--- |
| `target` | `GL_TEXTURE_2D` | 与当前 `glBindTexture` 的目标一致 |
| `level` | `0` | 写入 mip level 0 |
| `xoffset`, `yoffset` | `0, 0` | 子矩形左下角 |
| `width`, `height` | `width_`, `height_` | 子矩形大小 = 整张纹理 |
| `format` | `GL_RED_INTEGER` | 见下方 §3.1；单通道**整数**像素布局 |
| `type` | `GL_UNSIGNED_INT` | 与 `GL_R32UI` / `uint` 头指针匹配 |
| `pixels` | `nullptr` | **有 PBO**：PBO 偏移 0；**无 PBO**：则是 CPU 指针 |

**为何是 Sub、不是再 `TexStorage` / `TexImage`**：  
存储已由 `glTexStorage2D` 分配；每帧只改内容（类比 `glBufferSubData`）。

`format`/`type` 必须和内部格式 `GL_R32UI` 兼容，否则数据解释错误。

### 3.1 `GL_RED_INTEGER` 是什么意思？

表示：**这次上传的像素按「单通道整数」解释**，不是归一化的颜色浮点。

| 部分 | 含义 |
| :--- | :--- |
| **`RED`** | 只用 **R 一个通道**（没有 G/B/A） |
| **`INTEGER`** | 数据是 **整数**，直接当整数用，**不会**当成 `[0,1]` 颜色去归一化 |

本工程对应关系：

- 纹理内部格式：`GL_R32UI`（每像素一个 `uint32` 头指针 index）
- `format = GL_RED_INTEGER`：告诉 `TexSubImage2D`「源是单通道整数」
- `type = GL_UNSIGNED_INT`：每通道 4 字节无符号整数

若误写成普通 `GL_RED`（不带 `INTEGER`），驱动可能按**归一化颜色**解释，`0xFFFFFFFF` 会被错误缩放/转换，链表头指针就坏了。  
凡是整数纹理（`R32UI` / `R32I` 等）的上传/下载，format 都要用 `*_INTEGER` 这一套。

---

## 4. 前几行顺序有没有要求？

| 关系 | 要求 |
| :--- | :--- |
| B1、B2 → B3 | **必须先绑 PBO 与纹理，再 `TexSubImage2D`** |
| B1 vs B2 | **谁先谁后无所谓** |
| A vs B | **无依赖**；`BindImageTexture` 只要在 Transparent **draw 前**完成即可 |

更清晰的分组：

```cpp
// A. shader 用（与清空无关）
glBindImageTexture(0, headPtrTexture_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

// B. PBO 整幅清空头指针
glBindBuffer(GL_PIXEL_UNPACK_BUFFER, clearBuf_);
glBindTexture(GL_TEXTURE_2D, headPtrTexture_);
glTexSubImage2D(..., nullptr);
glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
```

---

## 5. 速查

```text
PIXEL_UNPACK_BUFFER   像素上传源（Buffer → 纹理）
PIXEL_PACK_BUFFER     像素下载目标（纹理 → Buffer）
TexSubImage2D         改已有纹理的一块内容
PBO + nullptr         从 buffer 偏移 0 读，不走 CPU 指针
BindImageTexture      给 image 原子读写，不负责清空
```

---

## 6. 相关文件

| 文件 | 内容 |
| :--- | :--- |
| `../src/LinkedListOITApp.cpp` | `clearBuf_` 创建；每帧 486–491 清空 |
| [OpenGL_ImageUnit与BindImageTexture.md](./OpenGL_ImageUnit与BindImageTexture.md) | Image Unit / `BindImageTexture` 参数 |
| [README.md](./README.md) | 链表 OIT 全流程 |
