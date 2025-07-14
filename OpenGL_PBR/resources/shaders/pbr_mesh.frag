#version 430 core
out vec4 FragColor;

// 顶点着色器输入的插值变量
in vec4 V_Texcoord;
in vec3 V_Normal;
in vec3 V_WorldPosition;
in mat3 V_TBN;

// 相机与全局参数
uniform vec3 cameraPos;

// IBL 预计算纹理采样器
uniform samplerCube U_PrefilteredColor; // 预过滤环境立方体贴图 (Specular IBL 第一项)
uniform samplerCube U_DiffuseIrradiance; // 漫反射辐照度立方体贴图 (Diffuse IBL)
uniform sampler2D U_BRDFLUT;            // 2D BRDF 积分查找表 (Specular IBL 第二项)

// PBR 材质贴图采样器
uniform sampler2D U_Albedo;    // 反照率/基础颜色 (sRGB 空间)
uniform sampler2D U_Normal;    // 切线空间法线贴图 (线形空间)
uniform sampler2D U_Emissive;  // 自发光贴图 (线形/sRGB)
uniform sampler2D U_AO;        // 预计算环境光遮蔽贴图 (线形空间)
uniform sampler2D U_Metallic;  // 金属度贴图 (线形空间 0=非金属, 1=金属)
uniform sampler2D U_Roughness; // 粗糙度贴图 (线形空间 0=光滑, 1=粗糙)

// IBL 强度调节参数
uniform float iblDiffuseIntensity;
uniform float iblSpecularIntensity;

const float PI = 3.14159265359;

// sRGB 转 线性 Color 空间解码 (Gamma Decoder, pow(color, 2.2))
vec3 GammaDecode(vec3 sRGB)
{
    return pow(sRGB, vec3(2.2));
}

// 菲涅尔公式 - Fresnel-Schlick 近似公式 (用于直接光照)
// F0: 基础反射率（非金属约为 0.04，金属使用 Albedo 颜色）
// H: 半角向量 (Halfway Vector)
// V: 视角向量 (View Vector)
vec3 F(vec3 F0, vec3 H, vec3 V)
{
    float HdotV = max(0.0, dot(H, V));
    return F0 + (vec3(1.0) - F0) * pow(1.0 - HdotV, 5.0);
}

// 菲涅尔公式 - 考虑粗糙度的 Fresnel-Schlick 近似公式 (用于 IBL 间接光照)
// 在高粗糙度下，边缘菲涅尔反射会被微表面遮挡而减弱
vec3 FRoughness(vec3 F0, float NdotV, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
}

// 法线分布函数 NDF (Normal Distribution Function) - Trowbridge-Reitz GGX
// 评估微表面朝向与半角向量 H 对齐的概率比例（决定高光亮度和斑点大小）
float NDF(vec3 N, vec3 H, float roughness)
{
    float a2 = pow(roughness, 4.0); // 迪士尼与 Epic 推荐 roughness^4 以获得更平滑的粗糙度线性响应
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

// 几何函数 - Schlick-GGX 单个方向的自遮挡/自阴影 (Geometry Shadowing/Masking)
// 直接光照下 k = (roughness + 1)^2 / 8
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// 几何函数 - Smith 方法结合视角与光线方向的双向遮挡
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// 从法线贴图获取世界坐标系下的法线向量
vec3 GetNormalFromMap()
{
    // 将法线贴图采样值 [0, 1] 解包映射到切线空间向量 [-1, 1]
    vec3 tangentNormal = texture(U_Normal, V_Texcoord.xy).xyz * 2.0 - 1.0;
    // 使用顶点着色器传来的 TBN 矩阵转换到世界坐标系
    return normalize(V_TBN * tangentNormal);
}

void main()
{
    // 1. 获取基础向量
    vec3 N = GetNormalFromMap();
    if (length(N) < 0.001 || isnan(N.x)) N = vec3(0.0, 1.0, 0.0);
    else N = normalize(N);

    vec3 V = cameraPos - V_WorldPosition;
    if (length(V) < 0.001 || isnan(V.x)) V = vec3(0.0, 0.0, 1.0);
    else V = normalize(V);

    vec3 R = reflect(-V, N); // 世界空间反射向量
    if (length(R) < 0.001 || isnan(R.x)) R = vec3(0.0, 1.0, 0.0);
    else R = normalize(R);

    // 2. 采样 PBR 材质贴图参数并转换到线性空间
    vec3 albedo = GammaDecode(texture(U_Albedo, V_Texcoord.xy).rgb);
    float roughness = texture(U_Roughness, V_Texcoord.xy).r;
    roughness = max(roughness, 0.01); // 避免 roughness = 0 导致除以 0 异常
    float metallic = texture(U_Metallic, V_Texcoord.xy).r;
    float ao = texture(U_AO, V_Texcoord.xy).r;
    vec3 emissive = texture(U_Emissive, V_Texcoord.xy).rgb;

    // 3. 计算基础反射率 F0 (0.04 为大多数非金属绝缘体的固定值；金属则直接使用 Albedo 颜色)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // =========================================================================
    // 4. 直接光照计算 (Direct Lighting)
    // =========================================================================
    vec3 L = normalize(vec3(1.0, 1.0, 1.0)); // 示例方向光光照向量
    vec3 H = normalize(L + V);                // 半角向量
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    vec3 directColor = vec3(0.0);
    if (NdotL > 0.0)
    {
        // 镜面反射占比 kS，漫反射占比 kD (能量守恒: kD + kS <= 1.0)
        vec3 F_dir = F(F0, H, V);
        vec3 kS = F_dir;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic); // 纯金属没有漫反射 (kD = 0)

        // Cook-Torrance Specular BRDF 项
        float D = NDF(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 specular = (D * kS * G) / (4.0 * NdotV * NdotL + 0.0001); // +0.0001 放置分母除零
        
        // Lambertian Diffuse BRDF 项
        vec3 diffuse = kD * albedo / PI;

        // 直接光照结果 (分出能量和角度相乘)
        directColor = (diffuse + specular) * vec3(1.0) * 2.0 * NdotL;
    }

    // =========================================================================
    // 5. IBL 间接环境光照计算 (Image-Based Lighting - Split-Sum 近似公式)
    // =========================================================================
    
    // (1) IBL 漫反射部分 (Diffuse IBL)
    vec3 F_ibl = FRoughness(F0, NdotV, roughness);
    vec3 kS_ibl = F_ibl;
    vec3 kD_ibl = (vec3(1.0) - kS_ibl) * (1.0 - metallic);
    
    vec3 irradiance = texture(U_DiffuseIrradiance, N).rgb;
    vec3 diffuseIBL = kD_ibl * irradiance * albedo * iblDiffuseIntensity;

    // (2) IBL 镜面反射部分 (Specular IBL)
    // 第一项: Prefiltered Environment Map 按照粗糙度 Mip 级别采样
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(U_PrefilteredColor, R, roughness * MAX_REFLECTION_LOD).rgb;
    
    // 第二项: BRDF 2D LUT 查找表 (基于 NdotV 和 roughness 采样 (scale, bias))
    vec2 brdf = texture(U_BRDFLUT, vec2(NdotV, roughness)).rg;
    vec3 specularIBL = prefilteredColor * (F0 * brdf.x + brdf.y) * iblSpecularIntensity;

    // 6. 最终颜色合成 (直接光 + IBL 间接光 * 环境光遮蔽 AO + 自发光 Emissive)
    vec3 ambient = (diffuseIBL + specularIBL) * ao;
    vec3 finalColor = directColor + ambient + emissive;

    FragColor = vec4(finalColor, 1.0);
}

