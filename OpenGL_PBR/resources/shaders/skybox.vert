#version 430 core

// 天空盒 (Skybox) 顶点着色器
layout(location = 0) in vec4 position;

uniform mat4 projection;
uniform mat4 view;

out vec3 V_Texcoord; // 3D 立方体纹理采样向量

void main()
{
    V_Texcoord = position.xyz;
    
    // 关键步骤 1：使用 mat4(mat3(view)) 移除 View 矩阵的平移分量，使天空盒始终居于观察者中心
    vec4 pos = projection * mat4(mat3(view)) * vec4(position.xyz, 1.0);
    
    // 关键步骤 2：深度优化 Trick (pos.xyww)
    // 在透视除法后 z/w = w/w = 1.0，强制天空盒在 NDC 空间的深度值为最大值 1.0
    // 配合 glDepthFunc(GL_LEQUAL)，可以在渲染完其他不透明物体后仅在背景未填充区域绘制天空盒（Early-Z 优化）
    gl_Position = pos.xyww;
}

