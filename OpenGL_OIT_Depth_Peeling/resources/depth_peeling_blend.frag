#version 330 core
// 将当前剥离层颜色 front-to-back 混合到累积缓冲
out vec4 FragColor;

in vec2 textureCoord;

uniform sampler2D texture_diffuse;

void main() {
  FragColor = texture(texture_diffuse, textureCoord);
}
