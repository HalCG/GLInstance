#ifndef POST_PROCESS_HPP
#define POST_PROCESS_HPP

#include <glad/glad.h>

// 后处理工具：全屏 blit、FXAA、MSAA resolve 目标上的全屏绘制
class PostProcess {
public:
    bool init();
    void shutdown();
    void resize(int width, int height);

    void blitTexture(GLuint colorTexture) const;
    void applyFxaa(GLuint colorTexture) const;

private:
    void drawFullscreen() const;

    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;
    GLuint blitShader_ = 0;
    GLuint fxaaShader_ = 0;
    mutable GLint blitLocInput_ = -1;
    mutable GLint fxaaLocInput_ = -1;
    mutable GLint fxaaLocTexelSize_ = -1;
    int width_ = 0;
    int height_ = 0;
};

#endif
