#include "RenderingPathsApp.hpp"

#include <glad/glad.h>
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ImGuiCnFont.hpp"

RenderingPathsApp *RenderingPathsApp::s_instance_ = nullptr;

bool RenderingPathsApp::init()
{
    s_instance_ = this;
    if (!initWindow())
    {
        return false;
    }
    if (!scene_.init())
    {
        return false;
    }

    lights_.init();
    lights_.regenerate(AppConfig::kLightCountPresets[lightPresetIndex_]);

    perf_.forwardPass.init();
    perf_.geometryPass.init();
    perf_.lightingPass.init();
    perf_.cullPass.init();
    perf_.shadingPass.init();

    if (!forward_.init() || !deferred_.init() || !forwardPlus_.init())
    {
        return false;
    }

    deferred_.resize(static_cast<int>(width_), static_cast<int>(height_));
    glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
    return true;
}

bool RenderingPathsApp::initWindow()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(static_cast<int>(width_), static_cast<int>(height_), AppConfig::kWindowTitle, nullptr,
                               nullptr);
    if (!window_)
    {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
    glfwSetKeyCallback(window_, keyCallback);
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetCursorPosCallback(window_, cursorPosCallback);
    glfwSetScrollCallback(window_, scrollCallback);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
    {
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glfwSwapInterval(vsyncEnabled_ ? 1 : 0);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGuiLoadChineseFont(18.0f);
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    return true;
}

FrameCamera RenderingPathsApp::buildCamera() const
{
    FrameCamera camera;
    camera.eye = camera_.eye();
    camera.view = camera_.viewMatrix();
    const float aspect =
        static_cast<float>(width_ > 0 ? width_ : 1) / static_cast<float>(height_ > 0 ? height_ : 1);
    camera.projection =
        glm::perspective(glm::radians(AppConfig::kFovDegrees), aspect, AppConfig::kNearPlane, AppConfig::kFarPlane);
    return camera;
}

void RenderingPathsApp::renderFrame()
{
    if (width_ == 0 || height_ == 0)
    {
        return;
    }

    const bool measureGpu = !camera_.isDragging();
    perf_.forwardPass.setEnabled(measureGpu);
    perf_.geometryPass.setEnabled(measureGpu);
    perf_.lightingPass.setEnabled(measureGpu);
    perf_.cullPass.setEnabled(measureGpu);
    perf_.shadingPass.setEnabled(measureGpu);

    perf_.beginFrame();
    const FrameCamera camera = buildCamera();

    switch (currentPath_)
    {
    case RenderPath::Forward:
        forward_.render(scene_, lights_, camera, static_cast<int>(width_), static_cast<int>(height_), perf_,
                        enableHDR_);
        break;
    case RenderPath::Deferred:
        deferred_.render(scene_, lights_, camera, static_cast<int>(width_), static_cast<int>(height_), perf_,
                         showGBufferDebug_, enableHDR_);
        break;
    case RenderPath::ForwardPlus:
        forwardPlus_.render(scene_, lights_, camera, static_cast<int>(width_), static_cast<int>(height_), perf_,
                            enableHDR_);
        break;
    }

    perf_.endFrame();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Rendering Paths Control Panel (渲染路径控制面板)")) {
        ImGui::Text("Render Resolution: %u x %u", width_, height_);
        ImGui::Separator();

        ImGui::Text("Rendering Path (渲染路径选择):");
        int pathIdx = static_cast<int>(currentPath_);
        if (ImGui::RadioButton("Forward (正向渲染)", pathIdx == 0)) {
            currentPath_ = RenderPath::Forward;
        }
        if (ImGui::RadioButton("Deferred (延迟渲染)", pathIdx == 1)) {
            currentPath_ = RenderPath::Deferred;
        }
        if (ImGui::RadioButton("Forward+ (Cluster/Tile 正向+)", pathIdx == 2)) {
            currentPath_ = RenderPath::ForwardPlus;
        }

        ImGui::Separator();
        ImGui::Text("Light Settings (光源设置):");
        int lightCount = lights_.activeCount();
        if (ImGui::SliderInt("Light Count (光源数量)", &lightCount, 1, AppConfig::kMaxLights)) {
            lights_.regenerate(lightCount);
        }
        if (ImGui::Button("32 Lights")) { lights_.regenerate(32); } ImGui::SameLine();
        if (ImGui::Button("64 Lights")) { lights_.regenerate(64); } ImGui::SameLine();
        if (ImGui::Button("128 Lights")) { lights_.regenerate(128); } ImGui::SameLine();
        if (ImGui::Button("256 Lights")) { lights_.regenerate(256); } ImGui::SameLine();
        if (ImGui::Button("512 Lights")) { lights_.regenerate(512); }

        bool stress = lights_.stressLighting();
        if (ImGui::Checkbox("Stress Lighting (大半径/强覆盖，便于看出灯数差异)", &stress)) {
            lights_.setStressLighting(stress);
        }
        ImGui::TextWrapped("提示：场景很轻时请看下方 Pass GPU ms，不要只看 FPS；"
                           "Forward+ 的 Cull 是 CPU 耗时。");

        ImGui::Separator();
        ImGui::Text("Debug & Display Options (调试选项):");
        if (ImGui::Checkbox("VSync (垂直同步)", &vsyncEnabled_)) {
            glfwSwapInterval(vsyncEnabled_ ? 1 : 0);
        }
        if (currentPath_ == RenderPath::Deferred) {
            ImGui::Checkbox("Show G-Buffer Debug (G键)", &showGBufferDebug_);
        }
        ImGui::Checkbox("Enable HDR Tone Mapping (H键)", &enableHDR_);

        ImGui::Separator();
        ImGui::Text("Performance Stats (看 ms，别只看 FPS):");
        const FrameStats stats = perf_.latest();
        ImGui::Text("1s Avg: %.0f FPS (%.2f ms) | Min: %.0f", stats.fps, stats.avgFrameMs, stats.minFps);
        ImGui::Text("Last frame CPU: %.3f ms", stats.totalFrameMs);
        if (currentPath_ == RenderPath::Forward) {
            ImGui::Text("Forward GPU: %.3f ms", stats.forwardPassMs);
        } else if (currentPath_ == RenderPath::Deferred) {
            ImGui::Text("Geometry GPU: %.3f ms", stats.geometryPassMs);
            ImGui::Text("Lighting GPU: %.3f ms  <-- 随灯数应上升", stats.lightingPassMs);
        } else {
            ImGui::Text("Cull CPU:     %.3f ms  (静止时缓存，转相机才重建)", stats.cullPassMs);
            ImGui::Text("Shading GPU:  %.3f ms", stats.shadingPassMs);
            ImGui::Text("Tile lights:  avg %.1f / max %d | saturated %.0f%%",
                        lights_.averageLightsPerTile(), AppConfig::kMaxLightsPerTile,
                        lights_.saturatedTileRatio() * 100.0f);
        }

        ImGui::Separator();
        ImGui::Text("Controls Help (交互指南):");
        ImGui::BulletText("LMB Drag: Rotate Camera (旋转相机)");
        ImGui::BulletText("MMB Drag: Pan Camera (平移相机)");
        ImGui::BulletText("RMB Drag / Wheel: Zoom (缩放)");
        ImGui::BulletText("Keys 1-3: Switch Path");
        ImGui::BulletText("[ / ]: Adjust Light Count");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void RenderingPathsApp::run()
{
    while (!glfwWindowShouldClose(window_))
    {
        glfwPollEvents();
        renderFrame();
        glfwSwapBuffers(window_);
    }
}

void RenderingPathsApp::shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    forwardPlus_.shutdown();
    deferred_.shutdown();
    forward_.shutdown();
    perf_.shadingPass.shutdown();
    perf_.cullPass.shutdown();
    perf_.lightingPass.shutdown();
    perf_.geometryPass.shutdown();
    perf_.forwardPass.shutdown();
    lights_.shutdown();
    scene_.shutdown();
    glfwTerminate();
    s_instance_ = nullptr;
}

int RenderingPathsApp::nextLightPreset(int delta) const
{
    int idx = lightPresetIndex_ + delta;
    if (idx < 0)
    {
        idx = 0;
    }
    if (idx >= AppConfig::kLightPresetCount)
    {
        idx = AppConfig::kLightPresetCount - 1;
    }
    return idx;
}

void RenderingPathsApp::framebufferSizeCallback(GLFWwindow * /*window*/, int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }
    if (s_instance_)
    {
        s_instance_->width_ = static_cast<unsigned int>(width);
        s_instance_->height_ = static_cast<unsigned int>(height);
        s_instance_->deferred_.resize(width, height);
    }
    glViewport(0, 0, width, height);
}

