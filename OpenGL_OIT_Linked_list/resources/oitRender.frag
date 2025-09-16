#version 430 core
/**
 * @file oitRender.frag
 * @brief OIT 链表构建 Fragment Shader
 *
 * 核心逻辑：
 * 1. 深度裁切：与 Pass 1 生成的不透明深度图 (texture_depth) 比较，若当前片段在不透明物体之后则 discard。
 * 2. 原子计数自增：利用 atomicCounterIncrement(nextNodeCounter) 获得唯一的线性 SSBO 节点索引。
 * 3. 头插法无锁插入链表：利用 imageAtomicExchange(headPointers, screenCoord, newIndex)
 *    将旧的头指针值读出作为新节点的 next 指针，并将 headPointers 指向 newIndex。
 */

#define MAX_FRAGMENTS 75

layout (location = 0) out vec4 FragColor;
in vec3 vertexPos;
in vec3 vertexNor;
in vec2 textureCoord;

uniform vec3 cameraPos;
uniform vec3 lightPos;
uniform vec3 k;
uniform uint MaxNodes;

uniform sampler2D texture_diffuse;
uniform sampler2D texture_depth; // 来自 Pass 1 的不透明物体深度图

// 链表节点内存布局 (std430 保证 C++ 与 GLSL 布局精确匹配)
struct NodeType {
  vec4 color;  // Fragment 颜色 (RGBA)
  float depth; // Fragment 屏幕空间深度 (gl_FragCoord.z)
  uint next;   // 链表中前一个节点的索引 Index (0xFFFFFFFF 表示链表尾 NULL)
};

// Image Unit 0: 单通道 32 位无符号整数 Head Pointer Texture (每像素保存链表头 Node Index)
layout(binding = 0, r32ui) uniform uimage2D headPointers;

// Atomic Counter Unit 0: 用于给新 Fragment 分配全局唯一的线性 SSBO Node Index
layout(binding = 0, offset = 0) uniform atomic_uint nextNodeCounter;

// SSBO Binding 0: 大容量节点存储数组
layout(binding = 0, std430) buffer linkedLists {
  NodeType nodes[];
};

void main() {
  // 1. 深度裁切逻辑 (手动 Early Z)
  // 计算当前 Fragment 在屏幕 0.0 ~ 1.0 的 UV 坐标
  vec2 uv = gl_FragCoord.xy / vec2(800.0, 600.0); // 屏幕宽高
  float opaqueDepth = texture(texture_depth, uv).r;

  // 如果当前透明片段的深度 gl_FragCoord.z 大于不透明物体的深度，说明被不透明物体挡住，直接丢弃
  if (gl_FragCoord.z > opaqueDepth + 0.0001) { // 加上 epsilon 避免浮点精度竞争
      discard;
  }

  // 2. 硬件原子自增获取当前可用节点 Index
  uint index = atomicCounterIncrement(nextNodeCounter);

  // 3. 防缓冲区溢出保护
  if (index < MaxNodes) {
    // 4. 头插法无锁原子交换 (Atomic Head Pointer Exchange)
    // imageAtomicExchange 原子地将 index 写入 headPointers[screenCoord]，并返回写入前的旧值 (preHead)
    uint preHead = imageAtomicExchange(headPointers, ivec2(gl_FragCoord.xy), index);

    // 5. 写入 SSBO 节点数据
    vec4 color = texture(texture_diffuse, textureCoord);
    nodes[index].color = color;
    nodes[index].depth = gl_FragCoord.z;
    nodes[index].next = preHead; // 将 next 指向旧头指针，建立链表链接
  }

  FragColor = texture(texture_diffuse, textureCoord);
}

/* 
=================================================================================
【GLSL 标准内置原子函数详解 (OpenGL 4.2+ Built-in Atomic Functions)】
=================================================================================

1. atomicCounterIncrement(atomic_uint counter)
   - 来源：GLSL 4.20+ 原生内置函数 (不需要用户/开发者手动声明或定义)。
   - 原型：uint atomicCounterIncrement(atomic_uint counter);
   - 作用：由 GPU 显存控制器的硬件原子电路提供互斥保障。多核并发时，安全地对全局原子计数器 `counter` 执行 +1，
           并返回自增前的整型旧值 (用于作为 SSBO 节点数组 `nodes[]` 的全局唯一分配索引 `index`)。

2. imageAtomicExchange(gimage2D image, ivec2 P, uint data)
   - 来源：GLSL 4.20+ / ARB_shader_image_load_store 原生内置函数。
   - 原型：uint imageAtomicExchange(gimage2D image, ivec2 P, uint data);
   - 作用：GPU 显存 Cache 硬件级无锁原子交换 (Atomic Exchange) 指令。在不可分割的单个原子周期内一步完成：
           ① 将新索引 `data` (即 `index`) 原子写入 `image` 的 `P` 像素坐标处；
           ② 同时瞬间读出该像素原先存储的旧头指针并作为返回值 `preHead` 返回。
           依靠返回值 `preHead` 可以无锁完成单链表“头插法”链接 (`nodes[index].next = preHead`).
=================================================================================
*/