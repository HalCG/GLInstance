#include "AntiAliasingApp.hpp"

#include <glad/glad.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/common.hpp>
#include <glm/trigonometric.hpp>
#include <algorithm>
#include <iostream>
#include <sstream>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ImGuiCnFont.hpp"

AntiAliasingApp *AntiAliasingApp::s_instance_ = nullptr;

namespace {
const char *modeName(AAMode mode) {
    switch (mode) {
    case AAMode::None:
        return "None";
    case AAMode::MSAA:
        return "MSAA";
    case AAMode::FXAA:
        return "FXAA";
    case AAMode::TAA:
        return "TAA";
    }
    return "Unknown";
}

int clampMsaaSamples(int requested) {
    GLint maxSamples = 4;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    if (requested > maxSamples) {
        return maxSamples;
    }
    if (requested < 2) {
        return 1;
    }
    return requested;
}
} // namespace

bool AntiAliasingApp::init() {
    s_instance_ = this;
    if (!initWindow()) {
        return false;
    }
    if (!scene_.init()) {
        return false;
    }
    if (!sceneRenderer_.init() || !postProcess_.init() || !taaPass_.init()) {
        return false;
    }

    perf_.scenePass.init();
    perf_.postPass.init();
    lastFrameTime_ = glfwGetTime();

    resizeTargets();
    glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
    cachedOverlayText_ = buildOverlayText();
    return true;
}

bool AntiAliasingApp::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 0);

    window_ = glfwCreateWindow(static_cast<int>(width_), static_cast<int>(height_), AppConfig::kWindowTitle, nullptr,
                               nullptr);
    if (!window_) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
    glfwSetKeyCallback(window_, keyCallback);
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetCursorPosCallback(window_, cursorPosCallback);
    glfwSetScrollCallback(window_, scrollCallback);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
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

void AntiAliasingApp::computeRenderSize(int &outW, int &outH) const {
    outW = std::max(1, static_cast<int>(static_cast<float>(width_) * renderScale_ + 0.5f));
    outH = std::max(1, static_cast<int>(static_cast<float>(height_) * renderScale_ + 0.5f));
}

void AntiAliasingApp::resizeTargets() {
    if (width_ == 0 || height_ == 0) {
        return;
    }
    computeRenderSize(renderW_, renderH_);
    singleFbo_.resize(renderW_, renderH_);
    baselineFbo_.resize(renderW_, renderH_);
    resolveFbo_.resize(renderW_, renderH_);
    const int samples = clampMsaaSamples(AppConfig::kMsaaSamplesPresets[msaaPresetIndex_]);
    msaaFbo_.resize(renderW_, renderH_, samples);
    postProcess_.resize(renderW_, renderH_);
    taaPass_.resize(renderW_, renderH_);
    taaPass_.resetHistory();
    hasPrevViewProj_ = false;
}

void AntiAliasingApp::markCameraDirty() {
    cameraDirty_ = true;
}

FrameCamera AntiAliasingApp::buildCamera(glm::vec2 jitterNdc) const {
    FrameCamera camera;
    camera.eye = camera_.eye();
    camera.view = camera_.viewMatrix();
    const float aspect =
        static_cast<float>(width_ > 0 ? width_ : 1) / static_cast<float>(height_ > 0 ? height_ : 1);
    const glm::mat4 baseProj = glm::perspective(glm::radians(AppConfig::kFovDegrees), aspect, AppConfig::kNearPlane,
                                                AppConfig::kFarPlane);
    camera.unjitteredViewProjection = baseProj * camera.view;
    camera.invUnjitteredViewProjection = glm::inverse(camera.unjitteredViewProjection);

    camera.projection = baseProj;
    if (currentMode_ == AAMode::TAA) {
        camera.projection[2][0] += jitterNdc.x * 2.0f;
        camera.projection[2][1] += jitterNdc.y * 2.0f;
    }
    camera.viewProjection = camera.projection * camera.view;
    camera.invViewProjection = glm::inverse(camera.viewProjection);
    camera.jitterNdc = jitterNdc;
    return camera;
}

