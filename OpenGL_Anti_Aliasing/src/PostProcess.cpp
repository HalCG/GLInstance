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
    blitShader_ = compileProgram(AppConfig::shaderPath("fullscreen.vert").c_str(),
                                 AppConfig::shaderPath("blit.frag").c_str());
    fxaaShader_ = compileProgram(AppConfig::shaderPath("fullscreen.vert").c_str(),
                                 AppConfig::shaderPath("fxaa.frag").c_str());

    if (blitShader_ == 0 || fxaaShader_ == 0) {
        return false;
    }

    //在 OpenGL 中，通过字符串名称（如 "uInput"）去查询变量位置的函数 glGetUniformLocation 开销比较大（涉及字符串哈希查找）。
    //因此，常规的做法是在程序**初始化（init）时查好并保存到 C++ 类的成员变量中
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

    return blitShader_ != 0 && fxaaShader_ != 0;
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
    // 【后处理为什么还需要顶点阶段？不能只用 Fragment Shader 吗？】
    // GPU 管线硬规则：无几何体就没有片元（Fragment）。Fragment Shader 无法凭空运行，必须由光栅化单元切像素来触发。
    // 这里绑定全屏 4 个顶点，绘制 GL_TRIANGLE_FAN（2个三角形组成覆盖屏幕的矩形 Quad）。
    // 【范围】：覆盖当前应用的窗口视口区域（Viewport），而不是物理显示器全屏。
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}

void PostProcess::blitTexture(GLuint colorTexture) const {
    // 直通画面输出：把 colorTexture 贴在全屏 Quad 上画到屏幕上。
    glDisable(GL_DEPTH_TEST);
    glUseProgram(blitShader_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glUniform1i(blitLocInput_, 0);
    // 【怎样就能直接画到屏幕上？】：此时解绑了离屏 FBO（目标为 0 默认屏幕），
    // 且配置好了 blitShader_ 和输入纹理，调用 drawFullscreen() 即可直接输出到窗口。
    drawFullscreen();
    glEnable(GL_DEPTH_TEST);
}

void PostProcess::applyFxaa(GLuint colorTexture) const {
    // FXAA 后处理抗锯齿：在进入函数时 glUseProgram(fxaaShader_) 绑定 Shader。
    glDisable(GL_DEPTH_TEST);
    glUseProgram(fxaaShader_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glUniform1i(fxaaLocInput_, 0);
    glUniform2f(fxaaLocTexelSize_, 1.0f / static_cast<float>(width_), 1.0f / static_cast<float>(height_));
    // 触发绘制：在全屏矩形上逐像素运行 fxaa.frag，做单帧边缘平滑后画到屏幕。
    drawFullscreen();
    glEnable(GL_DEPTH_TEST);
}

void PostProcess::resolveMsaaToScreen(GLuint msaaFbo, int width, int height) const {
    // 辅助解算函数：直接调用 glBlitFramebuffer 触发硬件 Resolve 把 MSAA 多采样离屏 FBO 搬运到屏幕窗口（FBO 0）。
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
