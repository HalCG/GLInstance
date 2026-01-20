#version 430 core
out vec4 FragColor;

in vec3 V_Texcoord;

uniform samplerCube U_SkyBox; // HDR 背景环境立方体贴图

void main()
{
    // 归一化方向向量采样天空盒
    vec3 texcoord = normalize(V_Texcoord);
    vec3 hdrColor = texture(U_SkyBox, texcoord).rgb;
    FragColor = vec4(hdrColor, 1.0);
}

