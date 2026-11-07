#version 430 core

// 最终屏幕颜色输出 (Location 0)
layout (location = 0) out vec4 FragColor;

// 从 mesh.vert 传来的插值变量
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoords;

// 点光源结构体 (布局与 CPU 端的 GpuPointLight 保持一致)
struct PointLight {
    vec4 positionRadius;  // xyz: 世界坐标, w: 影响半径 (Radius)
    vec4 colorIntensity;  // rgb: 光源颜色, w: 光照强度 (Intensity)
};

// SSBO 绑定点 0：从 GPU 显存共享只读点光源数组 (LightBuffer)
layout(std430, binding = 0) readonly buffer LightBuffer {
    PointLight lights[];
};

// Uniform 变量
uniform vec3 uCameraPos;             // 相机世界坐标 (用于计算视角向量 viewDir)
uniform vec3 uMaterialK;             // 材质系数 x: 环境光 Ka, y: 漫反射 Kd, z: 高光 Ks
uniform int uLightCount;             // 场景中当前激活的点光源数量
uniform bool uEnableHDR;             // 与 Deferred 共用：Reinhard + Gamma 开关
uniform sampler2D texture_diffuse1;  // 漫反射纹理

void main() {
    // 1. 采样漫反射纹理，归一化法线与计算视角方向
    vec3 albedo = texture(texture_diffuse1, vTexCoords).rgb;
    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(uCameraPos - vWorldPos);

    // 2. 基础环境光计算 (Ambient)
    vec3 result = uMaterialK.x * albedo;
    int count = min(uLightCount, 512);

    // 3. 【前向渲染核心】：对每个像素，死循环遍历场景中的所有点光源
    // 缺点：光源数量较多时，片元着色器内的 for 循环计算量暴增，导致严重卡顿
    for (int i = 0; i < count; ++i) {
        vec3 lightPos = lights[i].positionRadius.xyz;
        float radius = lights[i].positionRadius.w;
        vec3 lightColor = lights[i].colorIntensity.rgb * lights[i].colorIntensity.w;

        vec3 lightDir = lightPos - vWorldPos;
        float dist = length(lightDir);

        // 超出光源作用半径的片元直接跳过，避免无效计算
        if (dist > radius) {
            continue;
        }
        lightDir = normalize(lightDir);

        // 距离衰减：中心更柔和，边缘更快收束，减轻多灯叠加过曝
        float attenuation = pow(clamp(1.0 - dist / radius, 0.0, 1.0), 2.0);

        // Blinn-Phong 漫反射 (Diffuse)
        vec3 diffuse = uMaterialK.y * max(dot(normal, lightDir), 0.0) * lightColor * albedo;

        // Blinn-Phong 镜面高光 (Specular - 半角向量 Halfway Vector)
        vec3 halfway = normalize(lightDir + viewDir);
        vec3 specular = uMaterialK.z * pow(max(dot(normal, halfway), 0.0), 48.0) * lightColor;

        // 累加光照结果
        result += (diffuse + specular) * attenuation;
    }

    // 与 Deferred / Forward+ 同一套后处理，便于关 HDR 时公平对比
    if (uEnableHDR) {
        result = result / (result + vec3(1.0));
        result = pow(result, vec3(1.0 / 2.2));
    }
    FragColor = vec4(result, 1.0);
}
