#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNor;
layout(location = 2) in vec2 aTexCoord;

out vec2 textureCoord;

void main() {

  textureCoord = aTexCoord;
  // 裁剪空间坐标系 (clip space) 中 点的位置
  gl_Position = vec4(aPos, 1.0f);
}