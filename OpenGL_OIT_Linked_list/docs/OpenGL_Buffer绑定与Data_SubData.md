# OpenGL Buffer 绑定与数据更新：`BindBuffer` / `BindBufferBase` / `BufferData` / `SubData`

> 本文从 `OpenGL_OIT_Linked_list` 里原子计数器与 SSBO 的写法出发，把 Buffer 相关的两套常见困惑一次讲清：  
> 1）**改内容**还是**改分配**；2）**通用绑定**还是**索引绑定点**（给 Shader 用）。  
> 文中代码以本仓库链表 OIT 为准，并补充 VBO / UBO 对照，保证单独阅读也能闭环。

---

## 1. 先分清两件事

| 问题 | 对应 API | 一句话 |
| :--- | :--- | :--- |
| 显存要多大？要不要重分配？ | `glBufferData` / `glBufferSubData` | Data 管**分配（可顺带填）**；SubData 只在**已有存储上改内容** |
| Shader 怎么找到这块 Buffer？ | `glBindBuffer` / `glBindBufferBase` | BindBuffer = 给 CPU 命令用的**当前槽**；BindBufferBase = 挂到 Shader 的 **binding = N** |

两套 API 正交：你可以先 `BindBuffer` 再 `BufferData`，也可以 `BindBufferBase` 后再 `SubData`；  
但 **UBO / SSBO / Atomic Counter 要给 Shader 用，最终必须落到某个 indexed binding point 上**。

---

## 2. `glBufferData` vs `glBufferSubData`

### 2.1 各自干什么

```cpp
// 分配（或重新分配）size 字节；data 非空则同时上传初值
glBufferData(target, size, data /* 可为 nullptr */, usage);

// 不改变分配大小，只改 [offset, offset+size) 这段内容
glBufferSubData(target, offset, size, data);
```

| | `glBufferData` | `glBufferSubData` |
| :--- | :--- | :--- |
| 存储 | **创建 / 可能丢弃旧存储再分配** | **复用已有存储** |
| 典型用途 | 首次创建；改容量（如 resize） | 每帧清零、更新一小段 |
| `data == nullptr` | 只占坑，内容未定义（或由驱动延迟） | 非法（必须提供指针） |

### 2.2 链表 OIT 里为什么拆成两步

创建原子计数器时（`LinkedListOITApp::initOITBuffers`）：

```cpp
GLuint zero = 0;

glGenBuffers(1, &atomicBuffer_);
glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);

// ① 只分配 4 字节，不在这里塞初值
glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);

// ② 写入 0：只改内容，不重新分配
glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);
```

初始化其实可以合并成：

```cpp
glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), &zero, GL_DYNAMIC_DRAW);
```

拆开的原因是：**和后续每帧逻辑一致**——透明 Pass 前只清零计数器，绝不重分配：

```cpp
// 每帧 Transparent Pass 之前
glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);
glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero); // 只要把计数器打回 0
```

若每帧都用 `glBufferData(..., &zero, ...)`，等于反复**重新分配** 4 字节 buffer，多余，且可能带来不必要的同步/抖动。  
`GL_DYNAMIC_DRAW` 也暗示：这块会频繁被 CPU 更新内容。

### 2.3 SSBO 为什么常常只要 `BufferData(nullptr)`

```cpp
glGenBuffers(1, &linkedListBuffer_);
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linkedListBuffer_);
glBufferData(GL_SHADER_STORAGE_BUFFER, maxNodes_ * nodeSize, nullptr, GL_DYNAMIC_DRAW);
```

节点内容由 Fragment Shader 写入，CPU 不必预填；只要保证容量够（本工程约 `width * height * 20` 个节点）。  
窗口 resize 时会走 `destroyOITBuffers` → `initOITBuffers`，那时才用新的 `BufferData` **改容量**。

### 2.4 选用口诀

```text
要改「多大」     → glBufferData
只要改「里面是啥」→ glBufferSubData
每帧清零计数器   → 一定用 SubData，不要 Data 重分配
```

---

## 3. 通用绑定 vs 索引绑定点

