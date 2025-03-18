#include "TextOverlay.hpp"

bool TextOverlay::init() {
    const char *vs = R"(#version 430 core
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); })";

    const char *fs = R"(#version 430 core
out vec4 FragColor;
uniform vec4 uColor;
void main() { FragColor = uColor; })";

    auto compile = [](GLenum type, const char *src) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        return shader;
    };

    GLuint vsObj = compile(GL_VERTEX_SHADER, vs);
    GLuint fsObj = compile(GL_FRAGMENT_SHADER, fs);
    shader_ = glCreateProgram();
    glAttachShader(shader_, vsObj);
    glAttachShader(shader_, fsObj);
    glLinkProgram(shader_);
    glDeleteShader(vsObj);
    glDeleteShader(fsObj);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    return shader_ != 0;
}

void TextOverlay::shutdown() {
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (shader_) {
        glDeleteProgram(shader_);
        shader_ = 0;
    }
}

void TextOverlay::draw(int screenWidth, int screenHeight) const {
    if (!shader_) {
        return;
    }

    const float panelW = 460.0f / static_cast<float>(screenWidth) * 2.0f;
    const float panelH = 90.0f / static_cast<float>(screenHeight) * 2.0f;
    const float left = -1.0f;
    const float top = 1.0f;
    const float verts[] = {
        left, top - panelH, left + panelW, top - panelH, left + panelW, top,
        left, top - panelH, left + panelW, top,              left, top,
    };

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(shader_);
    glUniform4f(glGetUniformLocation(shader_, "uColor"), 0.05f, 0.05f, 0.08f, 0.72f);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
