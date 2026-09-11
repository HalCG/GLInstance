#version 430 core
layout (location = 0) out vec4 FragColor;

in vec2 vTexCoord;

uniform sampler2D uInput;
uniform vec2 uTexelSize;

// 心理学感知亮度公式 (Luma / Luminance)
float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// 【FXAA (Fast Approximate Anti-Aliasing) 快速近似抗锯齿算法】：
// 一次性屏幕空间后处理抗锯齿算法（全空间开销最小）
// 核心逻辑：
// 1. 采样中心 M 及东西南北 4 个邻域像素，计算亮度 Luma。
// 2. 局部对比度与边缘检测：对比当前像素与四周的亮度差，找出局部最小与最大亮度 [lumaMin, lumaMax]。
// 3. 判定边缘方向：比较垂直梯度 edgeVert 与水平梯度 edgeHorz，选择边缘切线方向。
// 4. 沿切线方向正负偏移半个 Texel 进行二次采样与 50% 混合 (mix(rgbA, rgbB, 0.5))。
// 5. Luma Range Clamping：检查混合后的颜色亮度是否超出原始 [lumaMin, lumaMax] 范围，超出则回退，防止边缘过度模糊或产生颜色伪影。
void main() {
    // 问：这里采样东西南北纹理时，会不会存在纹理数据还没传过来的情况？
    // 答：绝对不会。因为 Scene Pass 已经在显存中完整写完了静态 3D 画面纹理，
    // 且 GLSL 的 texture() 采样是强数据依赖阻塞的，GPU 会确保数据加载就绪后再往下计算。
    vec3 rgbM = texture(uInput, vTexCoord).rgb;
    vec3 rgbN = texture(uInput, vTexCoord + vec2(0.0, uTexelSize.y)).rgb;
    vec3 rgbS = texture(uInput, vTexCoord - vec2(0.0, uTexelSize.y)).rgb;
    vec3 rgbE = texture(uInput, vTexCoord + vec2(uTexelSize.x, 0.0)).rgb;
    vec3 rgbW = texture(uInput, vTexCoord - vec2(uTexelSize.x, 0.0)).rgb;

    float lumaM = luma(rgbM);
    float lumaN = luma(rgbN);
    float lumaS = luma(rgbS);
    float lumaE = luma(rgbE);
    float lumaW = luma(rgbW);
    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));

    // 比较垂直与水平梯度
    float edgeVert = abs((lumaN + lumaS) - 2.0 * lumaM);
    float edgeHorz = abs((lumaE + lumaW) - 2.0 * lumaM);
    bool horz = edgeHorz >= edgeVert;

    // 沿边缘切线方向采样两点
    vec2 offset = horz ? vec2(uTexelSize.x, 0.0) : vec2(0.0, uTexelSize.y);
    vec3 rgbA = texture(uInput, vTexCoord - offset).rgb;
    vec3 rgbB = texture(uInput, vTexCoord + offset).rgb;
    vec3 result = mix(rgbA, rgbB, 0.5);

    // Luma Clamping 校验
    float lumaResult = luma(result);
    result = clamp(result, min(rgbM, result), max(rgbM, result));
    if (lumaResult < lumaMin || lumaResult > lumaMax) {
        result = rgbM;
    }

    FragColor = vec4(result, 1.0);
}

