#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNor;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 vertexPos;
out vec3 vertexNor;
out vec2 textureCoord;

void main() {
  textureCoord = aTexCoord;
  // 裁剪空间坐标系 (clip space) 中 点的位置
  gl_Position = projection * view * model * vec4(aPos, 1.0f);
  // 世界坐标系 (world space) 中 点的位置
  vertexPos = (model * vec4(aPos, 1.0f)).xyz;
  // 世界坐标系 (world space) 中 点的法向
  vertexNor = mat3(transpose(inverse(model))) * aNor;
}