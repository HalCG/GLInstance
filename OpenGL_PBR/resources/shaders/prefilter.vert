#version 430 core

// Prefilter 立方体贴图渲染顶点着色器
layout(location = 0) in vec4 aPos;

out vec3 WorldPos; // 世界坐标系方向向量

uniform mat4 projection; // 90 度 Perspective 矩阵
uniform mat4 view;       // 对应立方体 6 个方向 LookAt 视图矩阵

void main()
{
    WorldPos = aPos.xyz;
    gl_Position = projection * view * vec4(WorldPos, 1.0);
}

