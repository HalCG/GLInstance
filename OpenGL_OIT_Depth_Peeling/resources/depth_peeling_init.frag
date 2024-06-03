#version 430 core
// 初始化 pass 用着色器（原工程加载，当前帧循环未调用）
layout(location = 0) out vec4 FragColor;

in vec3 vertexPos;
in vec3 vertexNor;
in vec2 textureCoord;

uniform sampler2D texture_diffuse;

void main() {
  vec3 objectColor = vec3(0.8);
  float alpha = 0.0;
  if (textureCoord.x >= 0.0 && textureCoord.y >= 0.0) {
    vec4 sampled = texture(texture_diffuse, textureCoord);
    objectColor = sampled.rgb;
    alpha = sampled.a;
  }
  FragColor = vec4(objectColor, alpha);
}
