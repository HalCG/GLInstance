#version 430 core
// 将累积颜色与背景色合成并输出到屏幕
layout(location = 0) out vec4 FragColor;

in vec2 textureCoord;

uniform vec3 background_color;
uniform sampler2D texture_diffuse;

void main() {
  vec4 frontColor = texture(texture_diffuse, textureCoord);
  FragColor = frontColor + vec4(background_color, 1.0) * frontColor.a;
  FragColor.a = 1.0;
}
