#version 430 core
layout (location = 0) out vec4 FragColor;
in vec3 vertexPos;
in vec3 vertexNor;
in vec2 textureCoord;

uniform vec3 cameraPos;
uniform vec3 lightPos;
uniform vec3 k;

uniform sampler2D texture_diffuse;

void main() {

  vec3 lightColor = vec3(1.0f, 1.0f, 1.0f);

  // 环境光
  // Ia = ka * La
  float ambientStrenth = k[0];
  vec3 ambient = ambientStrenth * lightColor;

  // 漫反射
  // Id = kd * max(0, normal · light) * Ld
  float diffuseStrenth = k[1];
  vec3 normalDir = normalize(vertexNor);
  vec3 lightDir = normalize(lightPos - vertexPos);
  vec3 diffuse =
      diffuseStrenth * max(dot(normalDir, lightDir), 0.0) * lightColor;

  // 镜面反射（Phong）
  // Is = ks * (view · reflect)^s * Ls

  // float specularStrenth = k[2];
  // vec3 viewDir = normalize(cameraPos - vertexPos);
  // vec3 reflectDir = reflect(-lightDir, normalDir);
  // vec3 specular = specularStrenth *
  //                 pow(max(dot(viewDir, reflectDir), 0.0f), 2) * lightColor;

  // 镜面反射（Blinn-Phong）
  // Is = ks * (normal · halfway)^s * Ls
  float specularStrenth = k[2];
  vec3 viewDir = normalize(cameraPos - vertexPos);
  vec3 halfwayDir = normalize(lightDir + viewDir);
  vec3 specular = specularStrenth *
                  pow(max(dot(normalDir, halfwayDir), 0.0f), 2) * lightColor;

  // 物体颜色
  vec3 objectColor = vec3(0.8, 0.8, 0.8);
  if (textureCoord.x >= 0 && textureCoord.y >= 0) {
    objectColor = texture(texture_diffuse, textureCoord).xyz;
  }

  FragColor = vec4((ambient + diffuse + specular) * objectColor, 1.0f);
  // FragColor = vec4(1.0, 0.0, 0.0, 1.0); 
}