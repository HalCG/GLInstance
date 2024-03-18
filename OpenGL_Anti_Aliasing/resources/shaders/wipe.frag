#version 430 core
layout (location = 0) out vec4 FragColor;

in vec2 vTexCoord;

uniform sampler2D uLeft;   // None 基线
uniform sampler2D uRight;  // 当前 AA
uniform float uWipe;       // [0,1] 分割线
uniform vec2 uTexelSize;

void main() {
    vec3 left = texture(uLeft, vTexCoord).rgb;
    vec3 right = texture(uRight, vTexCoord).rgb;
    vec3 color = (vTexCoord.x < uWipe) ? left : right;

    // 分割线高亮，方便对齐同一几何边缘对比
    float d = abs(vTexCoord.x - uWipe);
    if (d < max(uTexelSize.x * 1.5, 0.0015)) {
        color = mix(color, vec3(1.0, 0.85, 0.15), 0.85);
    }

    FragColor = vec4(color, 1.0);
}