OpenGL 里同一个 target（例如 `GL_ATOMIC_COUNTER_BUFFER`）上有两套「挂钩」：

### 3.1 通用绑定（`glBindBuffer`）

```cpp
glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicBuffer_);
```

含义：**「当前这个 target 上，CPU 接下来操作的是哪块 buffer」**。  
后续 `glBufferData` / `glBufferSubData` /（对 `GL_ARRAY_BUFFER`）`glVertexAttribPointer` 都认这个「当前对象」。

它**不是** Shader 里 `layout(binding = 0)` 的那个 `0`。  
也没有规范保证：「`glBindBuffer` 了就自动等于 binding 0」。

### 3.2 索引绑定点（`glBindBufferBase`）

```cpp
glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, /* index */ 0, atomicBuffer_);
```

含义：把 buffer 挂到该 target 的 **binding point `index`**。  
Shader 侧：

```glsl
layout(binding = 0, offset = 0) uniform atomic_uint nextNodeCounter;
layout(binding = 0, std430) buffer LinkedList { ... };
```

这里的 `binding = 0` 必须和 `glBindBufferBase(..., 0, ...)` 对齐。

`glBindBufferRange` 类似，但可以指定 offset/size（子范围）；本工程用整块 Base 即可。

#### 3.2.1 `atomic_uint` 的 `offset` 是什么？

```glsl
layout(binding = 0, offset = 0) uniform atomic_uint nextNodeCounter;
```

| 限定符 | 含义 |
| :--- | :--- |
| `binding = 0` | 用哪个 Atomic Counter **绑定点**（对上 `glBindBufferBase(..., 0, atomicBuffer_)`） |
| `offset = 0` | 在这块 buffer 里，该计数器从第几个**字节**开始（本工程只有一个 `GLuint`，故为 0） |

一块 Atomic Counter Buffer 里可以并排放多个计数器：

```text
buffer:  [ counter0 | counter1 | counter2 | ... ]
字节:      0..3       4..7       8..11
```

```glsl
layout(binding = 0, offset = 0) uniform atomic_uint a; // 第 0 个 uint
layout(binding = 0, offset = 4) uniform atomic_uint b; // 第 1 个 uint（偏移 4 字节）
```

`offset` 一般按 **4 字节对齐**（一个 `atomic_uint` 的大小）。  
本工程 `atomicBuffer_` 只存一个计数器，所以始终 `offset = 0`。

> `oitRender.frag` 真正用 `atomicCounterIncrement`；`composite.frag` 里若也声明了同一 layout，合成 Pass 通常不再递增，多半是布局一致/遗留，不影响 `offset` 含义。

### 3.3 示意图

```text
                    glBindBuffer(target, buf)
                              │
                              ▼
              ┌─────────────────────────────┐
              │  通用「当前 buffer」槽        │  ← 给 glBufferData / SubData 等用
              └─────────────────────────────┘

                    glBindBufferBase(target, N, buf)
                              │
                              ▼
              ┌── binding0 ── binding1 ── ... ──┐
              │   ←── Shader layout(binding=N)  │
              └─────────────────────────────────┘
```

两个都是 `binding = 0` 也不冲突——只要 **target 不同**（例如一个是 Atomic Counter，一个是 SSBO）。本工程正是如此。

---

## 4. `glBindBuffer` 只用、不用 `Base` 会怎样？

### 4.1 只做分配 / 清零

```cpp
glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicBuffer_);
glBufferData(...);
glBufferSubData(..., &zero);
```

**可以**，CPU 侧更新没问题。

### 4.2 还要让 Shader 读写

若从不调用：

```cpp
glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);
```

则 `layout(binding = 0) uniform atomic_uint ...` 对上的槽位可能是空的或旧的：  
`atomicCounterIncrement` 行为未定义 / 无效 → 节点索引乱掉，链表 OIT 直接坏。

### 4.3 推荐稳妥顺序

```cpp
// 1) 通用绑定：方便改数据
glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicBuffer_);
glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

// 2) 索引绑定：挂给 Shader binding = 0
glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);
```

