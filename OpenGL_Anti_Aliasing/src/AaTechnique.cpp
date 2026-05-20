#include "AaTechnique.hpp"

#include "AppConfig.hpp"

#include <glad/glad.h>

namespace {

// 单采样场景 Pass：画到 sceneFbo（None / FXAA / TAA / Wipe 基线共用）
void renderSceneTo(SingleSampleFbo &fbo, AATestScene &scene, SceneRenderer &renderer, const FrameCamera &camera,
                   int renderW, int renderH) {
    glViewport(0, 0, renderW, renderH);
    glDisable(GL_MULTISAMPLE);
    fbo.bind();
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderer.render(scene, camera);
    fbo.unbind();
}

// MSAA 场景 Pass：画到多重采样 FBO，之后需 resolve 到单采样目标
void renderSceneMsaa(MsaaFbo &fbo, AATestScene &scene, SceneRenderer &renderer, const FrameCamera &camera, int renderW,
                     int renderH) {
    glViewport(0, 0, renderW, renderH);
    glEnable(GL_MULTISAMPLE);
    fbo.bind();
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderer.render(scene, camera);
    fbo.unbind();
    glDisable(GL_MULTISAMPLE);
}

// ---------- None：无抗锯齿，场景直出到 resolve ----------
class NoneAa final : public IAaTechnique {
public:
    void onEnter(AaFrameContext &ctx) override {
        if (ctx.taaPass) {
            ctx.taaPass->resetHistory();
        }
    }

    glm::vec2 beginFrameJitter(AaFrameContext &) override { return glm::vec2(0.0f); }

    void renderToResolve(const FrameCamera &camera, AaFrameContext &ctx) override {
        renderSceneTo(*ctx.sceneFbo, *ctx.scene, *ctx.sceneRenderer, camera, ctx.renderW, ctx.renderH);
        // 颜色 blit 到 resolve，供统一上屏 / Wipe 右半边使用
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
        renderSceneMsaa(*ctx.msaaFbo, *ctx.scene, *ctx.sceneRenderer, camera, ctx.renderW, ctx.renderH);
        // glBlitFramebuffer 触发硬件 Resolve，写入 resolveFbo
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

std::unique_ptr<IAaTechnique> createAaTechnique(AAMode mode) {
    switch (mode) {
    case AAMode::None:
        return std::make_unique<NoneAa>();
    case AAMode::MSAA:
        return std::make_unique<MsaaAa>();
    case AAMode::FXAA:
        return std::make_unique<FxaaAa>();
    case AAMode::TAA:
        return std::make_unique<TaaAa>();
    }
    return std::make_unique<NoneAa>();
}
