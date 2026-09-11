/**
 * @file StochasticTransparencyApp.cpp
 * @brief OIT Stochastic Transparency 随机透明度应用实现
 *
 * =================================================================================
 * 【OpenGL 状态机与随机透明度 (Stochastic Transparency) 架构核心概念差异说明】
 * 1. 原理差异：传统 OIT (Depth Peeling / Linked List) vs 随机透明度 (Stochastic Transparency):
 *    - Depth Peeling / Linked List: 基于精准几何与片段收集排序 (Back-to-Front / Front-to-Back)，
 *      代价是多 Pass 渲染开销大，或需要海量显存 SSBO 及复杂原子无锁同步。
 *    - Stochastic Transparency: **单 Pass 算法**！将 Alpha 透明度等价转换为**子像素采样覆盖概率 (Subpixel Coverage Probability)**。
 *      利用硬件 MSAA (如 16x MSAA) 的 `gl_SampleMask` 接口，在 Fragment Shader 中根据 Alpha 概率分布
 *      随机开启/关闭 16 个 MSAA 采样点中的若干个。最后利用 GPU 硬件 MSAA Resolve 自动做多采样平均，获得半透明视觉效果。
 *
 * 2. 核心 OpenGL 状态设置与意义:
 *    - `glEnable(GL_MULTISAMPLE)`: 开启 GPU 硬件多重采样抗锯齿 (MSAA)。
 *    - `glEnable(GL_SAMPLE_MASK)`: **最关键的 OpenGL 状态**！允许 Fragment Shader 内部对内建变量 `gl_SampleMask[]` 进行写入，
 *      从而以 bitmask 形式直接强行掩码剔除/保留指定 MSAA 采样点。
 *    - `glEnable(GL_DEPTH_TEST)` 与 `glDepthMask(GL_TRUE)`: 
 *      在 Stochastic Transparency 中，**所有透明物体与不透明物体均正常开启深度测试与深度写入**！
 *      每个 MSAA 采样点独立拥有 Depth Buffer 和 Color Buffer。由于深度写入正常开启，物体间的交叉、相交在子像素级别自然遮挡！
 *
 * 3. 优点与局限性:
 *    - 优点：完全无需 CPU/GPU 排序，单 Pass 渲染，对极复杂/自相交透明模型（如头发、烟雾、重叠面片）性能极佳。
 *    - 缺点：受限于 MSAA 采样数 (如 8x/16x)，遮挡层次较多或采样数不足时画面带有高频噪声/颗粒感 (Noise Artifacts)。
 * =================================================================================
 */

#include "StochasticTransparencyApp.hpp"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

StochasticTransparencyApp *StochasticTransparencyApp::s_instance_ = nullptr;

// ---------------------------------------------------------------------------
// GLFW 回调（通过静态实例转发到成员状态）
// ---------------------------------------------------------------------------
void StochasticTransparencyApp::framebufferSizeCallback(GLFWwindow * /*window*/,
                                                        int width,
                                                        int height) {
  if (s_instance_) {
    s_instance_->width_ = static_cast<unsigned int>(width);
    s_instance_->height_ = static_cast<unsigned int>(height);
    glViewport(0, 0, width, height);
  }
}

void StochasticTransparencyApp::processInput(GLFWwindow *window) {
  if (!s_instance_) {
    return;
  }
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, true);
  } else if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
    s_instance_->viewRotate_ -= 1.0f;
  } else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
    s_instance_->viewRotate_ += 1.0f;
  }
}

bool StochasticTransparencyApp::init() {
  s_instance_ = this;
  if (!initWindow()) {
    return false;
  }
  if (!initShaders()) {
    return false;
  }
  if (!initScene()) {
    return false;
  }
  if (!initBuffers()) {
    return false;
  }

  glViewport(0, 0, static_cast<GLsizei>(width_),
             static_cast<GLsizei>(height_));
  glClearColor(0.2f, 0.3f, 0.3f, 1.0f);

  // 1. 开启 MSAA 硬件多采样支持
  glEnable(GL_MULTISAMPLE);

  // 2. 开启采样点掩码写使能 (GL_SAMPLE_MASK)
  // 必须开启此标志，Fragment Shader 中对 gl_SampleMask[0] 的赋值才会对硬件多重采样覆盖测试生效
  glEnable(GL_SAMPLE_MASK);

  // 3. 正常开启深度测试与深度写入 (Stochastic Transparency 中所有透明物体均像不透明物体一样更新 Z-Buffer)
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);

  return true;
}

bool StochasticTransparencyApp::initWindow() {
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  
  // 请求 16 倍 MSAA 默认帧缓冲区 (Default Framebuffer Samples = 16)
  // Stochastic Transparency 的品质直接取决于 MSAA 采样点数
  glfwWindowHint(GLFW_SAMPLES, 16);

  window_ = glfwCreateWindow(static_cast<int>(width_),
                             static_cast<int>(height_),
                             AppConfig::kWindowTitle, nullptr, nullptr);
  if (!window_) {
    std::cout << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window_);
  glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
    std::cout << "Failed to initialize GLAD" << std::endl;
    return false;
  }

  // 查询当前 GPU 驱动支持的最大 MSAA 采样数 (如 8, 16, 32)
  GLint maxSamples;
  glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
  std::cout << "Max supported MSAA samples: " << maxSamples << std::endl;

  return true;
}

