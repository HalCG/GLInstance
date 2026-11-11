#include "PostProcess.hpp"

#include "AppConfig.hpp"
#include "Shader.hpp"

namespace {
const float kQuadVerts[] = {
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,
    1.0f,  1.0f,  1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 1.0f,
};

GLuint compileProgram(const char *vertPath, const char *fragPath) {
    Shader shader(vertPath, fragPath);
    return shader.ID;
}
} // namespace

bool PostProcess::init() {
    // 先落成 std::string，避免 .c_str() 指向临时对象
    const std::string vert = AppConfig::shaderPath("fullscreen.vert");
    const std::string blitFrag = AppConfig::shaderPath("blit.frag");
    const std::string fxaaFrag = AppConfig::shaderPath("fxaa.frag");
    blitShader_ = compileProgram(vert.c_str(), blitFrag.c_str());
    fxaaShader_ = compileProgram(vert.c_str(), fxaaFrag.c_str());

    if (blitShader_ == 0 || fxaaShader_ == 0) {
        return false;
    }
    auto linked = [](GLuint prog) {
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        return ok == GL_TRUE;
    };
    if (!linked(blitShader_) || !linked(fxaaShader_)) {
        return false;
    }

    blitLocInput_ = glGetUniformLocation(blitShader_, "uInput");
    fxaaLocInput_ = glGetUniformLocation(fxaaShader_, "uInput");
    fxaaLocTexelSize_ = glGetUniformLocation(fxaaShader_, "uTexelSize");

    glGenVertexArrays(1, &quadVao_);
    glGenBuffers(1, &quadVbo_);
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(2 * sizeof(float)));
    glBindVertexArray(0);

    return true;
}

void PostProcess::shutdown() {
    if (quadVbo_) {
        glDeleteBuffers(1, &quadVbo_);
        quadVbo_ = 0;
    }
    if (quadVao_) {
        glDeleteVertexArrays(1, &quadVao_);
        quadVao_ = 0;
    }
    if (blitShader_) {
        glDeleteProgram(blitShader_);
        blitShader_ = 0;
    }
    if (fxaaShader_) {
        glDeleteProgram(fxaaShader_);
        fxaaShader_ = 0;
    }
}

void PostProcess::resize(int width, int height) {
    width_ = width;
    height_ = height;
}

void PostProcess::drawFullscreen() const {
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}

void PostProcess::blitTexture(GLuint colorTexture) const {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(blitShader_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glUniform1i(blitLocInput_, 0);
    drawFullscreen();
    glEnable(GL_DEPTH_TEST);
}

// FXAA 模式：对场景离屏颜色图做全屏后处理抗锯齿，结果画到当前绑定的 FBO/屏幕
void PostProcess::applyFxaa(GLuint colorTexture) const {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(fxaaShader_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glUniform1i(fxaaLocInput_, 0);

    glUniform2f(fxaaLocTexelSize_,
                1.0f / static_cast<float>(width_),
                1.0f / static_cast<float>(height_));

    drawFullscreen();
    glEnable(GL_DEPTH_TEST);
}
