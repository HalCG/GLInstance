#version 430 core

// 色调映射与后处理屏幕全屏四边形顶点着色器
layout(location = 0) in vec4 position; // 剪裁空间坐标
layout(location = 1) in vec4 texcoord; // 屏幕 UV 纹理坐标

out vec2 TexCoords;

void main()
{
    TexCoords = texcoord.xy;
    gl_Position = vec4(position.xy, 0.0, 1.0);
}

