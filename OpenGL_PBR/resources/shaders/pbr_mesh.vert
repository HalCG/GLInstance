#version 430 core

// 顶点属性输入 (对应 StaticMeshVertexDataEx 格式)
layout(location = 0) in vec4 position; // 局部坐标系位置 (x, y, z, w)
layout(location = 1) in vec4 texcoord; // 纹理坐标 (u, v, s, t)
layout(location = 2) in vec4 normal;   // 局部坐标系法线向量
layout(location = 3) in vec4 tangent;  // 局部坐标系切线向量

// 变换矩阵 Uniform
uniform mat4 M;   // Model 矩阵：模型局部坐标 -> 世界坐标
uniform mat4 V;   // View 矩阵：世界坐标 -> 观察者/相机坐标
uniform mat4 P;   // Projection 矩阵：相机坐标 -> 裁剪空间坐标
uniform mat4 ITM; // Inverse Transpose Model 矩阵：模型逆转置矩阵，用于正确变换法线向量（消除非等比缩放变形）

// 传递给 Fragment Shader 的插值变量
out vec4 V_Texcoord;      // 纹理坐标
out vec3 V_Normal;        // 世界空间法线向量
out vec3 V_WorldPosition; // 世界空间顶点位置
out mat3 V_TBN;           // 切线-副切线-法线 (TBN) 矩阵，用于将切线空间法线贴图采样值变换到世界空间

void main()
{
    // 1. 变换法线到世界空间（使用逆转置矩阵防止非等比缩放导致法线不再垂直于表面）
    vec3 n = mat3(ITM) * normal.xyz;
    if (length(n) < 0.001) n = vec3(0.0, 1.0, 0.0);
    V_Normal = normalize(n);
    
    // 2. 传递纹理坐标
    V_Texcoord = texcoord;
    
    // 3. 计算世界空间顶点位置
    V_WorldPosition = (M * vec4(position.xyz, 1.0)).xyz;
    
    // 4. 构建 TBN 正交切线空间矩阵 (防止切线与法线平行导致叉乘为零和 NaN 溢出)
    vec3 t = mat3(M) * tangent.xyz;
    if (length(t) < 0.001) t = vec3(1.0, 0.0, 0.0);
    else t = normalize(t);

    vec3 b = cross(V_Normal, t);
    if (length(b) < 0.001) {
        vec3 c1 = cross(V_Normal, vec3(0.0, 0.0, 1.0));
        vec3 c2 = cross(V_Normal, vec3(0.0, 1.0, 0.0));
        b = length(c1) > length(c2) ? normalize(c1) : normalize(c2);
        t = normalize(cross(b, V_Normal));
    } else {
        b = normalize(b);
        t = normalize(cross(b, V_Normal));
    }
    V_TBN = mat3(t, b, V_Normal);
    
    // 5. 计算最终裁剪空间位置
    gl_Position = P * V * vec4(V_WorldPosition, 1.0);
}

