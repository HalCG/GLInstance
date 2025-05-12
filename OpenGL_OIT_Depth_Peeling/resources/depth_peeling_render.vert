#version 430 core
// 深度剥离 pass：输出世界空间位置/法线与纹理坐标
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
  gl_Position = projection * view * model * vec4(aPos, 1.0);
  vertexPos = vec3(model * vec4(aPos, 1.0));
  vertexNor = mat3(transpose(inverse(model))) * aNor;
}
