#ifndef RENDERING_PATHS_APP_HPP
#define RENDERING_PATHS_APP_HPP

#include "AppConfig.hpp"
#include "DeferredRenderer.hpp"
#include "ForwardPlusRenderer.hpp"
#include "ForwardRenderer.hpp"
#include "LightManager.hpp"
#include "PerfStats.hpp"
#include "RenderTypes.hpp"
#include "Scene.hpp"
#include "VtkTrackballCamera.hpp"

#include <GLFW/glfw3.h>

// 渲染路径对比 Demo：Forward / Deferred / Forward+ 三管线切换
class RenderingPathsApp {
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
    FrameCamera buildCamera() const;
    void renderFrame();
    int nextLightPreset(int delta) const;

    GLFWwindow *window_ = nullptr;
    unsigned int width_ = AppConfig::kInitialWidth;
    unsigned int height_ = AppConfig::kInitialHeight;

    RenderPath currentPath_ = RenderPath::Forward;
    bool showGBufferDebug_ = false;
    bool enableHDR_ = true;
    bool vsyncEnabled_ = false;
    VtkTrackballCamera camera_;
    int lightPresetIndex_ = 1; // 默认 64 灯

    Scene scene_;
    LightManager lights_;
    ForwardRenderer forward_;
    DeferredRenderer deferred_;
    ForwardPlusRenderer forwardPlus_;
    PerfStats perf_;

    static RenderingPathsApp *s_instance_;
};

#endif
