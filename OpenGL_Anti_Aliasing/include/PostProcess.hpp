#ifndef POST_PROCESS_HPP
#define POST_PROCESS_HPP

#include <glad/glad.h>

// 后处理工具：全屏 blit、FXAA、左右 Wipe 对比、MSAA resolve
class PostProcess {
public:
    bool init();
    void shutdown();
    void resize(int width, int height);

    void blitTexture(GLuint colorTexture) const;
    void applyFxaa(GLuint colorTexture) const;
    // 左 None / 右 AA，uWipe 为分割位置 [0,1]
    void applyWipe(GLuint leftTexture, GLuint rightTexture, float wipe) const;
    void resolveMsaaToScreen(GLuint msaaFbo, int width, int height) const;

private:
    void drawFullscreen() const;

    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;
    GLuint blitShader_ = 0;
    GLuint fxaaShader_ = 0;
    GLuint wipeShader_ = 0;
    mutable GLint blitLocInput_ = -1;
    mutable GLint fxaaLocInput_ = -1;
    mutable GLint fxaaLocTexelSize_ = -1;
    mutable GLint wipeLocLeft_ = -1;
    mutable GLint wipeLocRight_ = -1;
    mutable GLint wipeLocWipe_ = -1;
    mutable GLint wipeLocTexelSize_ = -1;
    int width_ = 0;
    int height_ = 0;
};

#endif
