#include "AntiAliasingApp.hpp"

#include <glad/glad.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/common.hpp>
#include <glm/trigonometric.hpp>
#include <algorithm>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ImGuiCnFont.hpp"

AntiAliasingApp *AntiAliasingApp::s_instance_ = nullptr;

namespace {
// 将请求的 MSAA 采样数钳到驱动支持的 [1, GL_MAX_SAMPLES]
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
    setMode(currentMode_); // 创建默认策略（MSAA）并 onEnter
    glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
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

// 窗口尺寸 × renderScale_ → 离屏渲染分辨率；scale<1 可放大锯齿便于对比 AA
void AntiAliasingApp::computeRenderSize(int &outW, int &outH) const {
    outW = std::max(1, static_cast<int>(static_cast<float>(width_) * renderScale_ + 0.5f)); // +0.5 四舍五入
    outH = std::max(1, static_cast<int>(static_cast<float>(height_) * renderScale_ + 0.5f));
}

void AntiAliasingApp::resizeTargets() {
    if (width_ == 0 || height_ == 0) {
        return;
    }
    computeRenderSize(renderW_, renderH_);
    sceneFbo_.resize(renderW_, renderH_);
    resolveFbo_.resize(renderW_, renderH_);
    const int samples = clampMsaaSamples(AppConfig::kMsaaSamplesPresets[msaaPresetIndex_]);
    msaaFbo_.resize(renderW_, renderH_, samples);
    postProcess_.resize(renderW_, renderH_);
    taaPass_.resize(renderW_, renderH_);
    // 分辨率变化后 history 无效，下一帧按无历史处理
    taaPass_.resetHistory();
    hasPrevViewProj_ = false;
}

// 组装本帧 ctx（与 AaFrameContext 分组一致；所有权仍在 App）
AaFrameContext AntiAliasingApp::makeFrameContext() {
    AaFrameContext ctx;

    // 共用
    ctx.scene = &scene_;
    ctx.sceneRenderer = &sceneRenderer_;
    ctx.sceneFbo = &sceneFbo_;
    ctx.resolveFbo = &resolveFbo_;
    ctx.renderW = renderW_;
    ctx.renderH = renderH_;

    // 模式专用
    ctx.msaaFbo = &msaaFbo_;
    ctx.postProcess = &postProcess_;
    ctx.taaPass = &taaPass_;

    // 帧状态（TAA）
    ctx.prevViewProj = prevViewProj_;
    ctx.hasPrevViewProj = hasPrevViewProj_;
    ctx.cameraDragging = camera_.isDragging();

    return ctx;
}

// 切 AA 模式：换策略对象，并让新策略 onEnter（清 TAA history 等）
void AntiAliasingApp::setMode(AAMode mode) {
    currentMode_ = mode;
    technique_ = createAaTechnique(mode);
    AaFrameContext ctx = makeFrameContext();
    technique_->onEnter(ctx);
    hasPrevViewProj_ = false;
    if (mode == AAMode::MSAA) {
        // 切到 MSAA 时按当前采样预设重建多重采样 FBO
        resizeTargets();
    }
}

FrameCamera AntiAliasingApp::buildCamera(glm::vec2 jitterNdc) const {
    FrameCamera camera;
    camera.eye = camera_.eye();
    camera.view = camera_.viewMatrix();
    const float aspect =
        static_cast<float>(width_ > 0 ? width_ : 1) / static_cast<float>(height_ > 0 ? height_ : 1);
    const glm::mat4 baseProj = glm::perspective(glm::radians(AppConfig::kFovDegrees), aspect, AppConfig::kNearPlane,
                                                AppConfig::kFarPlane);
    // 无 jitter 的 VP：TAA 重投影 / 存 prevViewProj_ 必须用这套，否则 history 会漂
    camera.unjitteredViewProjection = baseProj * camera.view;
    camera.invUnjitteredViewProjection = glm::inverse(camera.unjitteredViewProjection);

    camera.projection = baseProj;
    if (currentMode_ == AAMode::TAA) {
        // 把 NDC 子像素偏移加到投影矩阵（仅影响本帧光栅化）
        camera.projection[2][0] += jitterNdc.x * 2.0f;
        camera.projection[2][1] += jitterNdc.y * 2.0f;
    }
    camera.viewProjection = camera.projection * camera.view;
    camera.jitterNdc = jitterNdc;
    return camera;
}

void AntiAliasingApp::renderFrame() {
    if (width_ == 0 || height_ == 0 || !technique_) {
        return;
    }

    const double now = glfwGetTime();
    const float dt = static_cast<float>(now - lastFrameTime_);
    lastFrameTime_ = now;
    if (autoOrbit_ && !camera_.isDragging()) {
        camera_.yaw += 18.0f * dt;
    }

    perf_.beginFrame();
    // 拖拽时关掉 GPU timer，减轻查询开销
    const bool measureGpu = !camera_.isDragging();
    perf_.scenePass.setEnabled(measureGpu);
    perf_.postPass.setEnabled(measureGpu);

    // --- Scene：策略产出 resolveFbo，再 blit 上屏 ---
    // 每帧重建 ctx：指针多半不变，但 prevViewProj / hasPrevViewProj / dragging 等帧状态要带到本帧
    AaFrameContext ctx = makeFrameContext();
    const glm::vec2 jitter = technique_->beginFrameJitter(ctx);
    const FrameCamera camera = buildCamera(jitter);

    perf_.scenePass.begin();
    technique_->renderToResolve(camera, ctx);
    perf_.frameStats().scenePassMs = perf_.scenePass.endMs();

    // Scene/FXAA/TAA 用 renderW_/renderH_；上屏按窗口分辨率 blit
    perf_.postPass.begin();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    resolveFbo_.blitColorToDefault(static_cast<int>(width_), static_cast<int>(height_));
    perf_.frameStats().postPassMs = perf_.postPass.endMs();

    // 供下一帧 TAA 重投影
    prevViewProj_ = camera.unjitteredViewProjection;
    hasPrevViewProj_ = true;
    perf_.endFrame();

    drawUi();
}

void AntiAliasingApp::drawUi() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 560), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Anti-Aliasing Control Panel (抗锯齿控制面板)")) {
        ImGui::Text("Window: %u x %u | Internal: %d x %d", width_, height_, renderW_, renderH_);
        ImGui::TextWrapped("同一场景、同一相机、同一交互；切换模式只改变 AA。");
        ImGui::Separator();

        ImGui::Text("Anti-Aliasing Mode (1-4):");
        int modeIdx = static_cast<int>(currentMode_);
        if (ImGui::RadioButton("None", modeIdx == 0)) {
            setMode(AAMode::None);
        }
        if (ImGui::RadioButton("MSAA", modeIdx == 1)) {
            setMode(AAMode::MSAA);
        }
        if (ImGui::RadioButton("FXAA", modeIdx == 2)) {
            setMode(AAMode::FXAA);
        }
        if (ImGui::RadioButton("TAA", modeIdx == 3)) {
            setMode(AAMode::TAA);
        }

        if (currentMode_ == AAMode::MSAA) {
            ImGui::Text("MSAA Samples:");
            if (ImGui::Button("2x") && msaaPresetIndex_ != 0) {
                msaaPresetIndex_ = 0;
                resizeTargets();
            }
            ImGui::SameLine();
            if (ImGui::Button("4x") && msaaPresetIndex_ != 1) {
                msaaPresetIndex_ = 1;
                resizeTargets();
            }
            ImGui::SameLine();
            ImGui::Text("(%dx)", clampMsaaSamples(AppConfig::kMsaaSamplesPresets[msaaPresetIndex_]));
        }
        if (currentMode_ == AAMode::TAA) {
            if (ImGui::Button("Reset TAA History")) {
                taaPass_.resetHistory();
                hasPrevViewProj_ = false;
            }
        }

        ImGui::Separator();
        ImGui::Text("可选：降低内部分辨率放大锯齿");
        const char *scaleLabels[] = {"50%", "75%", "100% (默认)"};
        const float scales[] = {0.5f, 0.75f, 1.0f};
        int scaleIdx = (renderScale_ < 0.6f) ? 0 : (renderScale_ < 0.9f ? 1 : 2);
        if (ImGui::Combo("Render Scale", &scaleIdx, scaleLabels, 3)) {
            renderScale_ = scales[scaleIdx];
            resizeTargets();
        }
        ImGui::Checkbox("Auto Orbit (可选)", &autoOrbit_);
        if (ImGui::Checkbox("VSync（建议开）", &vsyncEnabled_)) {
            glfwSwapInterval(vsyncEnabled_ ? 1 : 0);
        }
        if (!vsyncEnabled_) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                               "VSync 关闭时无帧率上限，易打满 GPU 导致整机卡死。");
        }

        ImGui::Separator();
        ImGui::Text("Performance:");
        const FrameStats stats = perf_.latest();
        ImGui::Text("1s Avg: %.0f FPS (%.2f ms) | Min: %.0f", stats.fps, stats.avgFrameMs, stats.minFps);
        ImGui::Text("Last frame: %.3f ms", stats.totalFrameMs);
        ImGui::Text("Scene: %.3f ms | Post: %.3f ms", stats.scenePassMs, stats.postPassMs);

        ImGui::Separator();
        ImGui::BulletText("LMB/MMB/RMB + Wheel: 相机（模式切换不改机位）");
        ImGui::BulletText("1-4: 切 AA   [/]: MSAA");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void AntiAliasingApp::run() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        renderFrame();
        glfwSwapBuffers(window_);
    }
}

