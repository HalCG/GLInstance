#version 430 core

// 最终场景渲染结果
layout (location = 0) out vec4 FragColor;

// 来自 scene.vert 的插值变量
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoords;

// Uniform 变量
uniform vec3 uCameraPos;             // 相机世界位置
uniform vec3 uMaterialK;             // 材质系数 (x:Ka, y:Kd, z:Ks)
uniform vec3 uLightPos0;             // 光源 0 世界位置
uniform vec3 uLightPos1;             // 光源 1 世界位置
uniform vec3 uLightColor0;           // 光源 0 颜色
uniform vec3 uLightColor1;           // 光源 1 颜色
uniform sampler2D texture_diffuse1;  // 漫反射纹理采样器

void main() {
    // 1. 采样纹理并计算基本方向向量
    vec3 albedo = texture(texture_diffuse1, vTexCoords).rgb;
    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(uCameraPos - vWorldPos);

    // 2. 基础环境光 (Ambient = Ka * Albedo)
    vec3 result = uMaterialK.x * albedo;

    vec3 lights[2] = vec3[](uLightPos0, uLightPos1);
    vec3 colors[2] = vec3[](uLightColor0, uLightColor1);

    // 3. 对 2 盏主光源计算 Blinn-Phong 漫反射与高光
    for (int i = 0; i < 2; ++i) {
        vec3 lightDir = normalize(lights[i] - vWorldPos);

        // 漫反射 (Diffuse)
        vec3 diffuse = uMaterialK.y * max(dot(normal, lightDir), 0.0) * colors[i] * albedo;

        // 镜面高光 (Specular - 半角向量)
        vec3 halfway = normalize(lightDir + viewDir);
        vec3 specular = uMaterialK.z * pow(max(dot(normal, halfway), 0.0), 64.0) * colors[i];

        result += diffuse + specular;
    }

    FragColor = vec4(result, 1.0);
}
