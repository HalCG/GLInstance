#version 430 core
// 输出 2D 查找表 (rg 通道分别存储 菲涅尔 Scale 缩放因子 A 与 Bias 偏移因子 B)
out vec2 FragColor;
in vec2 TexCoords; // x 表示 NdotV (0~1), y 表示 Roughness (0~1)

const float PI = 3.14159265359;

// =========================================================================
// 准蒙特卡洛积分 (Quasi-Monte Carlo Integration) 工具函数
// =========================================================================

// 范德科皮特 Radical Inverse 序列计算 (利用二进制位翻转位移高效实现 2 进制逆序)
// 将整数 i 的二进制按位倒转，得到 [0, 1) 区间内均匀分布的高质低差异伪随机数
float RadicalInverse_VDC(uint bits) 
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // 除以 0x100000000 (2^32)
}

// 生成第 i 个 Hammersley 2D 低差异点集 (低差异点集在 2D 域分布比纯随机数更均匀)
vec2 Hammersley(uint i, uint N)
{
    return vec2(float(i) / float(N), RadicalInverse_VDC(i));
}

// 基于 GGX 法线分布函数 NDF 的重要性采样 (Importance Sampling)
// Xi: 2D 均匀伪随机样本点 [0,1]^2
// N: 表面法线 (通常取 (0,0,1))
// roughness: 材质粗糙度
// 返回值: 按照 GGX 分布偏向高光微表面法线采样得到的半角向量 H
vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
    float a = roughness * roughness; // 采用 a = roughness^2
    
    // 方位角 phi 均匀分布 0~2PI
    float phi = 2.0 * PI * Xi.x;
    // 天顶角 theta 按照 GGX NDF 累积分布函数 CDF 逆变换求解 (把更多样本投射在偏向高光的方向)
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    
    // 从球坐标转换为切线空间笛卡尔坐标 H
    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
    
    // 从切线空间构建 TBN 坐标系转换到世界空间
    vec3 up          = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    
    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

// 几何遮蔽 Schlick-GGX 针对 IBL 预计算的变形 (k = a^2 / 2)
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0; // 注意：IBL 预计算使用的是 k = a^2 / 2，而不是直接光照的 (a+1)^2 / 8

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

// 几何遮蔽 Smith 函数
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// =========================================================================
// BRDF 2D LUT 积分计算 (Split-Sum 第二项)
// 计算理论推导:
// ∫ BRDF * L_in * cos(θ) dω ≈ pbr_specular * (F0 * A + B)
// 其中 A = ∫ (1 - (1-V·H)^5) * G_vis dω
//      B = ∫ (1-V·H)^5 * G_vis dω
// =========================================================================
vec2 IntegrateBRDF(float NdotV, float roughness)
{
    vec3 V;
    V.x = sqrt(1.0 - NdotV * NdotV); // sin(theta)
    V.y = 0.0;
    V.z = NdotV;                     // cos(theta)

    float A = 0.0;
    float B = 0.0;

    vec3 N = vec3(0.0, 0.0, 1.0);

    const uint SAMPLE_COUNT = 1024u;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        // 采样半角向量 H
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        vec3 H  = ImportanceSampleGGX(Xi, N, roughness);
        // 根据视角 V 和半角向量 H 求解反射入射光线 L = 2*(V·H)*H - V
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0)
        {
            // 几何遮蔽与微表面转换几何可见性因子 G_Vis = (G * VdotH) / (NdotH * NdotV)
            float G = GeometrySmith(N, V, L, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0); // 菲涅尔因子次方项

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    return vec2(A, B);
}

void main() 
{
    // TexCoords.x = NdotV，TexCoords.y = roughness
    vec2 integratedBRDF = IntegrateBRDF(TexCoords.x, TexCoords.y);
    FragColor = integratedBRDF;
}

