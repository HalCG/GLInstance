#version 430 core

// 2D 矩形/全屏 Quad 顶点属性输入
layout (location = 0) in vec2 aPos;      // 屏幕空間 NDC 位置 [-1, 1]
layout (location = 1) in vec2 aTexCoord;  // 屏幕纹理坐标 [0, 1]

out vec2 vTexCoord;                      // 输出到 Fragment Shader 的 UV 坐标

void main() {
    vTexCoord = aTexCoord;
    // 深度 Z 固定为 0.0，W 固定为 1.0，直接覆盖整个屏幕区域
    gl_Position = vec4(aPos, 0.0, 1.0);
}
