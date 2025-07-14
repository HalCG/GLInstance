#ifndef FRAME_BUFFER_HPP
#define FRAME_BUFFER_HPP

#include <glad/glad.h>

/**
 * @brief FrameBufferEx 离屏 HDR 帧缓冲区包装类
 * 
 * 包含 GL_RGBA16F 16位半精度浮点颜色附着与 GL_DEPTH_COMPONENT24 深度渲染缓冲区附着，
 * 用于在第一渲染 Pass 中收集超高动态范围的光照结果，以便后续进行 Tone Mapping 色调映射后处理。
 */
class FrameBufferEx {
public:
    FrameBufferEx(int width = 1280, int height = 720);
    ~FrameBufferEx();

    /**
     * @brief 初始化/重新分配帧缓冲区附着
     * @param width 帧缓冲区宽度
     * @param height 帧缓冲区高度
     * @return 成功且状态 Complete 返回 true
     */
    bool init(int width, int height);

    /**
     * @brief 绑定当前 FBO 并更新 Viewport
     */
    void bind() const;

    /**
     * @brief 解绑当前 FBO (恢复默认 0 号默认帧缓冲区/屏幕)
     */
    void unbind() const;

    /**
     * @brief 窗口尺寸变更时重构内部 Texture 与 RBO 存储
     */
    void resize(int width, int height);

    GLuint getColorTexture() const { return colorTexture_; }
    GLuint getFBO() const { return fbo_; }

private:
    void cleanup();

    GLuint fbo_ = 0;          // Framebuffer Object 句柄
    GLuint colorTexture_ = 0; // GL_RGBA16F 浮点颜色贴图句柄
    GLuint rboDepth_ = 0;     // GL_DEPTH_COMPONENT24 深度 Renderbuffer 句柄
    int width_ = 1280;
    int height_ = 720;
};

#endif // FRAME_BUFFER_HPP