// 释放顺序大致与 init 相反：先 UI / 策略，再 Pass·FBO，最后场景与 GLFW
void AntiAliasingApp::shutdown() {
    // UI
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    // AA 策略与 Pass（先丢 technique，避免悬空借用）
    technique_.reset();
    taaPass_.shutdown();
    postProcess_.shutdown();
    sceneRenderer_.shutdown();

    // 离屏 FBO
    msaaFbo_.destroy();
    resolveFbo_.destroy();
    sceneFbo_.destroy();

    // 计时 / 场景 / 窗口
    perf_.postPass.shutdown();
    perf_.scenePass.shutdown();
    scene_.shutdown();
    glfwTerminate();
    s_instance_ = nullptr;
}

// 键盘 [ / ] 切换 MSAA 档位（2x↔4x）：只算夹紧后的下标，不改状态
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

void AntiAliasingApp::framebufferSizeCallback(GLFWwindow * /*window*/, int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (s_instance_) {
        s_instance_->width_ = static_cast<unsigned int>(width);
        s_instance_->height_ = static_cast<unsigned int>(height);
        s_instance_->resizeTargets();
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
    } else if (action == GLFW_RELEASE) {
        s_instance_->camera_.endDrag();
    }
}

void AntiAliasingApp::cursorPosCallback(GLFWwindow * /*window*/, double xpos, double ypos) {
    if (!s_instance_) {
        return;
    }
    if (s_instance_->camera_.applyCursorDelta(xpos, ypos, s_instance_->width_, s_instance_->height_)) {
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


    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, true);
        break;
    case GLFW_KEY_1:
    case GLFW_KEY_F1:
        s_instance_->setMode(AAMode::None);
        break;
    case GLFW_KEY_2:
    case GLFW_KEY_F2:
        s_instance_->setMode(AAMode::MSAA);
        break;
    case GLFW_KEY_3:
    case GLFW_KEY_F3:
        s_instance_->setMode(AAMode::FXAA);
        break;
    case GLFW_KEY_4:
    case GLFW_KEY_F4:
        s_instance_->setMode(AAMode::TAA);
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
    case GLFW_KEY_O:
        s_instance_->autoOrbit_ = !s_instance_->autoOrbit_;
        break;
    default:
        break;
    }
}