本工程常 **直接 `BindBufferBase` 再 `Data/SubData`**：多数驱动上 Base 也会把该对象设成当前 target，因而能跑；  
语义上仍应记住：**给 Shader 看的是 Base，不是普通 BindBuffer。**

---

## 5. GLSL：`location` 和 `binding` 不是一回事

这是另一个高频混淆点。

### 5.0 绑定号 N：Shader 定或 CPU 定（两种等价）

对 UBO / SSBO / Atomic（以及 Image，见同目录 Image 专题）都成立：

> **绑定号 N 可以不在 Shader 里写死，改由 CPU 指定后再绑；也可以在 Shader 里 `layout(binding = N)` 写死，再在 CPU 侧用同一个 N 去绑。**  
> 两边最终必须约定同一个 N；不是「可以不绑」，只是「谁来定号」。

```text
路径 A：Shader 不写 binding  →  CPU 定 N（BlockBinding / Uniform1i 等）→ BindBufferBase / BindImageTexture(N, ...)
路径 B：Shader 写 binding=N  →  CPU 同步用同一个 N                    → BindBufferBase / BindImageTexture(N, ...)
```

本工程 Atomic / SSBO / 头指针 Image 都用 **路径 B**（`binding = 0`），CPU 直接 `BindBufferBase(..., 0, ...)` / `BindImageTexture(0, ...)`。

| 写法 | 典型用途 | CPU 怎么接 |
| :--- | :--- | :--- |
| `layout(location = N) in vec3 aPos;` | 顶点属性 | `glBindBuffer(GL_ARRAY_BUFFER, vbo)` + `glVertexAttribPointer(N, ...)` |
| `layout(location = N) out vec4 FragColor;` | 片元输出 | FBO 的 color attachment / `glDrawBuffers` |
| `layout(binding = N) uniform sampler2D uTex;` | 纹理单元 | `glActiveTexture(GL_TEXTURE0 + N)` + `glBindTexture` |
| `layout(binding = N) uniform atomic_uint c;` | 原子计数器 | **`glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, N, ...)`** |
| `layout(binding = N, std430) buffer B { ... };` | SSBO | **`glBindBufferBase(GL_SHADER_STORAGE_BUFFER, N, ...)`** |
| `layout(binding = N, std140) uniform U { ... };` | UBO | **`glBindBufferBase(GL_UNIFORM_BUFFER, N, ...)`** |

### 5.1 只有 `location`、没有 `binding` 时——能不能只用 `glBindBuffer`？

**要看是哪一类 `location`。**

#### 情况 A：顶点属性（可以，而且应该用 `glBindBuffer`）

```glsl
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
```

```cpp
glBindVertexArray(vao);
glBindBuffer(GL_ARRAY_BUFFER, vbo);          // 通用槽即可
glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
glVertexAttribPointer(0, 3, GL_FLOAT, ...);  // location=0 怎么从当前 VBO 取数
glEnableVertexAttribArray(0);
// 这里不需要 BindBufferBase，也没有 layout(binding)
```

##### 为什么顶点不用「索引绑定点」？（很重要）

顶点走的是另一套管线：**VAO + 属性 `location` + 通用 `GL_ARRAY_BUFFER`**，不是 UBO/SSBO/Atomic/Image 那套 `layout(binding = N)`。

| | 顶点属性 | UBO / SSBO / Atomic / Image |
| :--- | :--- | :--- |
| Shader 编号 | **`location`** | **`binding`** |
| CPU 怎么接 | `glBindBuffer` + **`glVertexAttribPointer(location, …)`** | **`glBindBufferBase` / `glBindImageTexture`** |
| 只要 `glBindBuffer`？ | **够用**（再配 AttribPointer） | 只够改数据，不够给 Shader 当资源 |

原因简要三条：

1. **管线阶段不同**：顶点拉取在顶点装配阶段，固定从「当前 `GL_ARRAY_BUFFER` + 属性指针」取数；binding 点是后来的可编程资源模型。  
2. **一块 VBO 常喂多个 location**：交错顶点里位置/UV 同 buffer，靠 AttribPointer 的 offset/stride 区分，不必每个属性一个 `BindBufferBase`。  
3. **VAO 记住「属性 ↔ buffer」**：绑好后 draw 时不必再对每个属性做 indexed bind；SSBO/Atomic 则是 draw 时按 binding 查资源。