void AntiAliasingApp::renderScenePass(const FrameCamera &camera, bool useMsaa) {
    glViewport(0, 0, renderW_, renderH_);
    if (useMsaa) {
        glEnable(GL_MULTISAMPLE);
        msaaFbo_.bind();
    } else {
        glDisable(GL_MULTISAMPLE);
        singleFbo_.bind();
    }
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    sceneRenderer_.render(scene_, camera);
    if (useMsaa) {
        msaaFbo_.unbind();
    } else {
        singleFbo_.unbind();
    }
}

void AntiAliasingApp::renderBaselineToBaseline(const FrameCamera &camera) {
    // 强制无 jitter 的基线（用 unjittered 相机再画一遍更干净；此处 camera 已是无 jitter 时最简单）
    FrameCamera baselineCam = camera;
    baselineCam.projection = glm::perspective(
        glm::radians(AppConfig::kFovDegrees),
        static_cast<float>(width_ > 0 ? width_ : 1) / static_cast<float>(height_ > 0 ? height_ : 1),
        AppConfig::kNearPlane, AppConfig::kFarPlane);
    baselineCam.viewProjection = baselineCam.projection * baselineCam.view;
    baselineCam.jitterNdc = glm::vec2(0.0f);

    glViewport(0, 0, renderW_, renderH_);
    glDisable(GL_MULTISAMPLE);
    singleFbo_.bind();
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    sceneRenderer_.render(scene_, baselineCam);
    singleFbo_.unbind();
    singleFbo_.blitColorTo(baselineFbo_.fbo(), renderW_, renderH_);
}

void AntiAliasingApp::renderAaToResolve(const FrameCamera &camera) {
    switch (currentMode_) {
    case AAMode::None:
        renderScenePass(camera, false);
        singleFbo_.blitColorTo(resolveFbo_.fbo(), renderW_, renderH_);
        break;
    case AAMode::MSAA:
        renderScenePass(camera, true);
        msaaFbo_.resolveColorTo(resolveFbo_.fbo(), renderW_, renderH_);
        break;
    case AAMode::FXAA:
        renderScenePass(camera, false);
        resolveFbo_.bind();
        glViewport(0, 0, renderW_, renderH_);
        glDisable(GL_DEPTH_TEST);
        postProcess_.applyFxaa(singleFbo_.colorTexture());
        glEnable(GL_DEPTH_TEST);
        resolveFbo_.unbind();
        break;
    case AAMode::TAA: {
        renderScenePass(camera, false);
        const float blend =
            camera_.isDragging() ? AppConfig::kTaaBlendDragging : AppConfig::kTaaBlendSettled;
        taaPass_.apply(singleFbo_.colorTexture(), singleFbo_.depthTexture(), camera, prevViewProj_,
                       hasPrevViewProj_, blend);
        resolveFbo_.bind();
        glViewport(0, 0, renderW_, renderH_);
        glDisable(GL_DEPTH_TEST);
        postProcess_.blitTexture(taaPass_.outputTexture());
        glEnable(GL_DEPTH_TEST);
        resolveFbo_.unbind();
        break;
    }
    }
}

