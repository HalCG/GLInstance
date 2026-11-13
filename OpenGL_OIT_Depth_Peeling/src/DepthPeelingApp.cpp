#include "DepthPeelingApp.hpp"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ImGuiCnFont.hpp"

DepthPeelingApp *DepthPeelingApp::s_instance_ = nullptr;

// ---------------------------------------------------------------------------
// GLFW 回调（通过静态实例转发到成员状态）
// ---------------------------------------------------------------------------
void DepthPeelingApp::framebufferSizeCallback(GLFWwindow * /*window*/,
                                              int width, int height)
{
  if (width <= 0 || height <= 0) {
    return;
  }
  if (s_instance_)
  {
    s_instance_->width_ = static_cast<unsigned int>(width);
    s_instance_->height_ = static_cast<unsigned int>(height);
    s_instance_->camera_.setAspectFromViewport(
        s_instance_->width_, s_instance_->height_);
    // 离屏 peel/accum FBO 按新分辨率重建，避免拉伸/深度 UV 错位
    s_instance_->initFramebuffers();
    glViewport(0, 0, width, height);
  }
}

void DepthPeelingApp::processInput(GLFWwindow *window)
{
  if (!s_instance_)
  {
    return;
  }
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
  {
    glfwSetWindowShouldClose(window, true);
  }
  else if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
  {
    s_instance_->camera_.orbitAngleDeg += 1.0f;
  }
  else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
  {
    s_instance_->camera_.orbitAngleDeg -= 1.0f;
  }
}

bool DepthPeelingApp::init()
{
  s_instance_ = this;

  if (!initWindow())
  {
    return false;
  }
  if (!initShaders())
  {
    return false;
  }
  if (!initScene())
  {
    return false;
  }
  if (!initFramebuffers())
  {
    return false;
  }

  // 生成 1 个硬件查询对象 (Query Object) 句柄，用于 Depth Peeling 过程中的硬件遮挡查询 (Occlusion Query: GL_SAMPLES_PASSED)
  // 核心作用：在 peelAndBlend() 中统计每层 Peel Pass 实际被绘制的像素采样数；当采样数降为 0 时提前终止循环 (Early-Out)
  glGenQueries(1, &queryId_);
  glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  return true;
}

