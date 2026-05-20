#ifndef ANTI_ALIASING_APP_HPP
#define ANTI_ALIASING_APP_HPP

#include "AaTechnique.hpp"
#include "AATestScene.hpp"
#include "AppConfig.hpp"
#include "Framebuffer.hpp"
#include "PerfStats.hpp"
#include "PostProcess.hpp"
#include "RenderTypes.hpp"
#include "SceneRenderer.hpp"
#include "TaaPass.hpp"
#include "VtkTrackballCamera.hpp"

#include <GLFW/glfw3.h>
#include <memory>

// AA 对比 Demo：四种模式走 IAaTechnique；Wipe / UI / 相机仍由 App 管理
class AntiAliasingApp {
public:
    bool init();
    void run();
    void shutdown();

    static void framebufferSizeCallback(GLFWwindow *window, int width, int height);
    static void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow *window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow *window, double xoffset, double yoffset);

private:
    bool initWindow();
    void setMode(AAMode mode);              // 切换策略：createAaTechnique + onEnter
    AaFrameContext makeFrameContext();      // 把 App 持有的资源塞进上下文给策略用
    FrameCamera buildCamera(glm::vec2 jitterNdc) const;
    void renderFrame();
    void resizeTargets();
    void renderBaselineToBaseline(const FrameCamera &camera); // Wipe 左侧：无 AA 基线
    int nextMsaaPreset(int delta) const;
    void computeRenderSize(int &outW, int &outH) const;

    GLFWwindow *window_ = nullptr;
    unsigned int width_ = AppConfig::kInitialWidth;
    unsigned int height_ = AppConfig::kInitialHeight;
    int renderW_ = AppConfig::kInitialWidth;  // 内部分辨率（可被 renderScale_ 缩小）
    int renderH_ = AppConfig::kInitialHeight;

    AAMode currentMode_ = AAMode::MSAA;
    std::unique_ptr<IAaTechnique> technique_; // 当前 AA 策略（None/MSAA/FXAA/TAA）
    int msaaPresetIndex_ = AppConfig::kDefaultMsaaPresetIndex;
    VtkTrackballCamera camera_;
    bool vsyncEnabled_ = false;
    bool wipeCompare_ = true;   // 左 None / 右当前 AA 同机位对比
    float wipeAmount_ = 0.5f;   // 分割线位置 [0,1]
    float renderScale_ = 1.0f;  // 内部分辨率比例，降低可放大锯齿
    bool autoOrbit_ = false;
    double lastFrameTime_ = 0.0;
    glm::mat4 prevViewProj_ = glm::mat4(1.0f); // 供 TAA 的上一帧无 jitter VP
    bool hasPrevViewProj_ = false;

    AATestScene scene_;
    SceneRenderer sceneRenderer_;
    SingleSampleFbo singleFbo_;   // 场景单采样离屏（None/FXAA/TAA/基线绘制）
    SingleSampleFbo baselineFbo_; // Wipe 左侧 None 结果
    SingleSampleFbo resolveFbo_;  // 当前 AA 最终颜色
    MsaaFbo msaaFbo_;
    PostProcess postProcess_;
    TaaPass taaPass_;
    PerfStats perf_;

    static AntiAliasingApp *s_instance_;
};

#endif
