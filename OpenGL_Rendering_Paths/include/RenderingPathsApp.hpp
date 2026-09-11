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
    bool init();     // 初始化窗口、场景、光源与三种 renderer
    void run();      // 事件驱动主循环
    void shutdown();

    static void framebufferSizeCallback(GLFWwindow *window, int width, int height); // resize 时重建 Deferred GBuffer
    static void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods); // 1/2/3 切路径、G GBuffer debug
    static void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow *window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow *window, double xoffset, double yoffset);

private:
    bool initWindow();
    FrameCamera buildCamera() const;       // 轨道球 → view + perspective
    void renderFrame();                    // 按 currentPath_ 分发到对应 renderer
    void updateWindowTitle();
    std::string buildOverlayText() const;  // 标题栏：路径名、光源数、各 pass 耗时
    int nextLightPreset(int delta) const;  // [ ] 切换光源数量 preset
    void markCameraDirty();

    GLFWwindow *window_ = nullptr;
    unsigned int width_ = AppConfig::kInitialWidth;
    unsigned int height_ = AppConfig::kInitialHeight;

    RenderPath currentPath_ = RenderPath::Forward; // 1/2/3 切换 Forward / Deferred / Forward+
    bool showGBufferDebug_ = false;                // 仅 Deferred 路径下 G 键有效
    bool enableHDR_ = true;                        // H 键切换 HDR Tone Mapping & Gamma 校正
    VtkTrackballCamera camera_;

    bool cameraDirty_ = true;                    // 事件驱动渲染：为 true 才 render+swap
    int lightPresetIndex_ = 2;
    int titleUpdateCounter_ = 0;
    std::string cachedOverlayText_;

    Scene scene_;
    LightManager lights_;
    ForwardRenderer forward_;
    DeferredRenderer deferred_;
    ForwardPlusRenderer forwardPlus_;
    PerfStats perf_;

    static RenderingPathsApp *s_instance_;
};

#endif
