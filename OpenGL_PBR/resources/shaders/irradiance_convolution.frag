#version 430 core
out vec4 FragColor;
in vec3 WorldPos; // 对应立方体表面的点坐标 (也就是半球中心法线向量 N)

uniform samplerCube environmentMap; // 原始 HDR 环境立方体贴图

const float PI = 3.14159265359;

// =========================================================================
// IBL 漫反射辐照度卷积 (Diffuse Irradiance Convolution)
// 积分公式: E(N) = ∫_Ω L(ω_i) * (N · ω_i) dω_i
// 采用半球黎曼和 (Riemann Sum) 离散采样卷积
// =========================================================================
void main()
{
    // 法线 N 对应当前渲染的面方向向量
    vec3 N = normalize(WorldPos);
    vec3 irradiance = vec3(0.0);

    // 切线空间基底构建
    vec3 up    = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up         = normalize(cross(N, right));

    // 步长 Delta，步长越小精确度越高 (0.025 rad 约相当于 125 个步长)
    float sampleDelta = 0.025;
    float nrSamples = 0.0;

    // 在半球范围内以极坐标 (phi, theta) 双重循环进行离散积分累加
    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            // 1. 球面坐标系转换为切线空间笛卡尔坐标向量
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            
            // 2. 将切线空间向量变换到世界空间采样方向
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            // 3. 采样 HDR 环境贴图，并乘以 cos(theta) (Lambertian 投影因子) 和 sin(theta) (球面立足角微元权重)
            irradiance += texture(environmentMap, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    // 4. 归一化积分结果 (PI * (1 / nrSamples))
    irradiance = PI * irradiance * (1.0 / float(nrSamples));

    FragColor = vec4(irradiance, 1.0);
}

