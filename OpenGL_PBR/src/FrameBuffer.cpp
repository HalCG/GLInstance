#include "FrameBuffer.hpp"
#include <iostream>

FrameBufferEx::FrameBufferEx(int width, int height)
    : width_(width), height_(height) {
}

FrameBufferEx::~FrameBufferEx() {
    cleanup();
}

bool FrameBufferEx::init(int width, int height) {
    cleanup();
    width_ = width;
    height_ = height;

    // 1. 生成并绑定离屏 FBO 句柄
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // 2. 创建 GL_RGBA16F 浮点颜色缓冲区贴图 (支持 HDR 高亮度无截断存储)
    glGenTextures(1, &colorTexture_);
    glBindTexture(GL_TEXTURE_2D, colorTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // 附加颜色贴图到 FBO GL_COLOR_ATTACHMENT0
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture_, 0);

    // 3. 创建 24 位深度渲染缓冲区 (Renderbuffer Object - RBO)
    glGenRenderbuffers(1, &rboDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, rboDepth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width_, height_);
    // 附加 RBO 到 FBO GL_DEPTH_ATTACHMENT
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rboDepth_);

    // 4. 检查帧缓冲区完整性
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[FrameBufferEx] Error: Framebuffer is not complete!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void FrameBufferEx::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

void FrameBufferEx::unbind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void FrameBufferEx::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == width_ && height == height_) return;
    init(width, height);
}

void FrameBufferEx::cleanup() {
    if (colorTexture_) {
        glDeleteTextures(1, &colorTexture_);
        colorTexture_ = 0;
    }
    if (rboDepth_) {
        glDeleteRenderbuffers(1, &rboDepth_);
        rboDepth_ = 0;
    }
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
}

