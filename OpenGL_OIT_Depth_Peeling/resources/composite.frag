#version 430 core
#define MAX_FRAGMENTS 75

layout (location = 0) out vec4 FragColor;

in vec2 textureCoord;

uniform sampler2D texture_opaque; // 不透明纹理

struct NodeType{
  vec4 color; // float * 4
  float depth; // float * 1
  uint next; // uint * 1
};

layout(binding = 0, r32ui) uniform uimage2D headPointers; // head_ptr_buffer
layout(binding = 0, offset = 0) uniform atomic_uint nextNodeCounter; // atomic_buffer
layout( binding = 0, std430 ) buffer linkedLists {
  NodeType nodes[];
};// linked_list_buffer

uniform uint MaxNodes;

const float EPSILON = 0.0001;

void main(){
  
  // 定义一个数组 用于将链表中的数据复制到该数组中
  NodeType frags[MAX_FRAGMENTS];
  
  // 计数当前像素 (x,y) 处的 list 有多少个 node
  int count = 0;
  // 获取 当前像素 (x,y) 处的 headpointer
  uint idx = imageLoad(headPointers, ivec2(gl_FragCoord.xy)).r;
  
  // 将 当前 headpointer 对应的 list 复制到 一个 数组中
  // 将该片段对应的链表复制到数组中
  while(idx!=0xffffffff && count < MAX_FRAGMENTS) {
    NodeType node = nodes[idx];
    bool isDuplicate = false;
    for (int i = 0; i < count; i++) {
        // 检查 nodes[idx] 是否和已有的片段接近
        // 如果接近，说明是同一个面片中，两个相邻三角形的边界 片段， 那么只保留一个 片段 即可
        if (abs(frags[i].depth - node.depth) < EPSILON) {
            isDuplicate = true;
            break;
        }
    }
    if (!isDuplicate) {
        frags[count] = node;
        count++;
    }
    idx = node.next;
  }

  // 排序 (使用插入排序)
  // 排序完成后: depth从大到小排序(从远到近排序)
  for(int i=1; i<count; i++){
    int j=i;
    NodeType toInsertNode = frags[i];
    while(j>0 && toInsertNode.depth > frags[j-1].depth){
      frags[j] = frags[j-1];
      j --;
    }
    frags[j] = toInsertNode;  
  }

  // 由后到前（远到近）
  // 从远到近使用 over 运算混合颜色
  vec4 color = texture(texture_opaque, textureCoord);
  for(int i=0; i<count; i++){
    color.rgb = color.rgb * (1.0 - frags[i].color.a) + frags[i].color.rgb * frags[i].color.a;
    color.a = color.a + frags[i].color.a * (1.0 - color.a);
  }
  FragColor = color;
}