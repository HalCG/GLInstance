#ifndef TAA_PASS_HPP
#define TAA_PASS_HPP

#include "RenderTypes.hpp"

#include <glad/glad.h>

// 时间抗锯齿后处理：Halton 抖动 + history 重投影混合
class TaaPass {
public:
    bool init();    // 编译 taa.frag、创建全屏 quad 与双缓冲 history 纹理
    void shutdown();
    void resize(int width, int height); // 重建 history 纹理，resetHistory 副作用
    void resetHistory();                // 切模式/resize 后调用，下一帧不做 history 混合

    glm::vec2 nextJitter(int width, int height); // 返回本帧子像素 jitter（NDC 单位）
    // prevViewProj / camera.invUnjittered* 必须是无 jitter 的 VP，避免亚像素重投影漂移
    void apply(GLuint currentColor, GLuint currentDepth, const FrameCamera &camera, const glm::mat4 &prevViewProj,
               bool hasHistory, float blendFactor);

    GLuint outputTexture() const { return history_[currentIndex_]; } // 混合结果，供 blit 到屏幕

private:
    GLuint history_[2] = {0, 0};
    GLuint fbo_ = 0;
    GLuint shader_ = 0;
    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;
    GLint locCurrentColor_ = -1;
    GLint locCurrentDepth_ = -1;
    GLint locHistoryColor_ = -1;
    GLint locInvViewProj_ = -1;
    GLint locPrevViewProj_ = -1;
    GLint locTexelSize_ = -1;
    GLint locBlendFactor_ = -1;
    GLint locHasHistory_ = -1;
    int currentIndex_ = 0;
    int frameIndex_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool validHistory_ = false;
};

#endif
