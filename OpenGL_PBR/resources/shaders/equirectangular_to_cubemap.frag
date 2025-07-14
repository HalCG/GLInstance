#version 430 core
out vec4 FragColor;
in vec3 WorldPos;

uniform sampler2D equirectangularMap; // 2D 矩形等距柱状投影全景图 (Equirectangular HDR Map)

const float PI = 3.14159265359;

// 将 3D 方向向量 v 转换为 2D 球面全景投影 UV 坐标 [0, 1]^2
vec2 SampleSphericalMap(vec3 v)
{
    // atan(z, x) 返回 [-PI, PI]，asin(y) 返回 [-PI/2, PI/2]
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    // 归一化缩放到 [-0.5, 0.5] (除以 2*PI 和 PI)
    uv *= vec2(0.1591, 0.3183); // 1 / (2*PI), 1 / PI
    // 平移到 [0, 1] 范围
    uv += 0.5;
    return uv;
}

void main()
{
    // 归一化 3D 观察方向向量
    vec3 normal = normalize(WorldPos);
    // 转换得到 2D UV 坐标
    vec2 uv = SampleSphericalMap(normal);
    // 采样 2D HDR 贴图并写入 3D 立方体贴图对应面
    vec3 color = texture(equirectangularMap, uv).rgb;
    FragColor = vec4(color, 1.0);
}

