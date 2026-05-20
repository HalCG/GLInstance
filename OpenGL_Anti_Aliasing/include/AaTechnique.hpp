#ifndef AA_TECHNIQUE_HPP
#define AA_TECHNIQUE_HPP

#include "AATestScene.hpp"
#include "Framebuffer.hpp"
#include "PostProcess.hpp"
#include "RenderTypes.hpp"
#include "SceneRenderer.hpp"
#include "TaaPass.hpp"

#include <memory>

// 一帧渲染时各 AA 技术共用的资源指针（资源所有权在 AntiAliasingApp，技术只借用）
struct AaFrameContext {
    AATestScene *scene = nullptr;
    SceneRenderer *sceneRenderer = nullptr;
    SingleSampleFbo *sceneFbo = nullptr;   // 单采样离屏：None / FXAA / TAA 画场景，Wipe 基线也复用
    SingleSampleFbo *resolveFbo = nullptr; // 当前 AA 模式的最终颜色（再 blit / Wipe 上屏）
    MsaaFbo *msaaFbo = nullptr;            // 仅 MSAA 使用
    PostProcess *postProcess = nullptr;    // FXAA / blit
    TaaPass *taaPass = nullptr;            // TAA history + jitter
    int renderW = 0;
    int renderH = 0;
    glm::mat4 prevViewProj = glm::mat4(1.0f); // 上一帧无 jitter 的 VP，TAA 重投影用
    bool hasPrevViewProj = false;             // 首帧 / 切模式后为 false，不做 history 混合
    bool cameraDragging = false;              // 拖拽时 TAA 提高 blend，减少拖影
};

// 策略接口：四种 AA 各一个实现；App 切模式只换 technique_，主循环不再写大 switch
class IAaTechnique {
public:
    virtual ~IAaTechnique() = default;

    // 进入该模式（切模式时调用）：清 TAA history，避免旧模式残留
    virtual void onEnter(AaFrameContext &ctx) = 0;
    // 本帧子像素抖动（NDC）；仅 TAA 非零，其它模式返回 (0,0)
    virtual glm::vec2 beginFrameJitter(AaFrameContext &ctx) = 0;
    // 画场景（+可选后处理），最终颜色写入 ctx.resolveFbo
    virtual void renderToResolve(const FrameCamera &camera, AaFrameContext &ctx) = 0;
};

// 按 AAMode 创建对应策略实例
std::unique_ptr<IAaTechnique> createAaTechnique(AAMode mode);

#endif
