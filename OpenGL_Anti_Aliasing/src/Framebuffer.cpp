#include "Framebuffer.hpp"

// 【单采样离线 FBO (SingleSampleFbo)】：
// 用于常规离线 Post-Processing 渲染（如 FXAA、TAA Pass 的输入输出）
// 包含一个 2D 颜色纹理 (GL_RGBA8) 和一个 2D 高精度深度纹理 (GL_DEPTH_COMPONENT32F)
void SingleSampleFbo::create(int width, int height) {
    destroy();
    width_ = width;
    height_ = height;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // 创建单采样 2D 颜色纹理
    glGenTextures(1, &color_);
    glBindTexture(GL_TEXTURE_2D, color_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    // NEAREST：低内部分辨率放大时保留锯齿，便于 AA 对比
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // FBO 可挂多个颜色附件(ATTACHMENT0/1/…)；这里只把 color_ 挂到附件 0
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);

    // 创建单采样 2D 深度纹理
    // glTexImage2D：目标/mip0/内部格式32F深度/宽高/border0/数据格式/类型/nullptr仅分配不上传
    glGenTextures(1, &depth_);
    glBindTexture(GL_TEXTURE_2D, depth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);

    // 绑定本 FBO 绘制时，片元颜色输出到 COLOR_ATTACHMENT0（即上面的 color_）
    const GLenum drawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuf);
    // 0 = 默认帧缓冲（窗口），创建完后解绑，避免污染后续状态
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SingleSampleFbo::destroy() {
    if (color_) {
        glDeleteTextures(1, &color_);
        color_ = 0;
    }
    if (depth_) {
        glDeleteTextures(1, &depth_);
        depth_ = 0;
    }
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
}

void SingleSampleFbo::resize(int width, int height) {
    if (width == width_ && height == height_ && fbo_) {
        return;
    }
    create(width, height);
}

void SingleSampleFbo::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
}

void SingleSampleFbo::unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SingleSampleFbo::blitColorToDefault(int width, int height) const {
    // None 模式：离屏 color 直接 blit 到窗口；不做抗锯齿计算
    // 这里的 0 = 默认帧缓冲（屏幕后缓冲）
    blitColorTo(0, width, height);
}

void SingleSampleFbo::blitColorTo(GLuint drawFbo, int dstW, int dstH) const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo); // drawFbo==0 时写到窗口
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, dstW, dstH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // 恢复默认帧缓冲
}

// 【多重采样离线 FBO (MsaaFbo)】：
// 用于硬件 MSAA (Multisample Anti-Aliasing) 渲染
// 【状态机关键区别】：
// 1. 纹理类型使用 GL_TEXTURE_2D_MULTISAMPLE，而非普通的 GL_TEXTURE_2D。
// 2. 内存分配必须调用 glTexImage2DMultisample(..., samples, ...)，让硬件为每个像素分配 N 个 Subsample 点（如 4x MSAA）。
void MsaaFbo::create(int width, int height, int samples) {
    destroy();
    width_ = width;
    height_ = height;
    samples_ = samples;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // 关键点 1：创建多重采样颜色纹理 (GL_TEXTURE_2D_MULTISAMPLE)
    glGenTextures(1, &colorMs_);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, colorMs_);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples_, GL_RGBA8, width, height, GL_TRUE);
    // 同样只挂到颜色附件 0（colorMs_）
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, colorMs_, 0);

    // 关键点 2：创建多重采样深度纹理
    glGenTextures(1, &depthMs_);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, depthMs_);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples_, GL_DEPTH_COMPONENT32F, width, height, GL_TRUE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, depthMs_, 0);

    // 绘制输出到 COLOR_ATTACHMENT0（colorMs_）
    const GLenum drawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuf);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // 恢复默认帧缓冲
}

void MsaaFbo::destroy() {
    if (colorMs_) {
        glDeleteTextures(1, &colorMs_);
        colorMs_ = 0;
    }
    if (depthMs_) {
        glDeleteTextures(1, &depthMs_);
        depthMs_ = 0;
    }
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
}

void MsaaFbo::resize(int width, int height, int samples) {
    if (width == width_ && height == height_ && samples == samples_ && fbo_) {
        return;
    }
    create(width, height, samples);
}

void MsaaFbo::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
}

void MsaaFbo::unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void MsaaFbo::resolveColorTo(GLuint drawFbo, int dstW, int dstH) const {
    // 源为 MULTISAMPLE 时，glBlitFramebuffer 会触发硬件 Resolve
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo); // drawFbo==0 时 resolve 到窗口
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, dstW, dstH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // 恢复默认帧缓冲
}

