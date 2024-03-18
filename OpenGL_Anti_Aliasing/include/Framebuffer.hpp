#ifndef FRAMEBUFFER_HPP
#define FRAMEBUFFER_HPP

#include <glad/glad.h>

// 单采样离屏 FBO：None / FXAA / TAA 的 scene pass 输出（color + depth 纹理）
class SingleSampleFbo {
public:
    void create(int width, int height);   // 分配 FBO 与 color/depth 纹理
    void destroy();                       // 释放 GPU 资源
    void resize(int width, int height);   // 尺寸变化时 destroy + create

    void bind() const;                    // 绑定为 GL_DRAW_FRAMEBUFFER
    void unbind() const;                  // 解绑到默认 framebuffer
    void blitColorToDefault(int width, int height) const; // None 模式：color 纹理 blit 到屏幕

    GLuint fbo() const { return fbo_; }
    GLuint colorTexture() const { return color_; }
    GLuint depthTexture() const { return depth_; }
    int width() const { return width_; }
    int height() const { return height_; }
    void blitColorTo(GLuint drawFbo, int dstW, int dstH) const; // 复制颜色到目标 FBO

private:
    GLuint fbo_ = 0;
    GLuint color_ = 0;
    GLuint depth_ = 0;
    int width_ = 0;
    int height_ = 0;
};

// 多重采样 FBO：MSAA 模式的 scene pass 输出
class MsaaFbo {
public:
    void create(int width, int height, int samples); // 分配 multisample color/depth
    void destroy();
    void resize(int width, int height, int samples);

    void bind() const;
    void unbind() const;
    void resolveColorToDefault(int width, int height) const; // MSAA resolve 到默认 framebuffer
    void resolveColorTo(GLuint drawFbo, int dstW, int dstH) const; // MSAA resolve 到指定 FBO（对比用）

    int samples() const { return samples_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    GLuint fbo_ = 0;
    GLuint colorMs_ = 0;
    GLuint depthMs_ = 0;
    int width_ = 0;
    int height_ = 0;
    int samples_ = 4;
};

#endif
