#version 430 core

// 调试线段片元着色器输出
layout (location = 0) out vec4 FragColor;

// Uniform 线段颜色
uniform vec3 uLineColor;

void main() {
    // 纯色输出线段
    FragColor = vec4(uLineColor, 1.0);
}