void AntiAliasingApp::renderFrame() {
    if (width_ == 0 || height_ == 0) {
        return;
    }

    const double now = glfwGetTime();
    const float dt = static_cast<float>(now - lastFrameTime_);
    lastFrameTime_ = now;
    if (autoOrbit_ && !camera_.isDragging()) {
        camera_.yaw += 18.0f * dt;
        markCameraDirty();
    }

    perf_.beginFrame();
    const bool measureGpu = !camera_.isDragging();
    perf_.scenePass.setEnabled(measureGpu);
    perf_.postPass.setEnabled(measureGpu);

    glm::vec2 jitter(0.0f);
    if (currentMode_ == AAMode::TAA) {
        jitter = taaPass_.nextJitter(renderW_, renderH_);
    }
    const FrameCamera camera = buildCamera(jitter);

    perf_.scenePass.begin();
    const bool doWipe = wipeCompare_ && currentMode_ != AAMode::None;
    if (doWipe) {
        renderBaselineToBaseline(camera);
        renderAaToResolve(camera);
    } else {
        renderAaToResolve(camera);
    }
    perf_.frameStats().scenePassMs = perf_.scenePass.endMs();

    perf_.postPass.begin();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    postProcess_.resize(static_cast<int>(width_), static_cast<int>(height_));
    if (doWipe) {
        postProcess_.applyWipe(baselineFbo_.colorTexture(), resolveFbo_.colorTexture(), wipeAmount_);
    } else {
        // 统一用硬件 blit 出屏，避免后处理程序异常时整屏黑
        resolveFbo_.blitColorToDefault(static_cast<int>(width_), static_cast<int>(height_));
    }
    postProcess_.resize(renderW_, renderH_);
    perf_.frameStats().postPassMs = perf_.postPass.endMs();

    prevViewProj_ = camera.unjitteredViewProjection;
    hasPrevViewProj_ = true;
    perf_.endFrame();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 560), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Anti-Aliasing Control Panel (抗锯齿控制面板)")) {
        ImGui::Text("Window: %u x %u | Internal: %d x %d", width_, height_, renderW_, renderH_);
        ImGui::TextWrapped("同一场景、同一相机、同一交互；切换模式或拖 Wipe 只改变 AA。");
        ImGui::Separator();

        ImGui::Text("Anti-Aliasing Mode (1-4):");
        int modeIdx = static_cast<int>(currentMode_);
        if (ImGui::RadioButton("None", modeIdx == 0)) {
            currentMode_ = AAMode::None;
            taaPass_.resetHistory();
            hasPrevViewProj_ = false;
            markCameraDirty();
        }
        if (ImGui::RadioButton("MSAA", modeIdx == 1)) {
            currentMode_ = AAMode::MSAA;
            taaPass_.resetHistory();
            hasPrevViewProj_ = false;
            resizeTargets();
            markCameraDirty();
        }
        if (ImGui::RadioButton("FXAA", modeIdx == 2)) {
            currentMode_ = AAMode::FXAA;
            taaPass_.resetHistory();
            hasPrevViewProj_ = false;
            markCameraDirty();
        }
        if (ImGui::RadioButton("TAA", modeIdx == 3)) {
            currentMode_ = AAMode::TAA;
            taaPass_.resetHistory();
            hasPrevViewProj_ = false;
            markCameraDirty();
        }

        if (currentMode_ == AAMode::MSAA) {
            ImGui::Text("MSAA Samples:");
            if (ImGui::Button("2x") && msaaPresetIndex_ != 0) {
                msaaPresetIndex_ = 0;
                resizeTargets();
                markCameraDirty();
            }
            ImGui::SameLine();
            if (ImGui::Button("4x") && msaaPresetIndex_ != 1) {
                msaaPresetIndex_ = 1;
                resizeTargets();
                markCameraDirty();
            }
            ImGui::SameLine();
            ImGui::Text("(%dx)", clampMsaaSamples(AppConfig::kMsaaSamplesPresets[msaaPresetIndex_]));
        }
        if (currentMode_ == AAMode::TAA) {
            if (ImGui::Button("Reset TAA History")) {
                taaPass_.resetHistory();
                hasPrevViewProj_ = false;
                markCameraDirty();
            }
        }

        ImGui::Separator();
        ImGui::Text("同机位对比:");
        if (ImGui::Checkbox("Wipe：左 None / 右 当前AA（同位置）", &wipeCompare_)) {
            markCameraDirty();
        }
        if (wipeCompare_) {
            ImGui::SliderFloat("分割线", &wipeAmount_, 0.05f, 0.95f, "%.2f");
            ImGui::TextWrapped("对准头盔轮廓或通风口边缘拖动，看锯齿差异。");
        }
        ImGui::Text("可选：降低内部分辨率放大锯齿");
        const char *scaleLabels[] = {"50%", "75%", "100% (默认)"};
        const float scales[] = {0.5f, 0.75f, 1.0f};
        int scaleIdx = (renderScale_ < 0.6f) ? 0 : (renderScale_ < 0.9f ? 1 : 2);
        if (ImGui::Combo("Render Scale", &scaleIdx, scaleLabels, 3)) {
            renderScale_ = scales[scaleIdx];
            resizeTargets();
            markCameraDirty();
        }
        ImGui::Checkbox("Auto Orbit (可选)", &autoOrbit_);
        if (ImGui::Checkbox("VSync", &vsyncEnabled_)) {
            glfwSwapInterval(vsyncEnabled_ ? 1 : 0);
            markCameraDirty();
        }

        ImGui::Separator();
        ImGui::Text("Performance:");
        const FrameStats stats = perf_.latest();
        ImGui::Text("1s Avg: %.0f FPS (%.2f ms) | Min: %.0f", stats.fps, stats.avgFrameMs, stats.minFps);
        ImGui::Text("Last frame: %.3f ms", stats.totalFrameMs);
        ImGui::Text("Scene: %.3f ms | Post: %.3f ms", stats.scenePassMs, stats.postPassMs);

        ImGui::Separator();
        ImGui::BulletText("LMB/MMB/RMB + Wheel: 相机（模式切换不改机位）");
        ImGui::BulletText("1-4: 切 AA   C: Wipe   [/]: MSAA");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (!camera_.isDragging() && ++titleUpdateCounter_ % 15 == 0) {
        cachedOverlayText_ = buildOverlayText();
        updateWindowTitle();
    }
}

