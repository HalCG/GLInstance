#version 430 core

// 调试视图最终输出颜色
layout (location = 0) out vec4 FragColor;

in vec2 vTexCoord; // 全屏 Quad 屏幕 UV [0, 1]

// G-Buffer 各附件输入采样器
uniform sampler2D uGAlbedo;     // Albedo 颜色附件
uniform sampler2D uGNormal;     // Normal 法线附件
uniform sampler2D uGMaterial;   // Material 材质附件

// 调试模式选择变量 (按键盘 'G' 触发切换)
// 1: 仅查看 Albedo 漫反射纹理
// 2: 查看世界空间法线 (把 Vector [-1, 1] 映射至 RGB 颜色空间 [0, 1])
// 3: 查看材质参数附件 (Ka, Kd, Ks)
uniform int uDebugMode;

void main() {
    if (uDebugMode == 1) {
        // Mode 1: 输出原图颜色
        FragColor = vec4(texture(uGAlbedo, vTexCoord).rgb, 1.0);
    } else if (uDebugMode == 2) {
        // Mode 2: 法线可视化 (normal * 0.5 + 0.5 将 [-1, 1] 映射至 [0, 1] 像素颜色)
        FragColor = vec4(normalize(texture(uGNormal, vTexCoord).rgb) * 0.5 + 0.5, 1.0);
    } else {
        // Mode 3: 材质属性可视化
        FragColor = vec4(texture(uGMaterial, vTexCoord).rgb, 1.0);
    }
}
