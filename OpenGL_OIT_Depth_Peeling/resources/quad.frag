#version 330 core
out vec4 FragColor;

in vec2 textureCoord;

uniform sampler2D texture_diffuse;

void main() { 
    vec4 color = texture(texture_diffuse, textureCoord);
    float temp = texture(texture_diffuse, textureCoord).w;
    
    // FragColor = vec4(vec3(temp), 1.0f);
    FragColor = color;

}