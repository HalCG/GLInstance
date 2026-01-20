#version 430 core

// 顶点属性输入 (适用于球体阵列网格，无需切线贴图)
layout(location = 0) in vec4 position; // 局部顶点位置
layout(location = 1) in vec4 texcoord; // 纹理坐标
layout(location = 2) in vec4 normal;   // 局部法线

// 矩阵 Uniform
uniform mat4 M;   // Model 矩阵
uniform mat4 V;   // View 矩阵
uniform mat4 P;   // Projection 矩阵
uniform mat4 ITM; // Inverse Transpose Model 矩阵

// 输出给片段着色器的插值变量
out vec4 V_Texcoord;
out vec3 V_Normal;
out vec3 V_WorldPosition;

void main()
{
    // 变换法线到世界空间
    V_Normal = normalize(mat3(ITM) * normal.xyz);
    V_Texcoord = texcoord;
    // 变换顶点位置到世界空间
    V_WorldPosition = (M * vec4(position.xyz, 1.0)).xyz;
    // 输出最终裁剪空间位置
    gl_Position = P * V * vec4(V_WorldPosition, 1.0);
}

