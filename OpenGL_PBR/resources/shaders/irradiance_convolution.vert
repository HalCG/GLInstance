#version 430 core

// IBL 离屏捕获立方体顶点属性
layout(location = 0) in vec4 aPos;

out vec3 WorldPos; // 世界/局部坐标位置，作为采样立方体贴图的方向向量

uniform mat4 projection; // Perspective 90 度透视投影矩阵
uniform mat4 view;       // 对应 6 个面 ( LookAt ) 的视图矩阵

void main()
{
    WorldPos = aPos.xyz;
    gl_Position = projection * view * vec4(WorldPos, 1.0);
}

