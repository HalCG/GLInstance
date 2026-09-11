#version 430 core
// 画面直通/贴图片元着色器（Pass-through Fragment Shader）
// 作用：最基础的“画面搬运工”。直接读取输入纹理 uInput 对应 UV 坐标的颜色，
// 原封不动地输出到屏幕或目标 FBO 上，不做任何调色或抗锯齿修饰。

layout (location = 0) out vec4 FragColor;

in vec2 vTexCoord;

uniform sampler2D uInput;

void main() {
    FragColor = texture(uInput, vTexCoord);
}
