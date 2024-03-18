#version 430 core

// 顶点属性输入
layout (location = 0) in vec3 aPos;       // 局部空间顶点位置
layout (location = 1) in vec3 aNormal;     // 局部空间顶点法线
layout (location = 2) in vec2 aTexCoords;  // 顶点 UV 坐标

// 传递给片元着色器的插值变量
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoords;

// MVP 矩阵
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vWorldPos = vec3(model * vec4(aPos, 1.0));
    // 法线矩阵防非等比例缩放变形
    vNormal = mat3(transpose(inverse(model))) * aNormal;
    vTexCoords = aTexCoords;
    gl_Position = projection * view * vec4(vWorldPos, 1.0);
}
