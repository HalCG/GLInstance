#include "RenderingPathsApp.hpp"

#include <glad/glad.h>
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <iostream>
#include <sstream>

RenderingPathsApp *RenderingPathsApp::s_instance_ = nullptr;

namespace
{
    const char *pathName(RenderPath path)
    {
        switch (path)
        {
        case RenderPath::Forward:
            return "Forward";
        case RenderPath::Deferred:
            return "Deferred";
        case RenderPath::ForwardPlus:
            return "Forward+";
        }
        return "Unknown";
    }
} // namespace

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
    cachedOverlayText_ = buildOverlayText();
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
    return true;
}

void RenderingPathsApp::markCameraDirty()
{
    // 相机、路径切换、光源 preset、GBuffer debug 等统一置 dirty
    cameraDirty_ = true;
}

// 组装 view + perspective（无 jitter）
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

    const bool measureGpu = !camera_.isDragging(); // 拖拽时跳过 GPU 计时与标题刷新
    perf_.forwardPass.setEnabled(measureGpu);
    perf_.geometryPass.setEnabled(measureGpu);
    perf_.lightingPass.setEnabled(measureGpu);
    perf_.cullPass.setEnabled(measureGpu);
    perf_.shadingPass.setEnabled(measureGpu);

    perf_.beginFrame();
    const FrameCamera camera = buildCamera();

    // 按 currentPath_ 分发到三条渲染管线（Forward / Deferred / Forward+）
    switch (currentPath_)
    {
    case RenderPath::Forward:
        forward_.render(scene_, lights_, camera, static_cast<int>(width_), static_cast<int>(height_), perf_);
        break;
    case RenderPath::Deferred:
        deferred_.render(scene_, lights_, camera, static_cast<int>(width_), static_cast<int>(height_), perf_,
                         showGBufferDebug_, enableHDR_);
        break;
    case RenderPath::ForwardPlus:
        forwardPlus_.render(scene_, lights_, camera, static_cast<int>(width_), static_cast<int>(height_), perf_);
        break;
    }

    perf_.endFrame();
    if (!camera_.isDragging() && ++titleUpdateCounter_ % 15 == 0)
    {
        cachedOverlayText_ = buildOverlayText();
        updateWindowTitle();
    }
}

void RenderingPathsApp::run()
{
    int frameCounter = 0;
    while (!glfwWindowShouldClose(window_))
    {
        // 空闲时 waitEvents 阻塞；有输入或 dirty 时 pollEvents 并渲染一帧
        const bool active = cameraDirty_ || camera_.isDragging();
        if (active)
        {
            glfwPollEvents();
        }
        else
        {
            glfwWaitEvents();
        }

        if (cameraDirty_)
        {
            renderFrame();
            glfwSwapBuffers(window_);
            cameraDirty_ = false;

            if (++frameCounter % 120 == 0)
            {
                const FrameStats stats = perf_.latest();
                std::cout << "path," << pathName(currentPath_) << ",lights," << lights_.activeCount() << ",fps,"
                          << stats.fps << ",frame_ms," << stats.totalFrameMs << ",forward_ms," << stats.forwardPassMs
                          << ",geom_ms," << stats.geometryPassMs << ",light_ms," << stats.lightingPassMs << ",cull_ms,"
                          << stats.cullPassMs << ",shade_ms," << stats.shadingPassMs << std::endl;
            }
        }
    }
}

void RenderingPathsApp::shutdown()
{
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

std::string RenderingPathsApp::buildOverlayText() const
{
    const FrameStats stats = perf_.latest();
    std::ostringstream oss;
    oss << pathName(currentPath_) << " | lights=" << lights_.activeCount()
        << " | objects=" << scene_.objectCount() << " | FPS=" << static_cast<int>(stats.fps) << " | frame="
        << stats.totalFrameMs << "ms";
    if (currentPath_ == RenderPath::Forward)
    {
        oss << " | forward=" << stats.forwardPassMs << "ms";
    }
    else if (currentPath_ == RenderPath::Deferred)
    {
        oss << " | geom=" << stats.geometryPassMs << "ms light=" << stats.lightingPassMs << "ms";
        if (showGBufferDebug_)
        {
            oss << " | GBufferDebug";
        }
    }
    else
    {
        oss << " | cull=" << stats.cullPassMs << "ms shade=" << stats.shadingPassMs << "ms";
    }
    oss << " | HDR:" << (enableHDR_ ? "ON" : "OFF");
    oss << " | VTK: LMB rotate, MMB pan, RMB dolly, wheel zoom, 1/2/3 path, [/] lights, G gbuffer, H hdr";
    return oss.str();
}

void RenderingPathsApp::updateWindowTitle()
{
    if (window_)
    {
        glfwSetWindowTitle(window_, cachedOverlayText_.c_str());
    }
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
        s_instance_->markCameraDirty();
    }
    glViewport(0, 0, width, height);
}

void RenderingPathsApp::mouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
{
    (void)mods;
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
        s_instance_->markCameraDirty();
    }
    else if (action == GLFW_RELEASE)
    {
        s_instance_->camera_.endDrag();
        s_instance_->cachedOverlayText_ = s_instance_->buildOverlayText();
        s_instance_->updateWindowTitle();
    }
}

void RenderingPathsApp::cursorPosCallback(GLFWwindow * /*window*/, double xpos, double ypos)
{
    if (!s_instance_)
    {
        return;
    }
    if (s_instance_->camera_.applyCursorDelta(xpos, ypos, s_instance_->width_, s_instance_->height_))
    {
        s_instance_->markCameraDirty();
    }
}

void RenderingPathsApp::scrollCallback(GLFWwindow * /*window*/, double /*xoffset*/, double yoffset)
{
    if (!s_instance_)
    {
        return;
    }

    s_instance_->camera_.applyScroll(yoffset);
    s_instance_->markCameraDirty();
}

void RenderingPathsApp::keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    if (action != GLFW_PRESS || !s_instance_)
    {
        return;
    }

    s_instance_->markCameraDirty();

    // 1/2/3 切换渲染路径；G 仅在 Deferred 下切换 GBuffer 可视化
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