void AntiAliasingApp::run() {
    int frameCounter = 0;
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        renderFrame();
        glfwSwapBuffers(window_);
        cameraDirty_ = false;

        if (++frameCounter % 120 == 0) {
            const FrameStats stats = perf_.latest();
            std::cout << "mode," << modeName(currentMode_) << ",scale," << renderScale_ << ",wipe," << wipeCompare_
                      << ",msaa," << (currentMode_ == AAMode::MSAA ? msaaFbo_.samples() : 0) << ",fps," << stats.fps
                      << ",scene_ms," << stats.scenePassMs << ",post_ms," << stats.postPassMs << std::endl;
        }
    }
}

void AntiAliasingApp::shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    taaPass_.shutdown();
    postProcess_.shutdown();
    sceneRenderer_.shutdown();
    msaaFbo_.destroy();
    resolveFbo_.destroy();
    baselineFbo_.destroy();
    singleFbo_.destroy();
    perf_.postPass.shutdown();
    perf_.scenePass.shutdown();
    scene_.shutdown();
    glfwTerminate();
    s_instance_ = nullptr;
}

int AntiAliasingApp::nextMsaaPreset(int delta) const {
    int idx = msaaPresetIndex_ + delta;
    if (idx < 0) {
        idx = 0;
    }
    if (idx >= AppConfig::kMsaaPresetCount) {
        idx = AppConfig::kMsaaPresetCount - 1;
    }
    return idx;
}

std::string AntiAliasingApp::buildOverlayText() const {
    const FrameStats stats = perf_.latest();
    std::ostringstream oss;
    oss << modeName(currentMode_);
    if (currentMode_ == AAMode::MSAA) {
        oss << " x" << msaaFbo_.samples();
    }
    oss << " | scale=" << static_cast<int>(renderScale_ * 100.0f) << "% | 1sFPS=" << static_cast<int>(stats.fps)
        << " | min=" << static_cast<int>(stats.minFps) << " | avg=" << stats.avgFrameMs << "ms"
        << " | scene=" << stats.scenePassMs << "ms post=" << stats.postPassMs << "ms";
    return oss.str();
}

void AntiAliasingApp::updateWindowTitle() {
    if (window_) {
        glfwSetWindowTitle(window_, cachedOverlayText_.c_str());
    }
}