void RenderingPathsApp::mouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
{
    (void)mods;
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    if (!s_instance_)
    {
        return;
    }

    CameraDragMode mode = CameraDragMode::None;
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        mode = CameraDragMode::Rotate;
    }
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE)
    {
        mode = CameraDragMode::Pan;
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        mode = CameraDragMode::Dolly;
    }
    else
    {
        return;
    }

    if (action == GLFW_PRESS)
    {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window, &x, &y);
        s_instance_->camera_.beginDrag(mode, x, y);
    }
    else if (action == GLFW_RELEASE)
    {
        s_instance_->camera_.endDrag();
    }
}

void RenderingPathsApp::cursorPosCallback(GLFWwindow * /*window*/, double xpos, double ypos)
{
    if (!s_instance_)
    {
        return;
    }
    s_instance_->camera_.applyCursorDelta(xpos, ypos, s_instance_->width_, s_instance_->height_);
}

void RenderingPathsApp::scrollCallback(GLFWwindow * /*window*/, double /*xoffset*/, double yoffset)
{
    if (!s_instance_)
    {
        return;
    }

    s_instance_->camera_.applyScroll(yoffset);
}

void RenderingPathsApp::keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    if (action != GLFW_PRESS || !s_instance_)
    {
        return;
    }

    switch (key)
    {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, true);
        break;
    case GLFW_KEY_1:
    case GLFW_KEY_F1:
        s_instance_->currentPath_ = RenderPath::Forward;
        s_instance_->showGBufferDebug_ = false;
        break;
    case GLFW_KEY_2:
    case GLFW_KEY_F2:
        s_instance_->currentPath_ = RenderPath::Deferred;
        break;
    case GLFW_KEY_3:
    case GLFW_KEY_F3:
        s_instance_->currentPath_ = RenderPath::ForwardPlus;
        s_instance_->showGBufferDebug_ = false;
        break;
    case GLFW_KEY_LEFT_BRACKET:
    {
        const int idx = s_instance_->nextLightPreset(-1);
        if (idx != s_instance_->lightPresetIndex_)
        {
            s_instance_->lightPresetIndex_ = idx;
            s_instance_->lights_.regenerate(AppConfig::kLightCountPresets[idx]);
        }
        break;
    }
    case GLFW_KEY_RIGHT_BRACKET:
    {
        const int idx = s_instance_->nextLightPreset(1);
        if (idx != s_instance_->lightPresetIndex_)
        {
            s_instance_->lightPresetIndex_ = idx;
            s_instance_->lights_.regenerate(AppConfig::kLightCountPresets[idx]);
        }
        break;
    }
    case GLFW_KEY_G:
        if (s_instance_->currentPath_ == RenderPath::Deferred)
        {
            s_instance_->showGBufferDebug_ = !s_instance_->showGBufferDebug_;
        }
        break;
    case GLFW_KEY_H:
        s_instance_->enableHDR_ = !s_instance_->enableHDR_;
        break;
    default:
        break;
    }
}
