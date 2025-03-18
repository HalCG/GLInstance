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
    const std::string wipeFrag = AppConfig::shaderPath("wipe.frag");
    blitShader_ = compileProgram(vert.c_str(), blitFrag.c_str());
    fxaaShader_ = compileProgram(vert.c_str(), fxaaFrag.c_str());
    wipeShader_ = compileProgram(vert.c_str(), wipeFrag.c_str());

    if (blitShader_ == 0 || fxaaShader_ == 0 || wipeShader_ == 0) {
        return false;
    }
    auto linked = [](GLuint prog) {
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        return ok == GL_TRUE;
    };
    if (!linked(blitShader_) || !linked(fxaaShader_) || !linked(wipeShader_)) {
        return false;
    }

    blitLocInput_ = glGetUniformLocation(blitShader_, "uInput");
    fxaaLocInput_ = glGetUniformLocation(fxaaShader_, "uInput");
    fxaaLocTexelSize_ = glGetUniformLocation(fxaaShader_, "uTexelSize");
    wipeLocLeft_ = glGetUniformLocation(wipeShader_, "uLeft");
    wipeLocRight_ = glGetUniformLocation(wipeShader_, "uRight");
    wipeLocWipe_ = glGetUniformLocation(wipeShader_, "uWipe");
    wipeLocTexelSize_ = glGetUniformLocation(wipeShader_, "uTexelSize");

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
    if (wipeShader_) {
        glDeleteProgram(wipeShader_);
        wipeShader_ = 0;
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

void PostProcess::applyFxaa(GLuint colorTexture) const {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(fxaaShader_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glUniform1i(fxaaLocInput_, 0);
    glUniform2f(fxaaLocTexelSize_, 1.0f / static_cast<float>(width_), 1.0f / static_cast<float>(height_));
    drawFullscreen();
    glEnable(GL_DEPTH_TEST);
}

void PostProcess::applyWipe(GLuint leftTexture, GLuint rightTexture, float wipe) const {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(wipeShader_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, leftTexture);
    glUniform1i(wipeLocLeft_, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, rightTexture);
    glUniform1i(wipeLocRight_, 1);
    glUniform1f(wipeLocWipe_, wipe);
    glUniform2f(wipeLocTexelSize_, 1.0f / static_cast<float>(width_), 1.0f / static_cast<float>(height_));
    drawFullscreen();
    glEnable(GL_DEPTH_TEST);
}

void PostProcess::resolveMsaaToScreen(GLuint msaaFbo, int width, int height) const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
