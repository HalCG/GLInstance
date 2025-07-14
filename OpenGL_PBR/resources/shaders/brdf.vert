#version 430 core

// 屏幕全屏四边形顶点属性输入 (用于 2D BRDF 查找表渲染)
layout(location = 0) in vec4 position; // [-1, 1] 剪裁空间坐标
layout(location = 1) in vec4 texcoord; // [0, 1] UV 纹理坐标 (x=NdotV, y=roughness)

out vec2 TexCoords;

void main()
{
    TexCoords = texcoord.xy;
    // 强制 z 为 0.0，使四边形充满 NDC
    gl_Position = vec4(position.xy, 0.0, 1.0);
}

