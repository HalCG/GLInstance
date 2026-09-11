#version 430 core

// 调试线段顶点位置输入
layout (location = 0) in vec3 aPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    // 线段顶点的 MVP 变换
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
