// ==============================================================================
// fsqFS.glsl — Pass 6 全屏四边形片段着色器
// 职责：采样 Pass 5 输出的 Visualization 纹理，写入默认帧缓冲
// 详述：doc/Code_Reading_Guide.md §5.4；binding=0 → U_VisualizationTexture
// ==============================================================================
#version 450
layout(location=0)in vec4 V_Texcoord;
layout(binding=0)uniform sampler2D U_VisualizationTexture;

layout(location=0)out vec4 RT0;
void main(){
    vec3 color=texture(U_VisualizationTexture,V_Texcoord.xy).rgb;
    RT0=vec4(color,1.0f);
}