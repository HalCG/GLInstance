#version 430 core
/**
 * @file composite.frag
 * @brief OIT 合成与 Over 运算 Fragment Shader
 *
 * 核心逻辑：
 * 1. 链表提取：读取 headPointers 获取像素 (x, y) 处链表头节点，沿着 next 指针遍历，将 Node 拷贝到局部数组 frags[]。
 * 2. 重复片段过滤：对共面三角形的共享边沿片段应用 Depth Epsilon 去重，消除由于面片自重叠导致的过度混合瑕疵。
 * 3. 局部插入排序 (Insertion Sort)：将片段按深度从远到近 (Depth 从大到小) 排序。
 * 4. Back-to-Front Over 混合：从不透明物体底色 (texture_opaque) 开始，依次做 over 运算混合半透明片段，输出最终 Color。
 */

#define MAX_FRAGMENTS 75

layout (location = 0) out vec4 FragColor;

in vec2 textureCoord;

uniform sampler2D texture_opaque; // 来自 Pass 1 的不透明物体颜色图

// 链表节点内存布局
struct NodeType {
  vec4 color;  // Fragment 颜色 (RGBA)
  float depth; // Fragment 屏幕空间深度 (gl_FragCoord.z)
  uint next;   // 前一个节点的 Index
};

layout(binding = 0, r32ui) uniform uimage2D headPointers;

layout(binding = 0, offset = 0) uniform atomic_uint nextNodeCounter;

layout(binding = 0, std430) buffer linkedLists {
  NodeType nodes[];
};

uniform uint MaxNodes;

const float EPSILON = 0.0001;

void main() {
  // 定义 Shader 局部寄存器数组，用于存储当前像素收到的所有半透明片段
  NodeType frags[MAX_FRAGMENTS];
  
  int count = 0;
  // 获取当前像素 (gl_FragCoord.xy) 处头指针指向的第一个 Node Index
  uint idx = imageLoad(headPointers, ivec2(gl_FragCoord.xy)).r;
  
  // 1. 遍历当前像素的单向链表 (直到 NULL 即 0xFFFFFFFF，或达到上限 MAX_FRAGMENTS)
  while (idx != 0xffffffff && count < MAX_FRAGMENTS) {
    NodeType node = nodes[idx];
    bool isDuplicate = false;
    
    // 检查是否有与已压入数组片段深度极度相近的重复片段
    // (解决三角形共面边界处的 Double-Blended Artifact)
    for (int i = 0; i < count; i++) {
        if (abs(frags[i].depth - node.depth) < EPSILON) {
            isDuplicate = true;
            break;
        }
    }
    
    if (!isDuplicate) {
        frags[count] = node;
        count++;
    }
    idx = node.next; // 沿着 next 指针移动到前一个节点
  }

  // 2. 插入排序 (Insertion Sort)
  // 将 frags 数组按照 depth 从大到小 (从远到近) 降序排列
  for (int i = 1; i < count; i++) {
    int j = i;
    NodeType toInsertNode = frags[i];
    while (j > 0 && toInsertNode.depth > frags[j - 1].depth) {
      frags[j] = frags[j - 1];
      j--;
    }
    frags[j] = toInsertNode;  
  }

  // 3. Back-to-Front (从远到近) Over 混合运算
  // 初始颜色为 Pass 1 不透明物体的渲染结果
  vec4 color = texture(texture_opaque, textureCoord);
  for (int i = 0; i < count; i++) {
    // 经典的 Standard Alpha Over Operator:
    // C_out = C_src * alpha_src + C_dst * (1 - alpha_src)
    color.rgb = color.rgb * (1.0 - frags[i].color.a) + frags[i].color.rgb * frags[i].color.a;
    color.a = color.a + frags[i].color.a * (1.0 - color.a);
  }

  FragColor = color;
}