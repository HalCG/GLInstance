#version 430 core
// 全屏通用顶点着色器（Pass-through Vertex Shader）
// 作用：为后处理（Blit / FXAA / TAA）搭建全屏 2D “空白画布框架”。
// 1. 接收全屏 4 个顶点的 NDC 归一化坐标 aPos (-1 ~ 1)，无需任何 MVP 矩阵变换，直接赋给 gl_Position。
// 2. 将顶点 UV 坐标 aTexCoord (0 ~ 1) 输出给 vTexCoord，经过 GPU 光栅化单元平滑线性插值后给 Fragment Shader 使用。

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
