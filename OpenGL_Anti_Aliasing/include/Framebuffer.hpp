#ifndef FRAMEBUFFER_HPP
#define FRAMEBUFFER_HPP

#include <glad/glad.h>

// 单采样离屏 FBO：None / FXAA / TAA 的 scene 输出（普通 2D color + depth）
class SingleSampleFbo {
public:
    void create(int width, int height);
    void destroy();
    void resize(int width, int height);

    void bind() const;
    void unbind() const;

    // blit：源已是单采样，glBlitFramebuffer 只做颜色拷贝（可缩放），不产生 AA
    void blitColorToDefault(int width, int height) const; // → 窗口默认 FBO(0)
    void blitColorTo(GLuint drawFbo, int dstW, int dstH) const; // → 指定 FBO（如 resolveFbo_）

    GLuint fbo() const { return fbo_; }
    GLuint colorTexture() const { return color_; }
    GLuint depthTexture() const { return depth_; }

private:
    GLuint fbo_ = 0;
    GLuint color_ = 0;
    GLuint depth_ = 0;
    int width_ = 0;
    int height_ = 0;
};

// 多重采样离屏 FBO：MSAA scene 输出（TEXTURE_2D_MULTISAMPLE）
class MsaaFbo {
public:
    void create(int width, int height, int samples);
    void destroy();
    void resize(int width, int height, int samples);

    void bind() const;
    void unbind() const;

    // resolve：源是多样本；同样调 glBlitFramebuffer，但驱动会把 N 个子采样合成 1 个颜色
    // （与单采样 blit 同 API，语义不同：拷贝 vs MSAA 收尾）
    void resolveColorTo(GLuint drawFbo, int dstW, int dstH) const;

private:
    GLuint fbo_ = 0;
    GLuint colorMs_ = 0;
    GLuint depthMs_ = 0;
    int width_ = 0;
    int height_ = 0;
    int samples_ = 4;
};

#endif
