#version 430 core

// 顶点属性输入 (Vertex Attributes)
layout (location = 0) in vec3 aPos;       // 局部空间顶点位置
layout (location = 1) in vec3 aNormal;     // 局部空间顶点法线
layout (location = 2) in vec2 aTexCoords;  // 顶点纹理坐标 (UV)

// 传递给 Fragment Shader 的插值变量 (Varying Outputs)
out vec3 vWorldPos;                        // 世界空间顶点位置
out vec3 vNormal;                          // 世界空间法线向量
out vec2 vTexCoords;                       // 传递 UV 坐标

// 变换矩阵 Uniform
uniform mat4 model;                        // 模型矩阵 (Local -> World)
uniform mat4 view;                         // 视图矩阵 (World -> Camera)
uniform mat4 projection;                   // 投影矩阵 (Camera -> Clip)

void main() {
    // 1. 将顶点位置变换到世界空间
    vWorldPos = vec3(model * vec4(aPos, 1.0));

    // 2. 使用法线矩阵 (Normal Matrix) 变换法线，防止模型进行非等比例缩放 (Non-uniform scale) 时法线变形
    vNormal = mat3(transpose(inverse(model))) * aNormal;

    // 3. 传递 UV 纹理坐标
    vTexCoords = aTexCoords;

    // 4. 计算最终的裁剪空间顶点位置 (MVP 变换)
    gl_Position = projection * view * vec4(vWorldPos, 1.0);
}
