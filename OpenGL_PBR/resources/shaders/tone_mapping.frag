#version 430 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D U_HDRBuffer; // 离屏 HDR 帧缓冲区浮点颜色贴图 (GL_RGBA16F)
uniform float exposure;        // 曝光度调节参数 (Exposure multiplier)
uniform float gamma;           // Gamma 校正系数 (默认 2.2)

void main()
{
    // 1. 从离屏 HDR 帧缓冲区采样浮点颜色值 (亮度可能远大于 1.0)
    vec3 hdrColor = texture(U_HDRBuffer, TexCoords).rgb;

    // 2. 曝光色调映射算法 (Exposure Tone Mapping): 将高动态范围 HDR 映射压缩至 [0, 1] LDR 显色范围
    vec3 mapped = vec3(1.0) - exp(-hdrColor * exposure);

    // 3. Gamma 校正编码 (Gamma Correction / Encoding): pow(mapped, 1.0 / 2.2)，从线性颜色空间转回到显示器 sRGB 物理空间
    mapped = pow(mapped, vec3(1.0 / gamma));

    FragColor = vec4(mapped, 1.0);
}

