#version 430 core

// 【延迟渲染几何 Pass (Geometry Pass)】：多渲染目标 (MRT, Multi-Render-Target) 变量输出
layout (location = 0) out vec4 gAlbedo;    // 写入 G-Buffer 附件 0: 颜色 (RGB) + 透明度 (A)
layout (location = 1) out vec3 gNormal;    // 写入 G-Buffer 附件 1: 世界空间法线 (RGB)
layout (location = 2) out vec4 gMaterial;  // 写入 G-Buffer 附件 2: 材质系数 (x:Ka, y:Kd, z:Ks)

// 从 mesh.vert 传递进来的顶点属性
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoords;

// 漫反射纹理采样器
uniform sampler2D texture_diffuse1;

void main() {
    // 1. 采样材质颜色并写入 gAlbedo
    gAlbedo = texture(texture_diffuse1, vTexCoords);

    // 2. 归一化法线并写入 gNormal
    gNormal = normalize(vNormal);

    // 3. 写入固定/自定义材质参数 (Ka=0.15, Kd=0.75, Ks=0.35)
    gMaterial = vec4(0.15, 0.75, 0.35, 1.0);
}