bool StochasticTransparencyApp::initShaders() {
  const std::string root = AppConfig::kResourceRoot;
  try {
    shaderQuad_ = std::make_unique<Shader>(
        (root + "quad.vert").c_str(),
        (root + "quad.frag").c_str());
    return true;
  } catch (const std::exception &e) {
    std::cout << "Failed to load shaders: " << e.what() << std::endl;
    return false;
  }
}

bool StochasticTransparencyApp::initScene() {
  try {
    modelQuad_ = std::make_unique<Model>("./resources/models/quad/quad.obj");
    modelSpot_ = std::make_unique<Model>("./resources/models/spot/spot.obj");

    texWindowR_ =
        std::make_unique<Texture>("./resources/models/quad/window-r.png");
    texWindowG_ =
        std::make_unique<Texture>("./resources/models/quad/window-g.png");
    texWindowB_ =
        std::make_unique<Texture>("./resources/models/quad/window-b.png");
    texSpot_ = std::make_unique<Texture>("./resources/models/spot/spot.png");

    return true;
  } catch (const std::exception &e) {
    std::cout << "Failed to load scene assets: " << e.what() << std::endl;
    return false;
  }
}

bool StochasticTransparencyApp::initBuffers() {
  return true;
}

void StochasticTransparencyApp::beginFrame() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
  glClearDepth(1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

glm::mat4 StochasticTransparencyApp::modelMatrix(const glm::vec3 &translation,
                                                 float scale) const {
  glm::mat4 model = glm::mat4(1.0f);
  model = glm::translate(model, translation);
  model = glm::scale(model, glm::vec3(scale, scale, scale));
  return model;
}

void StochasticTransparencyApp::renderScene() {
  shaderQuad_->use();

  // 计算相机矩阵 (支持方向键左右旋转 View)
  glm::mat4 cameraView = glm::lookAt(
      2.0f * glm::vec3(glm::sin(glm::radians(viewRotate_)), 0.0f,
                       glm::cos(glm::radians(viewRotate_))),
      glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

  glm::mat4 cameraProjection =
      glm::perspective(glm::radians(AppConfig::kFovDegrees),
                       (float)width_ / (float)height_,
                       AppConfig::kNearPlane, AppConfig::kFarPlane);

  shaderQuad_->setMat4("view", cameraView);
  shaderQuad_->setMat4("projection", cameraProjection);

  GLint maxSamples;
  glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);

  // frameID 传入 Shader 参与伪随机数 Seed 生成，随物体绘制自增避免采样掩码模式重复
  static int frameID = 0;
  int modelCnt = 4;

  // 1. 渲染不透明 spot 小牛
  shaderQuad_->setMat4("model", modelMatrix(glm::vec3(0.0f, 0.0f, 0.0f)));
  shaderQuad_->setInt("sampleCnt", maxSamples);
  shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
  modelSpot_->Draw(*shaderQuad_, 0,
                   {{"texture_diffuse", texSpot_->id}}, {},
                   GL_TRIANGLES, {false, false});

  // 2. 渲染 blue window 蓝色透明窗户 (单 Pass，深度正常测试与写入)
  shaderQuad_->setMat4("model",
                        modelMatrix(glm::vec3(0.3f, -0.1f, -0.8f), 0.5f));
  shaderQuad_->setInt("sampleCnt", maxSamples);
  shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
  modelQuad_->Draw(*shaderQuad_, 0,
                   {{"texture_diffuse", texWindowB_->id}}, {},
                   GL_TRIANGLES, {false, false});

  // 3. 渲染 green window 绿色透明窗户
  shaderQuad_->setMat4("model",
                        modelMatrix(glm::vec3(0.6f, 0.6f, -0.6f), 0.5f));
  shaderQuad_->setInt("sampleCnt", maxSamples);
  shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
  modelQuad_->Draw(*shaderQuad_, 0,
                   {{"texture_diffuse", texWindowG_->id}}, {},
                   GL_TRIANGLES, {false, false});

  // 4. 渲染 red window 红色透明窗户
  shaderQuad_->setMat4("model",
                        modelMatrix(glm::vec3(0.0f, 0.0f, 0.0f), 0.5f));
  shaderQuad_->setInt("sampleCnt", maxSamples);
  shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
  modelQuad_->Draw(*shaderQuad_, 0,
                   {{"texture_diffuse", texWindowR_->id}}, {},
                   GL_TRIANGLES, {false, false});
}

void StochasticTransparencyApp::endFrame() {
  glfwSwapBuffers(window_);
  glfwPollEvents();
}

void StochasticTransparencyApp::run() {
  while (!glfwWindowShouldClose(window_)) {
    processInput(window_);
    beginFrame();
    renderScene();
    endFrame();
  }
}

void StochasticTransparencyApp::shutdown() {
  shaderQuad_.reset();
  modelQuad_.reset();
  modelSpot_.reset();
  texWindowR_.reset();
  texWindowG_.reset();
  texWindowB_.reset();
  texSpot_.reset();

  if (window_) {
    glfwDestroyWindow(window_);
  }
  glfwTerminate();
}

