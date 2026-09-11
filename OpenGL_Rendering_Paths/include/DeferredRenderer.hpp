#ifndef DEFERRED_RENDERER_HPP
#define DEFERRED_RENDERER_HPP

#include "LightManager.hpp"
#include "PerfStats.hpp"
#include "RenderTypes.hpp"
#include "Scene.hpp"
#include "Shader.hpp"

#include <memory>

// 延迟渲染：Geometry Pass 写 GBuffer → Lighting Pass 全屏逐光源计算
class DeferredRenderer {
public:
    bool init();    // geometry / deferred_lighting / gbuffer_debug shader + 全屏 quad
    void shutdown();
    void resize(int width, int height); // 按窗口尺寸重建 GBuffer 附件
    void render(const Scene &scene, LightManager &lights, const FrameCamera &camera, int width, int height,
                PerfStats &stats, bool showGBufferDebug, bool enableHDR = true); // showGBufferDebug 时跳过 lighting，直出 GBuffer 可视化


    GLuint albedoTexture() const { return gAlbedo_; }

private:
    void createGBuffer(int width, int height); // 分配 albedo/normal/material/depth 四附件 FBO
    void destroyGBuffer();
    void drawFullscreenQuad(); // lighting/debug pass 用

    std::unique_ptr<Shader> geometryShader_;
    std::unique_ptr<Shader> lightingShader_;
    std::unique_ptr<Shader> debugShader_;

    GLuint gBufferFbo_ = 0;
    GLuint gAlbedo_ = 0;
    GLuint gNormal_ = 0;
    GLuint gMaterial_ = 0;
    GLuint gDepth_ = 0;

    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;

    int width_ = 0;
    int height_ = 0;
};

#endif
