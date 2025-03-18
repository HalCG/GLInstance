#ifndef ANTI_ALIASING_APP_HPP
#define ANTI_ALIASING_APP_HPP

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

// AA 对比 Demo：左右 Wipe、内部分辨率、自动旋转，便于肉眼看出差异
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
    FrameCamera buildCamera(glm::vec2 jitterNdc) const;
    void renderFrame();
    void resizeTargets();
    void renderScenePass(const FrameCamera &camera, bool useMsaa);
    // 把当前 AA 模式结果写入 resolveFbo_（颜色）
    void renderAaToResolve(const FrameCamera &camera);
    // 无 AA 基线写入 baselineFbo_
    void renderBaselineToBaseline(const FrameCamera &camera);
    std::string buildOverlayText() const;
    void updateWindowTitle();
    int nextMsaaPreset(int delta) const;
    void markCameraDirty();
    void computeRenderSize(int &outW, int &outH) const;

    GLFWwindow *window_ = nullptr;
    unsigned int width_ = AppConfig::kInitialWidth;
    unsigned int height_ = AppConfig::kInitialHeight;
    int renderW_ = AppConfig::kInitialWidth;
    int renderH_ = AppConfig::kInitialHeight;

    AAMode currentMode_ = AAMode::MSAA; // 默认 MSAA，配合 Wipe 与左侧 None 同机位对比
    int msaaPresetIndex_ = AppConfig::kDefaultMsaaPresetIndex;
    VtkTrackballCamera camera_;
    bool cameraDirty_ = true;
    bool vsyncEnabled_ = false;
    bool wipeCompare_ = true;          // 左 None / 右当前 AA：同一相机、同一场景
    float wipeAmount_ = 0.5f;
    float renderScale_ = 1.0f;         // 默认原生分辨率看模型；可降到 75%/50% 放大锯齿
    bool autoOrbit_ = false;           // 默认关闭，保持用户自己的交互与机位
    double lastFrameTime_ = 0.0;
    glm::mat4 prevViewProj_ = glm::mat4(1.0f);
    bool hasPrevViewProj_ = false;
    int titleUpdateCounter_ = 0;
    std::string cachedOverlayText_;

    AATestScene scene_;
    SceneRenderer sceneRenderer_;
    SingleSampleFbo singleFbo_;
    SingleSampleFbo baselineFbo_;
    SingleSampleFbo resolveFbo_;
    MsaaFbo msaaFbo_;
    PostProcess postProcess_;
    TaaPass taaPass_;
    PerfStats perf_;

    static AntiAliasingApp *s_instance_;
};

#endif