void AntiAliasingApp::framebufferSizeCallback(GLFWwindow * /*window*/, int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (s_instance_) {
        s_instance_->width_ = static_cast<unsigned int>(width);
        s_instance_->height_ = static_cast<unsigned int>(height);
        s_instance_->resizeTargets();
        s_instance_->markCameraDirty();
    }
    glViewport(0, 0, width, height);
}

void AntiAliasingApp::mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
    (void)mods;
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    if (!s_instance_) {
        return;
    }

    CameraDragMode mode = CameraDragMode::None;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        mode = CameraDragMode::Rotate;
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        mode = CameraDragMode::Pan;
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        mode = CameraDragMode::Dolly;
    } else {
        return;
    }

    if (action == GLFW_PRESS) {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window, &x, &y);
        s_instance_->camera_.beginDrag(mode, x, y);
        s_instance_->markCameraDirty();
    } else if (action == GLFW_RELEASE) {
        s_instance_->camera_.endDrag();
        s_instance_->cachedOverlayText_ = s_instance_->buildOverlayText();
        s_instance_->updateWindowTitle();
    }
}

void AntiAliasingApp::cursorPosCallback(GLFWwindow * /*window*/, double xpos, double ypos) {
    if (!s_instance_) {
        return;
    }
    if (s_instance_->camera_.applyCursorDelta(xpos, ypos, s_instance_->width_, s_instance_->height_)) {
        s_instance_->markCameraDirty();
    }
}

void AntiAliasingApp::scrollCallback(GLFWwindow * /*window*/, double /*xoffset*/, double yoffset) {
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    if (!s_instance_) {
        return;
    }
    s_instance_->camera_.applyScroll(yoffset);
    s_instance_->markCameraDirty();
}

void AntiAliasingApp::keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    if (action != GLFW_PRESS || !s_instance_) {
        return;
    }
    if (ImGui::GetIO().WantCaptureKeyboard) {
        return;
    }

    s_instance_->markCameraDirty();

    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, true);
        break;
    case GLFW_KEY_1:
    case GLFW_KEY_F1:
        s_instance_->currentMode_ = AAMode::None;
        s_instance_->taaPass_.resetHistory();
        s_instance_->hasPrevViewProj_ = false;
        break;
    case GLFW_KEY_2:
    case GLFW_KEY_F2:
        s_instance_->currentMode_ = AAMode::MSAA;
        s_instance_->taaPass_.resetHistory();
        s_instance_->hasPrevViewProj_ = false;
        s_instance_->resizeTargets();
        break;
    case GLFW_KEY_3:
    case GLFW_KEY_F3:
        s_instance_->currentMode_ = AAMode::FXAA;
        s_instance_->taaPass_.resetHistory();
        s_instance_->hasPrevViewProj_ = false;
        break;
    case GLFW_KEY_4:
    case GLFW_KEY_F4:
        s_instance_->currentMode_ = AAMode::TAA;
        s_instance_->taaPass_.resetHistory();
        s_instance_->hasPrevViewProj_ = false;
        break;
    case GLFW_KEY_LEFT_BRACKET: {
        const int idx = s_instance_->nextMsaaPreset(-1);
        if (idx != s_instance_->msaaPresetIndex_) {
            s_instance_->msaaPresetIndex_ = idx;
            s_instance_->resizeTargets();
        }
        break;
    }
    case GLFW_KEY_RIGHT_BRACKET: {
        const int idx = s_instance_->nextMsaaPreset(1);
        if (idx != s_instance_->msaaPresetIndex_) {
            s_instance_->msaaPresetIndex_ = idx;
            s_instance_->resizeTargets();
        }
        break;
    }
    case GLFW_KEY_C:
        s_instance_->wipeCompare_ = !s_instance_->wipeCompare_;
        break;
    case GLFW_KEY_O:
        s_instance_->autoOrbit_ = !s_instance_->autoOrbit_;
        break;
    default:
        break;
    }
}
