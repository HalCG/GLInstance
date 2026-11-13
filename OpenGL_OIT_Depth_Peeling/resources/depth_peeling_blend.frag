#version 330 core
// Blend Pass 片元：只采样本层 peel 颜色，当作混合的 src
// - texture_diffuse = fboPeel_.color（Peel 刚写出的一层）
// - Front-to-Back 公式不在这里算，而在 CPU：
//     glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA)
//   硬件把本 FragColor(src) 叠进 fboAccum_(dst)
out vec4 FragColor;

in vec2 textureCoord;

uniform sampler2D texture_diffuse;

void main() {
  FragColor = texture(texture_diffuse, textureCoord);
}