bool DepthPeelingApp::initWindow()
{
  if (!glfwInit())
  {
    std::cout << "Failed to initialize GLFW" << std::endl;
    return false;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  // Depth Peeling 画到非 MSAA 离屏 FBO，不请求窗口 MSAA，避免误导

  window_ = glfwCreateWindow(static_cast<int>(width_),
                             static_cast<int>(height_),
                             AppConfig::kWindowTitle, nullptr, nullptr);
  if (!window_)
  {
    std::cout << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window_);
  glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
  {
    std::cout << "Failed to initialize GLAD" << std::endl;
    glfwDestroyWindow(window_);
    window_ = nullptr;
    glfwTerminate();
    return false;
  }

  glEnable(GL_DEPTH_TEST);
  // 使用 GL_LEQUAL (<=)：在 depth_peeling_render.frag 中先通过 `if (gl_FragCoord.z <= frontDepth) discard;` 手动丢弃上一层及之前的所有旧片元；
  // 过滤后的片元进硬件 Z-Test 时，使用 GL_LEQUAL 能确保浮点深度相等或最近的当前层片元顺利通过测试并写入，防止 Z-Fighting 丢片。
  glDepthFunc(GL_LEQUAL);

  camera_.orbitAngleDeg = AppConfig::kInitialOrbitAngle;
  camera_.setAspectFromViewport(width_, height_);

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

bool DepthPeelingApp::initShaders()
{
  const std::string root = AppConfig::kResourceRoot;
  shaderPeel_ = std::make_unique<Shader>(
      (root + "depth_peeling_render.vert").c_str(),
      (root + "depth_peeling_render.frag").c_str());
  shaderBlend_ = std::make_unique<Shader>(
      (root + "depth_peeling_blend.vert").c_str(),
      (root + "depth_peeling_blend.frag").c_str());
  shaderFinal_ = std::make_unique<Shader>(
      (root + "depth_peeling_final.vert").c_str(),
      (root + "depth_peeling_final.frag").c_str());
  return true;
}

bool DepthPeelingApp::initScene()
{
  const std::string root = AppConfig::kResourceRoot;
  modelQuad_ = std::make_unique<Model>(root + "models/quad/quad.obj");
  modelSpot_ = std::make_unique<Model>(root + "models/spot/spot.obj");

  texWindowR_ = std::make_unique<Texture>(root + "models/quad/window-r.png");
  texWindowG_ = std::make_unique<Texture>(root + "models/quad/window-g.png");
  texWindowB_ = std::make_unique<Texture>(root + "models/quad/window-b.png");
  texSpot_ = std::make_unique<Texture>(root + "models/spot/spot.png");
  return true;
}

bool DepthPeelingApp::initFramebuffers()
{
  const int w = static_cast<int>(width_);
  const int h = static_cast<int>(height_);

  fboAccum_.create(w, h, "Accumulation (FBO_0)");
  fboPeel_.create(w, h, "Peel layer (FBO_1)");
  return true;
}

void DepthPeelingApp::run()
{
  if (!window_)
  {
    return;
  }

  while (!glfwWindowShouldClose(window_))
  {
    beginFrame();
    if (autoRotate_) {
      camera_.orbitAngleDeg += 0.3f;
    }

    initPeelBuffers();
    peelAndBlend();
    compositeToScreen();

    // ImGui 面板
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 360), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Depth Peeling OIT Control Panel (深度剥离控制面板)")) {
      ImGui::Text("Render Resolution: %u x %u", width_, height_);
      ImGui::Separator();

      ImGui::Text("OIT Parameters (算法参数):");
      ImGui::SliderInt("Max Peeling Layers (最大剥离层数)", &maxPeelLayers_, 1, 16);

      ImGui::Separator();
      ImGui::Text("Scene Controls (场景与视角):");
      ImGui::Checkbox("Auto Rotate Scene (自动旋转)", &autoRotate_);
      ImGui::SliderFloat("Orbit Angle (旋转角度)", &camera_.orbitAngleDeg, 0.0f, 360.0f);
      ImGui::ColorEdit3("Background Color (背景颜色)", &bgColor_.x);

      ImGui::Separator();
      ImGui::Text("Performance Stats (性能统计):");
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
      ImGui::Text("Frame Time: %.2f ms", 1000.0f / ImGui::GetIO().Framerate);

      ImGui::Separator();
      ImGui::Text("Controls Help (交互指南):");
      ImGui::BulletText("Left/Right Arrow Keys: Rotate scene");
      ImGui::BulletText("ESC: Exit");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    endFrame();
  }
}

void DepthPeelingApp::shutdown()
{
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  if (queryId_ != 0)
  {
    glDeleteQueries(1, &queryId_);
    queryId_ = 0;
  }
  fboAccum_.destroy();
  fboPeel_.destroy();
  if (window_)
  {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  glfwTerminate();
  s_instance_ = nullptr;
}

void DepthPeelingApp::beginFrame()
{
  processInput(window_);
}

// 每一帧开始时初始化剥离与累积缓冲区
// 【状态机逻辑】：
// 1. 清除 fboAccum_ 和 fboPeel_ 的颜色与深度缓冲区。注意 depth 初值设为 0.0，供第 0 层剥离进行 > 0 比较。
// 2. 开启深度测试 `glEnable(GL_DEPTH_TEST)`，测试函数设为 `GL_LESS`。
// 3. 开启深度写入 `glDepthMask(GL_TRUE)`，以便在 Peel Pass 记录当前层的最小深度。
void DepthPeelingApp::initPeelBuffers()
{
  // FBO_0：累积颜色 + 深度
  fboAccum_.bindColorDepth(fboAccum_.color.id, fboAccum_.depth.id);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  // 将深度缓冲区初始值设为 0.0f（而非默认 1.0f）：
  // 原因：第 0 层剥离 Pass 开始时，Shader 需要用 `if (z <= frontDepth) discard;` 判断。
  // 将初值设为 0.0f 才能保证第一层绘制时所有正常片元 (z > 0.0f) 不会被错误丢弃，成功剥离出最前端的第 1 层片元。
  glClearDepth(0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // FBO_1：当前剥离层
  fboPeel_.bindColorDepth(fboPeel_.color.id, fboPeel_.depth.id);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClearDepth(0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glEnable(GL_DEPTH_TEST); // 开启深度测试：只有深度值小于（比当前 Depth 缓冲区旧值更靠前/更近）的片元才能留存
  glDepthFunc(GL_LESS);    // 深度比较规则：仅保留距离相机更近 (gl_FragCoord.z 更小) 的片元
  glDepthMask(GL_TRUE);    // 开启深度写入：允许 GPU 硬件自动更新当前剥离层的 outputDepth 纹理

  inputDepthIndex_ = 0;
  outputDepthIndex_ = 1;
}

// 获取当前乒乓双缓冲中的深度纹理 ID (索引 0 为 fboAccum_.depth，索引 1 为 fboPeel_.depth)
// 【核心设计说明】：
// 这两张深度纹理纯粹是 Depth Peeling 算法内部的“中间过程辅助缓冲区 (Intermediate Helper Buffer)”。
// - 在剥离循环中：一个负责提供 Shader 读取上一层 reference depth（只读），另一个挂载到 FBO 接收 GPU 硬件写入当前层深度（只写）。
// - 剥离结束后：最终 compositeToScreen() 输出屏幕时只使用 fboAccum_.color，深度纹理无任何后续渲染副作用。
GLuint DepthPeelingApp::depthTexture(int index) const
{
  return index ? fboPeel_.depth.id : fboAccum_.depth.id;
}

glm::mat4 DepthPeelingApp::modelMatrix(const glm::vec3 &translation,
                                       float scale) const
{
  glm::mat4 model(1.0f);
  model = glm::translate(model, translation);
  model = glm::rotate(model, glm::radians(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
  model = glm::rotate(model, glm::radians(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  model = glm::rotate(model, glm::radians(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  return glm::scale(model, glm::vec3(scale));
}

void DepthPeelingApp::drawSceneLayer(Shader &shader, GLuint targetFbo,
                                     int inputDepthIndex,
                                     int outputDepthIndex)
{
  const glm::mat4 view = camera_.view();
  const glm::mat4 projection = camera_.projection();
  const GLuint inputDepth = depthTexture(inputDepthIndex);

  shader.use();
  // 与轨道 view 同一眼点，避免相机转动后高光仍按固定位置算
  shader.setVec3("cameraPos", camera_.eye());
  shader.setVec3("lightPos", lightPos_);
  shader.setVec3("k", lightCoeffs_);
  shader.setVec2("u_ScreenSize",
                 glm::vec2(static_cast<float>(width_),
                           static_cast<float>(height_)));

  // Peel：带 MVP 的场景绘制（Blend 见 blendPeelLayerToAccum）
  auto draw = [&](Model &model, const glm::vec3 &pos, GLuint diffuseId)
  {
    shader.setMat4("model", modelMatrix(pos));
    shader.setMat4("view", view);
    shader.setMat4("projection", projection);
    model.Draw(shader, targetFbo,
               {{"texture_diffuse", diffuseId},
                {"texture_depth", inputDepth}},
               {}, GL_TRIANGLES, {false, false});
  };

  draw(*modelSpot_, glm::vec3(0.0f, 0.0f, 0.0f), texSpot_->id);
  draw(*modelQuad_, glm::vec3(-0.5f, 0.0f, 0.8f), texWindowR_->id);
  draw(*modelQuad_, glm::vec3(0.2f, -0.5f, -1.0f), texWindowG_->id);
  draw(*modelQuad_, glm::vec3(0.2f, 0.0f, -0.5f), texWindowB_->id);

  (void)outputDepthIndex;
}

// 阻塞等待 GPU 完成遮挡查询，获取当前剥离层通过深度测试的像素/采样点总数 (sampleCount)
// 注：之所以这里能确定返回的是采样点数量，是因为在调用此函数前执行了 `glBeginQuery(GL_SAMPLES_PASSED, queryId_)`，
// 显式将 queryId_ 的统计目标指定为了 GL_SAMPLES_PASSED（通过 Z-Test 的采样点数）。
GLuint DepthPeelingApp::waitSampleCount()
{
  GLint available = 0;
  // 1. 轮询等待 GPU 将 Occlusion Query 统计结果写回 (异步管道同步)
  while (!available)
  {
    glGetQueryObjectiv(queryId_, GL_QUERY_RESULT_AVAILABLE, &available);
  }
  // 2. 提取 GL_SAMPLES_PASSED 统计到的片元采样点数量 (sampleCount)
  GLuint sampleCount = 0;
  glGetQueryObjectuiv(queryId_, GL_QUERY_RESULT, &sampleCount);
  return sampleCount;
}

// 【Depth Peeling 核心循环：剥离 (Peel) 与 累积 (Blend)】
// 算法逻辑：
// 1. 乒乓剥离 (Peel Phase)：
//    - 将目标 FBO 的深度附件设为 depthTexture(outputDepthIndex)。
//    - 传入上一层的深度纹理 inputDepth (depthTexture(inputDepthIndex))。
//    - 在 shaderPeels (depth_peeling_render.frag) 中，若 gl_FragCoord.z <= frontDepth 则 discard（剥离掉上一层及更浅的片元）。
//    - 剩下的片元经 OpenGL 硬件 Z-Test (GL_LESS) 选出最靠近上一层的“下一层”深度并写入 outputDepth。
// 2. 遮挡查询提前终止 (Occlusion Query Early-Out)：
//    - 使用 `glBeginQuery(GL_SAMPLES_PASSED, queryId_)` 统计通过当前 Peel 测试的片元数量。
//    - 如果 sampleCount == 0，说明场景中所有像素的透明重叠层已全部剥离完毕，循环直接 `break` 提前退出！
// 3. 混合累积 (Blend Phase)：
//    - `glEnable(GL_BLEND)` + `glDisable(GL_DEPTH_TEST)` + `glDepthMask(GL_FALSE)`。
//    - 使用 Front-to-Back（从近到远 / 浅→深，不是从深到浅）混合公式
//      (`GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA`) 将当前剥离层融进 fboAccum_：
//      先混入离眼睛最近的一层（权重最大、受后续层影响最小），再混入更远的层（乘剩余透射 A_dst）。
// 4. 乒乓交换 (Ping-Pong Swap)：
//    - 交换 inputDepthIndex_ 与 outputDepthIndex_ 索引。
void DepthPeelingApp::peelAndBlend()
{
  for (int layer = 0; layer < maxPeelLayers_; ++layer)
  {
    (void)layer;

    // --- 步骤 A: 剥离当前层 (Peel Pass) ---
    glBindFramebuffer(GL_FRAMEBUFFER, fboPeel_.fbo);
    // 1. 指定当前层深度的写入目标纹理：将 GL_DEPTH_ATTACHMENT 挂载至 depthTexture(outputDepthIndex_)
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           depthTexture(outputDepthIndex_), 0);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ========== A. Peel：画的是 3D 场景（Spot + 三块玻璃），有 model/view/projection ==========
    glBeginQuery(GL_SAMPLES_PASSED, queryId_);
    // 片元经 Shader (z > frontDepth) 筛选后，硬件 Z-Test 写入本层深度纹理
    drawSceneLayer(*shaderPeel_, fboPeel_.fbo, inputDepthIndex_,
                   outputDepthIndex_);
    glEndQuery(GL_SAMPLES_PASSED);

    const GLuint sampleCount = waitSampleCount();

    // ========== B. Blend：不是再画场景；全屏贴图，把 fboPeel_.color 混进 fboAccum_ ==========
    blendPeelLayerToAccum();

    // 乒乓交换深度索引，供下一层 Peel 使用
    inputDepthIndex_ = (inputDepthIndex_ + 1) % 2;
    outputDepthIndex_ = (outputDepthIndex_ + 1) % 2;

    // 恢复深度状态，准备下一层 Peel
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    if (sampleCount <= 0)
    {
      break;
    }
  }
}

// Blend：全屏贴图 Pass（≠ Peel 再画场景）
// blend.vert/frag 几乎只产出 src；叠进 fboAccum_ 靠 glBlend* —— docs/Blend全屏脚手架与硬件混合.md
void DepthPeelingApp::blendPeelLayerToAccum()
{
  shaderBlend_->use();
  glEnable(GL_BLEND);
  glDepthMask(GL_FALSE);    // 不写深度（深度乒乓留给 Peel）
  glDisable(GL_DEPTH_TEST); // 全屏必覆盖每个像素
  // C' = Csrc·Adst + Cdst ;  A' = Adst·(1-Asrc)  —— 公式在硬件，不在 blend.frag
  glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO,
                      GL_ONE_MINUS_SRC_ALPHA);

  // 全屏无 MVP quad（blend.vert：gl_Position=aPos），不是再画 3D 场景/玻璃
  modelQuad_->Draw(
      *shaderBlend_,                                  // Blend 脚手架着色器
      fboAccum_.fbo,                                  // 画到 accum；FBO 已有像素 = dst（累加色，不必当纹理传入）
      {{"texture_diffuse", fboPeel_.color.id}},       // 采样本层 peel → FragColor = src
      {},                                             // 无 cubemap
      GL_TRIANGLES,                                   // 绘制模式
      {false, true});                                 // 不清颜色（保住 dst），只清深度
}

// Final：全屏画到默认 FBO 0（上屏）。背景合成在 final.frag 里做，不是 glBlend*
void DepthPeelingApp::compositeToScreen()
{
  shaderFinal_->use();
  shaderFinal_->setVec3("background_color", bgColor_);
  modelQuad_->Draw(
      *shaderFinal_,
      0,                                          // 默认帧缓冲 = 上屏
      {{"texture_diffuse", fboAccum_.color.id}},  // 读累积结果
      {},
      GL_TRIANGLES,
      {true, true});                              // 清屏后再画
}

void DepthPeelingApp::endFrame()
{
  glfwSwapBuffers(window_);
  glfwPollEvents();
}
