#version 430 core

// 最终屏幕颜色输出 (Location 0)
layout (location = 0) out vec4 FragColor;

// 从 mesh.vert 传来的插值变量
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoords;

// 点光源结构体 (与 CPU 端 GpuPointLight 保持一致)
struct PointLight {
    vec4 positionRadius;  // xyz: 世界坐标, w: 影响半径
    vec4 colorIntensity;  // rgb: 光源颜色, w: 强弱系数
};

// SSBO 绑定点 0：包含场景中所有点光源数据的数组
layout(std430, binding = 0) readonly buffer LightBuffer {
    PointLight lights[];
};

// SSBO 绑定点 1：由 CPU (LightManager) 计算好的每个 Tile 内部有效光源数量数组 (TileCounts)
layout(std430, binding = 1) readonly buffer TileCounts {
    uint counts[];
};

// SSBO 绑定点 2：由 CPU (LightManager) 计算好的每个 Tile 关联的光源 Index 列表 (TileIndices)
layout(std430, binding = 2) readonly buffer TileIndices {
    uint indices[];
};

// Uniform 变量
uniform vec3 uCameraPos;            // 相机世界位置
uniform vec3 uMaterialK;            // 材质系数 (x:Ka, y:Kd, z:Ks)
uniform int uLightCount;            // 光源总数
uniform int uTilesX;                // 横向 Tile 数量
uniform int uTilesY;                // 纵向 Tile 数量
uniform int uTileSize;              // 单个 Tile 像素尺寸 (默认 16x16)
uniform int uMaxLightsPerTile;      // 单个 Tile 最多容纳的光源数量上限 (默认 64)
uniform sampler2D texture_diffuse1; // 漫反射纹理

void main() {
    // 1. 【Forward+ 核心逻辑 1】：根据屏幕像素坐标 (gl_FragCoord.xy) 换算当前片源所属的 Tile 网格坐标 (tx, ty)
    ivec2 tile = ivec2(gl_FragCoord.xy) / uTileSize;
    tile = clamp(tile, ivec2(0), ivec2(uTilesX - 1, uTilesY - 1));

    // 2. 换算成一维 Tile 索引 tileIndex，并从 SSBO 中获取该 Tile 实际影响的光源数量 localCount
    int tileIndex = tile.y * uTilesX + tile.x;
    uint localCount = counts[tileIndex];
    localCount = min(localCount, uint(uMaxLightsPerTile));

    // 3. 基础几何与纹理采样准备
    vec3 albedo = texture(texture_diffuse1, vTexCoords).rgb;
    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 result = uMaterialK.x * albedo; // 基础环境光

    // 4. 【Forward+ 核心逻辑 2】：仅循环遍历该 Tile 内部挑选出的 localCount 盏灯，而不是全场景 256/512 盏！
    // 这大幅压短了 Shader 的 for 循环长度，成功解决了标准 Forward 路径下的“光源爆炸卡顿”问题。
    for (uint i = 0u; i < localCount; ++i) {
        // 间接寻址：从 TileIndices SSBO 中取得真实的灯光数组下标 lightIndex
        uint lightIndex = indices[tileIndex * uMaxLightsPerTile + int(i)];
        if (lightIndex >= uint(uLightCount)) {
            continue;
        }

        // 读取灯光数据
        vec3 lightPos = lights[lightIndex].positionRadius.xyz;
        float radius = lights[lightIndex].positionRadius.w;
        vec3 lightColor = lights[lightIndex].colorIntensity.rgb * lights[lightIndex].colorIntensity.w;

        vec3 lightDir = lightPos - vWorldPos;
        float dist = length(lightDir);
        if (dist > radius) {
            continue;
        }
        lightDir = normalize(lightDir);

        // 衰减与 Blinn-Phong 光照计算
        float attenuation = 1.0 - smoothstep(radius * 0.7, radius, dist);
        vec3 diffuse = uMaterialK.y * max(dot(normal, lightDir), 0.0) * lightColor * albedo;
        vec3 halfway = normalize(lightDir + viewDir);
        vec3 specular = uMaterialK.z * pow(max(dot(normal, halfway), 0.0), 32.0) * lightColor;

        result += (diffuse + specular) * attenuation;
    }

    FragColor = vec4(result, 1.0);
}
