#version 330 core
// Blend Pass 顶点：全屏脚手架（不是再画 3D 场景）
// - 无 model/view/projection；aPos 已是 NDC（quad.obj 顶点 ±1）
// - 只负责铺满屏幕 + 传 UV，真正混合在固定管线 glBlend*
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNor; // 未使用，与网格 VAO layout 对齐
layout(location = 2) in vec2 aTexCoord;

out vec2 textureCoord;

void main() {
  textureCoord = aTexCoord;
  gl_Position = vec4(aPos, 1.0);
}
