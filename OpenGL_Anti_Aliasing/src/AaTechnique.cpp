#include "AaTechnique.hpp"

#include "AATestScene.hpp"
#include "AppConfig.hpp"
#include "Framebuffer.hpp"
#include "PostProcess.hpp"
#include "SceneRenderer.hpp"
#include "TaaPass.hpp"

#include <glad/glad.h>

namespace {

// 单采样场景 Pass：None / FXAA / TAA 共用；三种模式只在这之后的收尾不同
void renderSceneTo(SingleSampleFbo &fbo, AATestScene &scene, SceneRenderer &renderer, const FrameCamera &camera,
                   int renderW, int renderH) {
    glViewport(0, 0, renderW, renderH);
    glDisable(GL_MULTISAMPLE); // 与 MSAA 路径互斥
    fbo.bind();
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderer.render(scene, camera); // 同一套场景几何
    fbo.unbind();
}

// MSAA 专用场景 Pass：开多重采样，画完后由调用方 Resolve
void renderSceneMsaa(MsaaFbo &fbo, AATestScene &scene, SceneRenderer &renderer, const FrameCamera &camera, int renderW,
                     int renderH) {
    glViewport(0, 0, renderW, renderH);
    glEnable(GL_MULTISAMPLE);
    fbo.bind();
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderer.render(scene, camera); // 与上面同一套场景，只是目标 FBO 不同
    fbo.unbind();
    glDisable(GL_MULTISAMPLE);
}

// ---------- None：无抗锯齿，场景直出到 resolve ----------
class NoneAa final : public IAaTechnique {
public:
    void onEnter(AaFrameContext &ctx) override {
        // 离开 / 未用 TAA：清 history，避免之后切回 TAA 混入旧帧
        if (ctx.taaPass) {
            ctx.taaPass->resetHistory();
        }
    }

    // 主循环每帧都会问 technique「本帧 jitter 多少」；None 答 (0,0)，投影不加偏
    glm::vec2 beginFrameJitter(AaFrameContext &) override { return glm::vec2(0.0f); }

    void renderToResolve(const FrameCamera &camera, AaFrameContext &ctx) override {
        // 本可直画 resolveFbo（无后处理）；经 sceneFbo 再 blit，是为与 FXAA/TAA 同一出口约定
        renderSceneTo(*ctx.sceneFbo, *ctx.scene, *ctx.sceneRenderer, camera, ctx.renderW, ctx.renderH);
        ctx.sceneFbo->blitColorTo(ctx.resolveFbo->fbo(), ctx.renderW, ctx.renderH);
    }
};

// ---------- MSAA：硬件多重采样 → resolve 到单采样 ----------
class MsaaAa final : public IAaTechnique {
public:
    void onEnter(AaFrameContext &ctx) override {
        if (ctx.taaPass) {
            ctx.taaPass->resetHistory();
        }
    }

    glm::vec2 beginFrameJitter(AaFrameContext &) override { return glm::vec2(0.0f); }

    void renderToResolve(const FrameCamera &camera, AaFrameContext &ctx) override {
        // MSAA 场景必须画在多重采样 FBO；Resolve（N→1）这一步省不掉
        renderSceneMsaa(*ctx.msaaFbo, *ctx.scene, *ctx.sceneRenderer, camera, ctx.renderW, ctx.renderH);
        // 可直 Resolve 到默认 FB，但会绕开「统一进 resolveFbo 再上屏」（含 renderScale 缩放）
        ctx.msaaFbo->resolveColorTo(ctx.resolveFbo->fbo(), ctx.renderW, ctx.renderH);
    }
};

// ---------- FXAA：先画单采样场景，再全屏后处理 ----------
class FxaaAa final : public IAaTechnique {
public:
    void onEnter(AaFrameContext &ctx) override {
        if (ctx.taaPass) {
            ctx.taaPass->resetHistory();
        }
    }

    glm::vec2 beginFrameJitter(AaFrameContext &) override { return glm::vec2(0.0f); }

    void renderToResolve(const FrameCamera &camera, AaFrameContext &ctx) override {
        renderSceneTo(*ctx.sceneFbo, *ctx.scene, *ctx.sceneRenderer, camera, ctx.renderW, ctx.renderH);
        // 以 resolveFbo 为绘制目标，对场景颜色做 FXAA
        ctx.resolveFbo->bind();
        glViewport(0, 0, ctx.renderW, ctx.renderH);
        glDisable(GL_DEPTH_TEST);
        ctx.postProcess->applyFxaa(ctx.sceneFbo->colorTexture());
        glEnable(GL_DEPTH_TEST);
        ctx.resolveFbo->unbind();
    }
};

// ---------- TAA：子像素抖动 + history 重投影混合 ----------
class TaaAa final : public IAaTechnique {
public:
    void onEnter(AaFrameContext &ctx) override {
        // 切到 TAA 时丢掉旧 history，避免与其它模式结果混在一起
        if (ctx.taaPass) {
            ctx.taaPass->resetHistory();
        }
    }

    glm::vec2 beginFrameJitter(AaFrameContext &ctx) override {
        // Halton 序列子像素偏移，由 buildCamera 写入 projection
        return ctx.taaPass->nextJitter(ctx.renderW, ctx.renderH);
    }

    void renderToResolve(const FrameCamera &camera, AaFrameContext &ctx) override {
        // 1) 用 jittered 相机画当前帧到 sceneFbo
        renderSceneTo(*ctx.sceneFbo, *ctx.scene, *ctx.sceneRenderer, camera, ctx.renderW, ctx.renderH);
        // 2) 与 history 混合；拖拽时加大 blend，减轻拖影
        const float blend =
            ctx.cameraDragging ? AppConfig::kTaaBlendDragging : AppConfig::kTaaBlendSettled;
        ctx.taaPass->apply(ctx.sceneFbo->colorTexture(), ctx.sceneFbo->depthTexture(), camera, ctx.prevViewProj,
                           ctx.hasPrevViewProj, blend);
        // 3) 把 TAA 输出 blit 到 resolveFbo
        ctx.resolveFbo->bind();
        glViewport(0, 0, ctx.renderW, ctx.renderH);
        glDisable(GL_DEPTH_TEST);
        ctx.postProcess->blitTexture(ctx.taaPass->outputTexture());
        glEnable(GL_DEPTH_TEST);
        ctx.resolveFbo->unbind();
    }
};

} // namespace

// 按 AAMode 堆上构造对应策略，包进 unique_ptr 返回（所有权交给调用方 technique_）
std::unique_ptr<IAaTechnique> createAaTechnique(AAMode mode) {
    switch (mode) {
    case AAMode::None:
        return std::make_unique<NoneAa>(); // 堆上构造 → unique_ptr，返回时转为 unique_ptr<IAaTechnique>
    case AAMode::MSAA:
        return std::make_unique<MsaaAa>();
    case AAMode::FXAA:
        return std::make_unique<FxaaAa>();
    case AAMode::TAA:
        return std::make_unique<TaaAa>();
    }
    return std::make_unique<NoneAa>(); // 兜底
}
