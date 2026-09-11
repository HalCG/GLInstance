#version 430 core

// 最终渲染输出至屏幕 (Location 0)
layout (location = 0) out vec4 FragColor;

in vec2 vTexCoord; // 全屏 Quad 的屏幕纹理坐标 [0, 1]

// G-Buffer 输入采样器 (来自 Geometry Pass 生成的 4 张纹理)
uniform sampler2D uGAlbedo;     // 颜色附件 (Color Attachment 0)
uniform sampler2D uGNormal;     // 法线附件 (Color Attachment 1)
uniform sampler2D uGMaterial;   // 材质参数附件 (Color Attachment 2)
uniform sampler2D uGDepth;      // 深度附件 (Depth Attachment)

uniform vec3 uCameraPos;        // 相机世界坐标
uniform mat4 uInvView;         // 逆视图矩阵 (Camera Space -> World Space)
uniform mat4 uInvProjection;   // 逆投影矩阵 (NDC Space -> Camera Space)

// 点光源结构体 (与 CPU 端 GpuPointLight 保持一致)
struct PointLight {
    vec4 positionRadius;  // xyz: 世界坐标, w: 影响半径
    vec4 colorIntensity;  // rgb: 光源颜色, w: 强弱系数
};

// SSBO 绑定点 0：从 GPU 读取全局点光源数组
layout(std430, binding = 0) readonly buffer LightBuffer {
    PointLight lights[];
};

uniform int uLightCount;

// 根据屏幕 UV [0, 1] 和高精度 Depth 值反推重建世界坐标 (worldPos)
// 【几何与数学推导逻辑】：
// 1. clip：将 UV [0, 1] 映射到 NDC (Normalized Device Coordinates) 空间 [-1, 1]。
//    注意：OpenGL 深度值 depth 也在 [0, 1] 范围内，同样映射到 NDC z [-1, 1]。
// 2. viewPos：使用逆投影矩阵 (uInvProjection) 将 NDC 坐标变换回齐次相机空间 (View Space)，
//    然后执行透视除法 (viewPos /= viewPos.w) 得到真正的 3D 相机空间坐标。
// 3. worldPos：使用逆视图矩阵 (uInvView) 将相机空间坐标变换为最终的世界空间坐标。
// 【显存带宽优化】：
// 延迟渲染的瓶颈在于显存带宽。如果不从 Depth 重建，而单独开设一个 `gPosition` 纹理（RGB16F），
// 每个像素在 Pass 1 写入和 Pass 2 读取时要额外多读写 6 字节。直接利用现有的 Depth 纹理解算，
// 仅需增加极其微小的 GPU 矩阵乘法计算量，即可节省巨量的 VRAM 读写带宽！
vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = uInvProjection * clip;
    viewPos /= viewPos.w;
    vec4 worldPos = uInvView * viewPos;
    return worldPos.xyz;
}

uniform bool uEnableHDR = true; // HDR Tone Mapping & Gamma 校正开关

void main() {
    // 1. 从 G-Buffer 4 个附件纹理中采样像素几何与材质属性
    vec3 albedo = texture(uGAlbedo, vTexCoord).rgb;
    vec3 normal = normalize(texture(uGNormal, vTexCoord).rgb);
    vec3 materialK = texture(uGMaterial, vTexCoord).rgb; // x: Ka, y: Kd, z: Ks
    float depth = texture(uGDepth, vTexCoord).r;

    // 2. 根据 Depth 反推当前像素的世界空间坐标
    vec3 worldPos = reconstructWorldPos(vTexCoord, depth);
    vec3 viewDir = normalize(uCameraPos - worldPos);

    // 3. 基础环境光计算 (Ambient = Ka * Albedo)
    vec3 result = materialK.x * albedo;
    int count = min(uLightCount, 512);

    // 4. 【延迟渲染光照 Pass 核心】：全屏按像素遍历所有点光源累加光照
    // 与 Forward 路径区别：光照计算在全屏 Quad 阶段集中按像素触发，复杂度与几何体数量完全解耦
    for (int i = 0; i < count; ++i) {
        vec3 lightPos = lights[i].positionRadius.xyz;
        float radius = lights[i].positionRadius.w;
        vec3 lightColor = lights[i].colorIntensity.rgb * lights[i].colorIntensity.w;

        vec3 lightDir = lightPos - worldPos;
        float dist = length(lightDir);
        if (dist > radius) {
            continue;
        }
        lightDir = normalize(lightDir);

        // 光源衰减
        float attenuation = 1.0 - smoothstep(radius * 0.7, radius, dist);
        // Blinn-Phong 漫反射
        vec3 diffuse = materialK.y * max(dot(normal, lightDir), 0.0) * lightColor * albedo;
        // Blinn-Phong 高光
        vec3 halfway = normalize(lightDir + viewDir);
        vec3 specular = materialK.z * pow(max(dot(normal, halfway), 0.0), 32.0) * lightColor;

        result += (diffuse + specular) * attenuation;
    }

    // 5. 后处理 Pass：HDR Tone Mapping & Gamma 校正
    if (uEnableHDR) {
        // Reinhard Tone Mapping: result / (result + 1.0) 平滑压缩 [0, +inf) -> [0, 1)，防止多光源叠加导致高光死白
        result = result / (result + vec3(1.0));
        // Gamma 2.2 空间校正 (1.0 / 2.2)
        result = pow(result, vec3(1.0 / 2.2));
    }

    FragColor = vec4(result, 1.0);
}
