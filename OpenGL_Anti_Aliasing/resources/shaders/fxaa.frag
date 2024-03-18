#version 430 core
layout (location = 0) out vec4 FragColor;

in vec2 vTexCoord;

uniform sampler2D uInput;
uniform vec2 uTexelSize;

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec3 rgbM = texture(uInput, vTexCoord).rgb;
    vec3 rgbN = texture(uInput, vTexCoord + vec2(0.0, uTexelSize.y)).rgb;
    vec3 rgbS = texture(uInput, vTexCoord - vec2(0.0, uTexelSize.y)).rgb;
    vec3 rgbE = texture(uInput, vTexCoord + vec2(uTexelSize.x, 0.0)).rgb;
    vec3 rgbW = texture(uInput, vTexCoord - vec2(uTexelSize.x, 0.0)).rgb;
    vec3 rgbNW = texture(uInput, vTexCoord + vec2(-uTexelSize.x, uTexelSize.y)).rgb;
    vec3 rgbNE = texture(uInput, vTexCoord + vec2(uTexelSize.x, uTexelSize.y)).rgb;
    vec3 rgbSW = texture(uInput, vTexCoord + vec2(-uTexelSize.x, -uTexelSize.y)).rgb;
    vec3 rgbSE = texture(uInput, vTexCoord + vec2(uTexelSize.x, -uTexelSize.y)).rgb;

    float lumaM = luma(rgbM);
    float lumaN = luma(rgbN);
    float lumaS = luma(rgbS);
    float lumaE = luma(rgbE);
    float lumaW = luma(rgbW);
    float lumaNW = luma(rgbNW);
    float lumaNE = luma(rgbNE);
    float lumaSW = luma(rgbSW);
    float lumaSE = luma(rgbSE);

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaRange = lumaMax - lumaMin;

    if (lumaRange < 0.0312) {
        FragColor = vec4(rgbM, 1.0);
        return;
    }

    float edgeVert = abs(lumaN + lumaS - 2.0 * lumaM);
    float edgeHorz = abs(lumaE + lumaW - 2.0 * lumaM);
    edgeVert += abs(lumaNW + lumaNE - 2.0 * lumaN) * 0.25;
    edgeHorz += abs(lumaNE + lumaSE - 2.0 * lumaE) * 0.25;
    bool horz = edgeHorz >= edgeVert;

    vec2 dir = horz ? vec2(uTexelSize.x, 0.0) : vec2(0.0, uTexelSize.y);
    vec3 rgb1 = texture(uInput, vTexCoord - dir).rgb;
    vec3 rgb2 = texture(uInput, vTexCoord + dir).rgb;
    vec3 rgb3 = texture(uInput, vTexCoord - 2.0 * dir).rgb;
    vec3 rgb4 = texture(uInput, vTexCoord + 2.0 * dir).rgb;

    vec3 blendA = mix(rgb1, rgb2, 0.5);
    vec3 blendB = mix(rgb3, rgb4, 0.5);
    vec3 result = mix(blendA, blendB, 0.25);
    result = mix(rgbM, result, 0.75);

    float lumaResult = luma(result);
    if (lumaResult < lumaMin || lumaResult > lumaMax) {
        result = blendA;
    }

    FragColor = vec4(result, 1.0);
}