```text
顶点：  BindBuffer(ARRAY) → BufferData → VertexAttribPointer(location)
        ↑ 完整路径，没有 binding 点

资源：  layout(binding=N) ↔ BindBufferBase / BindImageTexture(N)
        ↑ 这才是索引绑定点那套
```

所以不是忘了做绑定点，而是**顶点属性本来就不走 indexed binding**。

#### 情况 B：UBO / SSBO 没写 `layout(binding = …)`（仍要 `BindBufferBase`）

Shader 可以不写死 binding，改由 CPU 指定：

```glsl
// 注意：没有 layout(binding = N)
layout(std140) uniform FrameData {
    mat4 viewProj;
    vec3 lightPos;
};
```

```cpp
GLuint blockIndex = glGetUniformBlockIndex(program, "FrameData");
const GLuint bindingPoint = 2;
glUniformBlockBinding(program, blockIndex, bindingPoint); // 告诉程序：这个 block 用 2 号点

glBindBuffer(GL_UNIFORM_BUFFER, ubo);
glBufferData(GL_UNIFORM_BUFFER, sizeof(data), &data, GL_DYNAMIC_DRAW);

// 仍然必须挂到索引点 2——不能省略 Base
glBindBufferBase(GL_UNIFORM_BUFFER, bindingPoint, ubo);
```

SSBO 同理，用 `glShaderStorageBlockBinding`。这就是上面的 **路径 A**。

若 Shader 写成 `layout(binding = 2, std140) uniform FrameData { ... }`，则跳过 `glUniformBlockBinding`，CPU 直接 `glBindBufferBase(GL_UNIFORM_BUFFER, 2, ubo)`——即 **路径 B**。

**结论**：  
- 顶点 `location` → **`BindBuffer` + `BufferData` + `VertexAttribPointer`**，不需要 `BindBufferBase`；  
- UBO/SSBO/Atomic → **最终都要挂到某个 binding 点**（`BindBufferBase`）；N 可在 GLSL 写死（路径 B），或只在 CPU 指定（路径 A）。

---

## 6. 本工程完整对照（链表 OIT）

### 6.1 CPU：创建与绑定

```cpp
// Atomic Counter → shader: layout(binding = 0) uniform atomic_uint nextNodeCounter;
glGenBuffers(1, &atomicBuffer_);
glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);
glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

// SSBO → shader: layout(binding = 0, std430) buffer ...
glGenBuffers(1, &linkedListBuffer_);
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linkedListBuffer_);
glBufferData(GL_SHADER_STORAGE_BUFFER, maxNodes_ * nodeSize, nullptr, GL_DYNAMIC_DRAW);
```

两者都是 `binding = 0`，但 target 分别是 `ATOMIC_COUNTER` 与 `SHADER_STORAGE`，互不抢槽。

### 6.2 每帧透明 Pass 前

```cpp
glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, atomicBuffer_);
glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero); // 只清内容

glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, linkedListBuffer_);
// head image 另用 glBindImageTexture / PBO clear，此处不展开
```

### 6.3 Shader：用 binding 取节点

```glsl
layout(binding = 0, offset = 0) uniform atomic_uint nextNodeCounter;
layout(binding = 0, std430) buffer linkedLists {
  NodeType nodes[];   // 末尾不定长数组，见 §6.4
};

uint index = atomicCounterIncrement(nextNodeCounter); // 原子取号并 +1
// 写入 nodes[index]，再用 imageAtomicExchange 挂到像素链表头
```

### 6.4 SSBO 里的 `nodes[]`：末尾不定长数组

```glsl
layout(binding = 0, std430) buffer linkedLists {
  NodeType nodes[];
};
```

这是 GLSL SSBO 的标准写法，**不是** C 语言那种编译期定长数组。

