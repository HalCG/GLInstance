#version 430 core
// ============================================================
// FXAA（Fast Approximate Anti-Aliasing）简化实现
// 思路：不依赖几何/深度，只看颜色图。用感知亮度找强边缘，
//       沿边缘切线方向双向采样并混合，近似抹掉锯齿。
// 输入：uInput = 场景单采样颜色；uTexelSize = (1/w, 1/h)
// ============================================================
layout (location = 0) out vec4 FragColor;

in vec2 vTexCoord;

uniform sampler2D uInput;
uniform vec2 uTexelSize; // 一个像素对应的 UV 步长

// 感知亮度（Rec.601）：人眼对绿最敏感，对蓝最不敏感
float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    // ---------- 1) 取 3×3 邻域颜色（中心 + 四邻 + 四角）----------
    // N/S/E/W：上下左右；NW/NE/SW/SE：对角，后面估边缘方向要用
    vec3 rgbM = texture(uInput, vTexCoord).rgb;
    vec3 rgbN = texture(uInput, vTexCoord + vec2(0.0, uTexelSize.y)).rgb;
    vec3 rgbS = texture(uInput, vTexCoord - vec2(0.0, uTexelSize.y)).rgb;
    vec3 rgbE = texture(uInput, vTexCoord + vec2(uTexelSize.x, 0.0)).rgb;
    vec3 rgbW = texture(uInput, vTexCoord - vec2(uTexelSize.x, 0.0)).rgb;
    vec3 rgbNW = texture(uInput, vTexCoord + vec2(-uTexelSize.x, uTexelSize.y)).rgb;
    vec3 rgbNE = texture(uInput, vTexCoord + vec2(uTexelSize.x, uTexelSize.y)).rgb;
    vec3 rgbSW = texture(uInput, vTexCoord + vec2(-uTexelSize.x, -uTexelSize.y)).rgb;
    vec3 rgbSE = texture(uInput, vTexCoord + vec2(uTexelSize.x, -uTexelSize.y)).rgb;

    // ---------- 2) 全部换成亮度，便于比对比度 ----------
    float lumaM = luma(rgbM);
    float lumaN = luma(rgbN);
    float lumaS = luma(rgbS);
    float lumaE = luma(rgbE);
    float lumaW = luma(rgbW);
    float lumaNW = luma(rgbNW);
    float lumaNE = luma(rgbNE);
    float lumaSW = luma(rgbSW);
    float lumaSE = luma(rgbSE);

    // ---------- 3) 局部对比度：太小说明不是强边，直接输出原色 ----------
    // 只用十字邻域估 min/max（省一点，对角后面才参与方向判定）
    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaRange = lumaMax - lumaMin;

    // 阈值约 1/32：平坦区域跳过，避免无谓模糊、也省带宽
    if (lumaRange < 0.0312) {
        FragColor = vec4(rgbM, 1.0);
        return;
    }

    // ---------- 4) 判断边缘走向：水平边 vs 垂直边 ----------
    // 二阶差分：|邻 + 邻 - 2*中心| 越大，该方向上的“边”越明显
    float edgeVert = abs(lumaN + lumaS - 2.0 * lumaM); // 上下变化 → 更像水平边
    float edgeHorz = abs(lumaE + lumaW - 2.0 * lumaM); // 左右变化 → 更像垂直边
    // 对角再补一点权重，减轻斜边判错（简化：只加了 N 行 / E 列一侧）
    // 对称还可加：edgeVert += |SW+SE-2*S|*0.25；edgeHorz += |NW+SW-2*W|*0.25（本 Demo 未加）
    edgeVert += abs(lumaNW + lumaNE - 2.0 * lumaN) * 0.25;
    edgeHorz += abs(lumaNE + lumaSE - 2.0 * lumaE) * 0.25;
    bool horz = edgeHorz >= edgeVert; // true：沿水平方向采样（边大致竖直）

    // ---------- 5) 沿切线方向双向采样并混合（核心 AA）----------
    // 垂直边上左右糊；水平边上下糊 —— 都是沿着“边的走向”抹锯齿
    vec2 dir = horz ? vec2(uTexelSize.x, 0.0) : vec2(0.0, uTexelSize.y);
    vec3 rgb1 = texture(uInput, vTexCoord - dir).rgb;       // ±1 像素
    vec3 rgb2 = texture(uInput, vTexCoord + dir).rgb;
    vec3 rgb3 = texture(uInput, vTexCoord - 2.0 * dir).rgb; // ±2 像素，拉长平滑核
    vec3 rgb4 = texture(uInput, vTexCoord + 2.0 * dir).rgb;

    vec3 blendA = mix(rgb1, rgb2, 0.5);       // 近邻平均
    vec3 blendB = mix(rgb3, rgb4, 0.5);       // 远邻平均
    vec3 result = mix(blendA, blendB, 0.25);  // 近邻为主，远邻略参与
    result = mix(rgbM, result, 0.75);         // 75% 用平滑结果，保留一点中心细节

    // ---------- 6) 亮度钳制：混合结果若跳出局部 luma 范围则回退 ----------
    // 防止过度模糊把对比度冲垮（类似“别糊过头”）
    float lumaResult = luma(result);
    if (lumaResult < lumaMin || lumaResult > lumaMax) {
        result = blendA; // 退回仅近邻平均，比远邻更保守
    }

    FragColor = vec4(result, 1.0);
}
