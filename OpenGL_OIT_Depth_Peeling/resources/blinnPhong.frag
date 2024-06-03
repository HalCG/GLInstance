#version 430 core

// 不透明物体 Blinn-Phong 光照 Fragment Shader
layout (location = 0) out vec4 FragColor;

in vec3 vertexPos;       // 世界空间顶点位置
in vec3 vertexNor;       // 世界空间法线
in vec2 textureCoord;    // 纹理坐标 UV

uniform vec3 cameraPos;  // 相机世界坐标
uniform vec3 lightPos;   // 光源世界坐标
uniform vec3 k;          // 材质参数向量 (k.x: 环境光, k.y: 漫反射, k.z: 高光)

uniform sampler2D texture_diffuse;

void main() {
    vec3 lightColor = vec3(1.0, 1.0, 1.0);

    // 1. 环境光 (Ambient = Ka * LightColor)
    float ambientStrength = k[0];
    vec3 ambient = ambientStrength * lightColor;

    // 2. 漫反射 (Diffuse = Kd * max(N · L, 0) * LightColor)
    float diffuseStrength = k[1];
    vec3 normalDir = normalize(vertexNor);
    vec3 lightDir = normalize(lightPos - vertexPos);
    vec3 diffuse = diffuseStrength * max(dot(normalDir, lightDir), 0.0) * lightColor;

    // 3. 镜面高光 (Blinn-Phong Specular = Ks * max(N · H, 0)^s * LightColor)
    float specularStrength = k[2];
    vec3 viewDir = normalize(cameraPos - vertexPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    vec3 specular = specularStrength * pow(max(dot(normalDir, halfwayDir), 0.0), 2.0) * lightColor;

    // 4. 物体颜色/纹理采样
    vec3 objectColor = vec3(0.8, 0.8, 0.8);
    if (textureCoord.x >= 0.0 && textureCoord.y >= 0.0) {
        objectColor = texture(texture_diffuse, textureCoord).rgb;
    }

    // 5. 组合光照与颜色输出
    FragColor = vec4((ambient + diffuse + specular) * objectColor, 1.0);
}