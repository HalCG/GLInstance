#version 330 core
out vec4 FragColor;

in vec2 textureCoord;

uniform sampler2D diffuse_texture;

void main() { 
    vec4 color = texture(diffuse_texture, textureCoord);
    float temp = texture(diffuse_texture, textureCoord).r;
    
    // FragColor = vec4(vec3(temp), 1.0f);
    FragColor = color;
}