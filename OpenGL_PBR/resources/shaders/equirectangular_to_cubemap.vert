#version 430 core

// 矩形全景 HDR 转换为 3D 立方体贴图顶点着色器
layout(location = 0) in vec4 aPos;

out vec3 WorldPos; // 3D 立方体顶点位置，直接代表 3D 观察方向向量

uniform mat4 projection; // 90 度透视投影矩阵
uniform mat4 view;       // 6 个面 LookAt 视图矩阵

void main()
{
    WorldPos = aPos.xyz;
    gl_Position = projection * view * vec4(WorldPos, 1.0);
}