| 部分 | 含义 |
| :--- | :--- |
| `buffer linkedLists { ... }` | 声明一块可读写的 Shader Storage Buffer |
| `binding = 0` | 对上 `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, …)` |
| `std430` | 内存布局（比 UBO 的 `std140` 紧凑，便于与 C++ `ListNode` 对齐） |
| `NodeType nodes[]` | **最后一个成员**可不写长度 =「整块 buffer 都当这个数组用」 |

长度由 CPU 分配决定：

```cpp
glBufferData(GL_SHADER_STORAGE_BUFFER, maxNodes_ * nodeSize, nullptr, ...);
// GPU：nodes[0] … nodes[maxNodes_-1]
// 元素个数 ≈ buffer 字节数 / sizeof(NodeType)
```

和 C 的对比：

```c
NodeType nodes[100];   // C：编译期定长度
NodeType nodes[];      // GLSL SSBO 末尾：运行时由 buffer 大小定长度
```

规则要点：

1. **只有 buffer block 的最后一个成员**才能写成 `T name[]`  
2. 若前面还有字段，不定长数组必须放最后  
3. 本工程 block 里只有 `nodes[]`，故整块 SSBO = 节点大数组  

```text
CPU:  linkedListBuffer_  大小 ≈ maxNodes_ * 24 字节
GPU:  nodes[0] … nodes[maxNodes_-1]
Pass2: atomic 取 index → 写 nodes[index]
Pass3: 从头指针顺着 next 读 nodes[…]
```

一句话：`nodes[]` = 「这块 SSBO 是一个长度由显存分配决定的 `NodeType` 数组」。

---

## 7. 常见踩坑

| 现象 | 常见原因 |
| :--- | :--- |
| 第二帧开始透明乱掉 / 节点爆表 | 漏了每帧 `SubData` 把 atomic 清 0 |
| Shader 里 atomic/SSBO 像没数据 | 只 `glBindBuffer`，没 `glBindBufferBase` 到对应 binding |
| resize 后崩溃或花屏 | 改了分辨率却没按新 `maxNodes_` 重新 `BufferData` 分配 SSBO |
| 以为 `location` 能代替 `binding` | 顶点属性和 UBO/SSBO 是两套管线 |
| 两个 `binding = 0` 冲突？ | 看 target：Atomic / SSBO / Image 是不同绑定域 |

---

## 8. 速查表

```text
创建 / 改容量          glBufferData
只改内容（每帧清零）    glBufferSubData

VBO 顶点数据            glBindBuffer(ARRAY) + BufferData + VertexAttribPointer(location)
                        ↑ 不走 binding 点，不要 BindBufferBase
UBO / SSBO / Atomic     glBindBufferBase(target, bindingIndex, ...)

layout(location = N)    顶点 in / 片元 out（属性槽，≠ binding）
layout(binding = N)     纹理单元、image、UBO、SSBO、atomic
```

---

## 9. 相关代码位置

| 文件 | 内容 |
| :--- | :--- |
| `../src/LinkedListOITApp.cpp` | `initOITBuffers`、每帧 `glBufferSubData` 清零 |
| `../resources/oitRender.frag` | `atomicCounterIncrement`、SSBO 头插法 |
| [README.md](./README.md) | 链表 OIT 全流程 |
| [OpenGL_ImageUnit与BindImageTexture.md](./OpenGL_ImageUnit与BindImageTexture.md) | 头指针纹理的 Image Unit / `BindImageTexture` |

---

## 10. 一句话收束

- **`SubData`**：只改内容、不改分配 → 适合「每帧把计数器打回 0」。  
- **`BindBuffer`**：CPU 当前操作哪个 buffer。  
- **`BindBufferBase`**：挂到索引绑定点 N，供 Shader 读写。  
- **绑定号 N**：可 Shader 写死再 CPU 对齐，或只由 CPU 指定再绑；两边约定同一 N。  
- **`location` ≠ `binding`**：顶点用 location + 通用 `ARRAY_BUFFER`；资源用 binding + `BindBufferBase`/`BindImageTexture`。见 §5.1 情况 A。  
- Image 同理，见 [OpenGL_ImageUnit与BindImageTexture.md](./OpenGL_ImageUnit与BindImageTexture.md)。
