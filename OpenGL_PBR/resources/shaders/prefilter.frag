#version 430 core
out vec4 FragColor;
in vec3 WorldPos;

uniform samplerCube environmentMap; // 原始 HDR 环境立方体贴图
uniform float roughness;            // 当前正在预过滤渲染的 Mip 级别的粗糙度 (0.0 ~ 1.0)

const float PI = 3.14159265359;

// 位反转 low-discrepancy 伪随机序列生成
float RadicalInverse_VDC(uint bits) 
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

// Hammersley 2D 采样点生成
vec2 Hammersley(uint i, uint N)
{
    return vec2(float(i) / float(N), RadicalInverse_VDC(i));
}

// GGX NDF 重要性采样：根据粗糙度生成偏向于高光反射 lobe 的半角向量 H
vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
    float a = roughness * roughness;
    
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    
    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
    
    vec3 up          = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    
    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

// =========================================================================
// IBL 镜面反射预过滤卷积 (Specular Prefiltered Environment Map)
// 假设视线 R = N = V (Isotropic 近似)，利用 GGX 重要性采样对环境光滤波
// 粗糙度越高，高光斑越扩散，导出的 Mipmap 越模糊
// =========================================================================
void main()
{
    vec3 N = normalize(WorldPos);
    vec3 R = N;
    vec3 V = R;

    const uint SAMPLE_COUNT = 1024u;
    float totalWeight = 0.0;
    vec3 prefilteredColor = vec3(0.0);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        // 1. 生成 Hammersley 低差异点集
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        // 2. 重要性采样获取半角向量 H
        vec3 H  = ImportanceSampleGGX(Xi, N, roughness);
        // 3. 计算入射光方向 L
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            // 4. 累加环境光颜色 (根据 NdotL 进行加权)
            prefilteredColor += texture(environmentMap, L).rgb * NdotL;
            totalWeight      += NdotL;
        }
    }

    // 5. 计算最终加权平均颜色
    prefilteredColor = prefilteredColor / totalWeight;

    FragColor = vec4(prefilteredColor, 1.0);
}

