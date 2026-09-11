#version 430 core
layout (location = 0) out vec4 FragColor;

in vec2 vTexCoord;

uniform sampler2D uCurrentColor;
uniform sampler2D uCurrentDepth;
uniform sampler2D uHistoryColor;

uniform mat4 uInvViewProj;
uniform mat4 uPrevViewProj;
uniform vec2 uTexelSize;
uniform float uBlendFactor;
uniform bool uHasHistory;

// 【历史帧重投影 (Reprojection)】：
// 1. 根据当前帧 UV 和 Depth 值重构当前像素在世界空间中的 3D 坐标。
// 2. 使用上一帧的 ViewProjection 矩阵 (uPrevViewProj) 将该世界坐标投影到上一帧的 NDC 屏幕空间。
// 3. 映射到上一帧的 UV [0, 1] 坐标，从上一帧的历史混合纹理 (uHistoryColor) 中采样像素颜色。
// 4. 检查 prevUv 是否超出 [0, 1] 视口边界（越界说明当前像素在上一帧不在视锥体内，历史数据无效）。
vec3 reprojectHistory(vec2 uv, out bool valid) {
    float depth = texture(uCurrentDepth, uv).r;
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * ndc;
    world /= world.w;
    vec4 prevNdc = uPrevViewProj * world;
    prevNdc /= prevNdc.w;
    vec2 prevUv = prevNdc.xy * 0.5 + 0.5;
    valid = prevUv.x >= 0.0 && prevUv.x <= 1.0 && prevUv.y >= 0.0 && prevUv.y <= 1.0;
    return texture(uHistoryColor, prevUv).rgb;
}

// 【邻域 AABB 颜色裁剪 (Neighborhood Color Clamping / Bounding Box Clipping)】：
// 消除鬼影（Ghosting / 拖尾）的核心算法！
// 当相机移动或物体被遮挡解遮挡（Occlusion/Disocclusion）时，重投影采样到的上一帧历史颜色可能是被遮挡物体的旧颜色，
// 直接混合会导致严重的“鬼影/拖尾”。
// 算法：遍历当前像素周围 3x3 邻域，计算出当前帧颜色的 AABB 最小/最大包围盒 [minC, maxC]，
// 将上一帧的历史颜色强行 Clamp 限制在这个包围盒内。如果历史颜色偏差过大（说明发生遮挡改变），就会被裁剪拉回到当前帧范围，从而瞬间消除鬼影！
vec3 clipHistory(vec3 history, vec3 current) {
    vec3 minC = current;
    vec3 maxC = current;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 uv = vTexCoord + vec2(float(x), float(y)) * uTexelSize;
            vec3 c = texture(uCurrentColor, uv).rgb;
            minC = min(minC, c);
            maxC = max(maxC, c);
        }
    }
    return clamp(history, minC, maxC);
}

void main() {
    vec3 current = texture(uCurrentColor, vTexCoord).rgb;
    // 如果无历史数据（如第 1 帧、切模式或 resize），直接输出当前帧
    if (!uHasHistory) {
        FragColor = vec4(current, 1.0);
        return;
    }

    // 重投影获取上一帧对应位置的历史颜色
    bool valid;
    vec3 history = reprojectHistory(vTexCoord, valid);
    if (!valid) {
        FragColor = vec4(current, 1.0);
        return;
    }

    // 执行 3x3 AABB 颜色裁剪防鬼影
    history = clipHistory(history, current);

    // 时间维度指数平滑混合：result = history * (1 - blend) + current * blend (默认 blend = 0.1)
    vec3 result = mix(history, current, uBlendFactor);
    FragColor = vec4(result, 1.0);
}

