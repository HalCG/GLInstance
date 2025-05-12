#version 330 core
// 全屏四边形：直接输出 NDC 位置与 UV
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNor;
layout(location = 2) in vec2 aTexCoord;

out vec2 textureCoord;

void main() {
  textureCoord = aTexCoord;
  gl_Position = vec4(aPos, 1.0);
}
