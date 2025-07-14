#version 430 core
out vec4 FragColor;

// 顶点插值输入
in vec4 V_Texcoord;
in vec3 V_Normal;
in vec3 V_WorldPosition;

uniform vec3 cameraPos;

// GUI 传入的参数化材质属性 (用于绘制 7x7 球体测试网格)
uniform vec3 albedo;       // 统一基础颜色
uniform float roughness;  // 粗糙度 (逐列变化)
uniform float metallic;   // 金属度 (逐行变化)

// IBL 预计算贴图
uniform samplerCube U_PrefilteredColor; // 预过滤环境贴图
uniform samplerCube U_DiffuseIrradiance; // 漫反射辐照度贴图
uniform sampler2D U_BRDFLUT;            // BRDF 2D 查找表

uniform float iblDiffuseIntensity;
uniform float iblSpecularIntensity;

const float PI = 3.14159265359;

// 菲涅尔 Schlick 公式
vec3 F(vec3 F0, vec3 H, vec3 V)
{
    float HdotV = max(0.0, dot(H, V));
    return F0 + (vec3(1.0) - F0) * pow(1.0 - HdotV, 5.0);
}

// 考虑粗糙度的 IBL 菲涅尔公式
vec3 FRoughness(vec3 F0, float NdotV, float roughnessVal)
{
    return F0 + (max(vec3(1.0 - roughnessVal), F0) - F0) * pow(1.0 - NdotV, 5.0);
}

// Trowbridge-Reitz GGX 法线分布函数
float NDF(vec3 N, vec3 H, float roughnessVal)
{
    float a2 = pow(roughnessVal, 4.0);
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

// Schlick-GGX 几何阴影/遮盖函数
float GeometrySchlickGGX(float NdotV, float roughnessVal)
{
    float r = (roughnessVal + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith 双向几何遮罩函数
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughnessVal)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughnessVal);
    float ggx1 = GeometrySchlickGGX(NdotL, roughnessVal);
    return ggx1 * ggx2;
}

void main()
{
    vec3 N = V_Normal;
    if (length(N) < 0.001 || isnan(N.x)) N = vec3(0.0, 1.0, 0.0);
    else N = normalize(N);

    vec3 V = cameraPos - V_WorldPosition;
    if (length(V) < 0.001 || isnan(V.x)) V = vec3(0.0, 0.0, 1.0);
    else V = normalize(V);

    vec3 R = reflect(-V, N);
    if (length(R) < 0.001 || isnan(R.x)) R = vec3(0.0, 1.0, 0.0);
    else R = normalize(R);

    float rVal = max(roughness, 0.01);
    // 基础反射率 F0 (绝缘体 0.04, 金属根据 albedo 插值)
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // 1. 直接光照计算 (方向光 L=(1,1,1))
    vec3 L = normalize(vec3(1.0, 1.0, 1.0));
    vec3 H = normalize(L + V);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    vec3 directColor = vec3(0.0);
    if (NdotL > 0.0)
    {
        vec3 F_dir = F(F0, H, V);
        vec3 kS = F_dir;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        float D = NDF(N, H, rVal);
        float G = GeometrySmith(N, V, L, rVal);
        vec3 specular = (D * kS * G) / (4.0 * NdotV * NdotL + 0.0001);
        vec3 diffuse = kD * albedo / PI;

        directColor = (diffuse + specular) * vec3(1.0) * 2.0 * NdotL;
    }

    // 2. IBL 环境光计算 (Split-Sum 近似)
    vec3 F_ibl = FRoughness(F0, NdotV, rVal);
    vec3 kS_ibl = F_ibl;
    vec3 kD_ibl = (vec3(1.0) - kS_ibl) * (1.0 - metallic);

    // 漫反射 IBL
    vec3 irradiance = texture(U_DiffuseIrradiance, N).rgb;
    vec3 diffuseIBL = kD_ibl * irradiance * albedo * iblDiffuseIntensity;

    // 镜面反射 IBL
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(U_PrefilteredColor, R, rVal * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(U_BRDFLUT, vec2(NdotV, rVal)).rg;
    vec3 specularIBL = prefilteredColor * (F0 * brdf.x + brdf.y) * iblSpecularIntensity;

    vec3 ambient = diffuseIBL + specularIBL;
    vec3 finalColor = directColor + ambient;

    FragColor = vec4(finalColor, 1.0);
}

