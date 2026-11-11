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

// AA 对比 Demo：四种模式走 IAaTechnique；UI / 相机由 App 管理
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
    void drawUi();                          // ImGui 控制面板（与 AA 流水线分离）
    void resizeTargets();
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
    bool vsyncEnabled_ = true;  // 默认开：关 VSync + 持续出帧会打满 GPU，整机卡死难点 UI
    float renderScale_ = 1.0f;  // 内部分辨率比例，降低可放大锯齿
    bool autoOrbit_ = false;
    double lastFrameTime_ = 0.0;
    glm::mat4 prevViewProj_ = glm::mat4(1.0f); // 供 TAA 的上一帧无 jitter VP
    bool hasPrevViewProj_ = false;

    AATestScene scene_;
    SceneRenderer sceneRenderer_;
    SingleSampleFbo sceneFbo_;    // 场景单采样离屏（None/FXAA/TAA），与 AaFrameContext::sceneFbo 同名
    SingleSampleFbo resolveFbo_;  // 当前 AA 最终颜色
    MsaaFbo msaaFbo_;
    PostProcess postProcess_;
    TaaPass taaPass_;
    PerfStats perf_;

    static AntiAliasingApp *s_instance_;
};

#endif
