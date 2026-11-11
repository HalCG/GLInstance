#ifndef AA_TECHNIQUE_HPP
#define AA_TECHNIQUE_HPP

#include "RenderTypes.hpp"

#include <memory>

class AATestScene;
class SceneRenderer;
class SingleSampleFbo;
class MsaaFbo;
class PostProcess;
class TaaPass;

// 一帧渲染上下文：App 持有资源，technique 只借用。按「共用 / 模式专用 / 帧状态」分组。
struct AaFrameContext {
    // --- 共用：sceneFbo 画场景，resolveFbo 收最终色再上屏 ---
    AATestScene *scene = nullptr;
    SceneRenderer *sceneRenderer = nullptr;
    SingleSampleFbo *sceneFbo = nullptr;
    SingleSampleFbo *resolveFbo = nullptr;
    int renderW = 0;
    int renderH = 0;

    // --- 模式专用：MSAA / FXAA·blit / TAA，用不到的忽略 ---
    MsaaFbo *msaaFbo = nullptr;
    PostProcess *postProcess = nullptr;
    TaaPass *taaPass = nullptr;

    // --- 帧状态（TAA）：重投影矩阵、是否有历史、拖拽时提高 blend ---
    glm::mat4 prevViewProj = glm::mat4(1.0f);
    bool hasPrevViewProj = false;
    bool cameraDragging = false;
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
