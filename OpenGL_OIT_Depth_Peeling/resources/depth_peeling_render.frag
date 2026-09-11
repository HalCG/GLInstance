#version 430 core
// 单层深度剥离：与上一层深度比较，通过则输出带光照与透明度的颜色
layout(location = 0) out vec4 FragColor;

in vec3 vertexPos;
in vec3 vertexNor;
in vec2 textureCoord;

uniform vec3 cameraPos;
uniform vec3 lightPos;
uniform vec3 k; // x=环境光, y=漫反射, z=高光

uniform sampler2D texture_diffuse;
uniform sampler2D texture_depth;
uniform vec2 u_ScreenSize;

void main() {
  // 采样上一层保存的 Depth 纹理值 frontDepth
  vec2 uv = gl_FragCoord.xy / u_ScreenSize;
  float frontDepth = texture(texture_depth, uv).r;

  // 【Depth Peeling 核心剥离条件】：
  // 将当前片源的深度 gl_FragCoord.z 与上一层剥离出的深度 frontDepth 进行比较。
  // 如果当前片源深度 <= frontDepth，说明该片源在之前的 Render Pass 中已经被处理过（属于更近或同一层），
  // 必须直接 discard 丢弃！
  // ⚠️ 硬件级副作用：此处显式使用了 `discard` 指令，会导致 GPU 的 Early-Z (前置深度测试) 优化被强制关闭。
  // 片元必须完整执行完 Fragment Shader 后，才能进入 ROP 阶段接受后续的固定管线深度测试。
  // 剩下的片源（gl_FragCoord.z > frontDepth）随后通过 OpenGL 硬件 Z-Test，
  // 就会保留下所有剩余片源中“最靠近上一层”的那个片源，成功剥离出下一层！
  if (gl_FragCoord.z <= frontDepth) {
    discard;
  }

  vec3 lightColor = vec3(1.0);


  float ambientStrength = k.x;
  vec3 ambient = ambientStrength * lightColor;

  float diffuseStrength = k.y;
  vec3 normalDir = normalize(vertexNor);
  vec3 lightDir = normalize(lightPos - vertexPos);
  vec3 diffuse =
      diffuseStrength * max(dot(normalDir, lightDir), 0.0) * lightColor;

  float specularStrength = k.z;
  vec3 viewDir = normalize(cameraPos - vertexPos);
  vec3 halfwayDir = normalize(lightDir + viewDir);
  vec3 specular = specularStrength *
                  pow(max(dot(normalDir, halfwayDir), 0.0), 2.0) * lightColor;

  vec3 objectColor = vec3(0.8);
  float alpha = 0.0;
  if (textureCoord.x >= 0.0 && textureCoord.y >= 0.0) {
    vec4 sampled = texture(texture_diffuse, textureCoord);
    objectColor = sampled.rgb;
    alpha = sampled.a;
  }

  FragColor = vec4((ambient + diffuse + specular) * objectColor, alpha);
}
