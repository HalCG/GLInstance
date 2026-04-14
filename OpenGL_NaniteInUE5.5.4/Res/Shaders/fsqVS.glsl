// ==============================================================================
// fsqVS.glsl — Pass 6 全屏四边形顶点着色器
// 职责：输出裁剪空间全屏三角 + 传递 UV（无矩阵变换）
// 详述：doc/Code_Reading_Guide.md §1.2 Pass 6；上屏贴 Visualization 纹理
// ==============================================================================
#version 450
layout(location=0)in vec4 position;
layout(location=1)in vec4 texcoord;

layout(location=0)out vec4 V_Texcoord;
void main(){
    V_Texcoord=texcoord;
    gl_Position=position;
}