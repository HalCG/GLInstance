#include "DepthPeelingApp.hpp"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

DepthPeelingApp *DepthPeelingApp::s_instance_ = nullptr;

// ---------------------------------------------------------------------------
// GLFW 回调（通过静态实例转发到成员状态）
// ---------------------------------------------------------------------------
void DepthPeelingApp::framebufferSizeCallback(GLFWwindow * /*window*/,
                                              int width, int height)
{
  if (s_instance_)
  {
    s_instance_->width_ = static_cast<unsigned int>(width);
    s_instance_->height_ = static_cast<unsigned int>(height);
    s_instance_->camera_.setAspectFromViewport(
        s_instance_->width_, s_instance_->height_);
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
  glfwWindowHint(GLFW_SAMPLES, 4);

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
  // 开启 MSAA：配合 GLFW 4x 采样点 (GLFW_SAMPLES=4)，对多边形边缘与半透明片元做多重采样抗锯齿平滑。
  glEnable(GL_MULTISAMPLE);

  camera_.orbitAngleDeg = AppConfig::kInitialOrbitAngle;
  camera_.setAspectFromViewport(width_, height_);
  return true;
}

bool DepthPeelingApp::initShaders()
{
  const std::string root = AppConfig::kResourceRoot;
  shaderInit_ = std::make_unique<Shader>(
      (root + "depth_peeling_init.vert").c_str(),
      (root + "depth_peeling_init.frag").c_str());
  shaderPeel_ = std::make_unique<Shader>(
      (root + "depth_peeling_render.vert").c_str(),
      (root + "depth_peeling_render.frag").c_str());
  shaderBlend_ = std::make_unique<Shader>(
      (root + "depth_peeling_blend.vert").c_str(),
      (root + "depth_peeling_blend.frag").c_str());
  shaderFinal_ = std::make_unique<Shader>(
      (root + "depth_peeling_final.vert").c_str(),
      (root + "depth_peeling_final.frag").c_str());
  shaderQuad_ = std::make_unique<Shader>((root + "quad.vert").c_str(),
                                         (root + "quad.frag").c_str());
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

  texOitColor_.createColorHDR(w, h);
  glGenFramebuffers(1, &fboOit_);
  glBindFramebuffer(GL_FRAMEBUFFER, fboOit_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texOitColor_.id, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         fboAccum_.depth.id, 0);
  const GLenum bufs[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
  glDrawBuffers(2, bufs);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
  {
    std::cout << "ERROR::FRAMEBUFFER:: OIT framebuffer is not complete!"
              << std::endl;
  }
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    initPeelBuffers();
    peelAndBlend();
    compositeToScreen();
    endFrame();
  }
}

void DepthPeelingApp::shutdown()
{
  if (queryId_ != 0)
  {
    glDeleteQueries(1, &queryId_);
    queryId_ = 0;
  }
  if (fboOit_ != 0)
  {
    glDeleteFramebuffers(1, &fboOit_);
    fboOit_ = 0;
  }
  texOitColor_.destroy();
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
  shader.setVec3("cameraPos", cameraPos_);
  shader.setVec3("lightPos", lightPos_);
  shader.setVec3("k", lightCoeffs_);
  shader.setVec2("u_ScreenSize",
                 glm::vec2(static_cast<float>(width_),
                           static_cast<float>(height_)));

  // 辅助绘图 Lambda 函数：传入网格模型、世界空间位置 pos、以及漫反射纹理 ID (diffuseId)
  // diffuseId 作用：向 Shader "texture_diffuse" 绑定纹理，提供物体的表面固有颜色 (RGB) 和片元 Alpha 透明度 (A)
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
//    - 使用 Front-to-Back 混合公式 (`GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA`) 将当前剥离层的颜色融合到 fboAccum_.fbo 中。
// 4. 乒乓交换 (Ping-Pong Swap)：
//    - 交换 inputDepthIndex_ 与 outputDepthIndex_ 索引。
void DepthPeelingApp::peelAndBlend()
{
  for (int layer = 0; layer < AppConfig::kMaxDepthPeelLayers; ++layer)
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

    // 开启遮挡查询统计当前层绘制片元数
    glBeginQuery(GL_SAMPLES_PASSED, queryId_);
    // 2. 触发绘制：片元通过 Shader (z > frontDepth) 筛选后，OpenGL 硬件管线(GL_LESS + glDepthMask(TRUE))
    //    自动将当前剥离出的深度 gl_FragCoord.z 写入上述绑定的 depthTexture(outputDepthIndex_) 纹理中！
    drawSceneLayer(*shaderPeel_, fboPeel_.fbo, inputDepthIndex_,
                   outputDepthIndex_);
    glEndQuery(GL_SAMPLES_PASSED);

    const GLuint sampleCount = waitSampleCount();

    // --- 步骤 B: 混合当前层到累积缓冲区 (Blend Pass) ---
    shaderBlend_->use();
    glEnable(GL_BLEND);       // 开启硬件混合（关混合=覆盖写入；开混合=将新旧颜色按公式融算）
    glDepthMask(GL_FALSE);    // 禁用深度写入
    glDisable(GL_DEPTH_TEST); // 禁用深度测试
    // Front-to-Back 混合规则：
    // Source (源): 当前刚剥离出的浅层颜色 (fboPeel_.color)； Destination (目标): FBO 已有旧累积颜色 (fboAccum_.color)
    // 实体遮挡原理: 若剥离出 Alpha=1 的实体，剩余透光率算式为 (1 - 1) = 0 归零；后续所有剥离层颜色均乘以 0 彻底无效（自动遮挡）！
    // RGB公式: C_final = C_src * A_dst + C_dst;  Alpha公式: A_final = A_dst * (1 - A_src)
    glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO,
                        GL_ONE_MINUS_SRC_ALPHA);

    // 触发绘制：硬件自动将 fboPeel_.color (Source) 融进 fboAccum_.fbo (Destination)
    modelQuad_->Draw(*shaderBlend_, fboAccum_.fbo,
                     {{"texture_diffuse", fboPeel_.color.id}}, {},
                     GL_TRIANGLES, {false, true});

    // 乒乓索引交换
    inputDepthIndex_ = (inputDepthIndex_ + 1) % 2;
    outputDepthIndex_ = (outputDepthIndex_ + 1) % 2;

    // 恢复状态机为下一次 Peel Pass 做准备
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    // 如果本层没有剥离出任何新片元，提前终止循环！
    if (sampleCount <= 0)
    {
      break;
    }
  }
}

// 将累积的 Front-to-Back OIT 混合结果与不透明背景合成并输出到默认窗口 FBO 0
void DepthPeelingApp::compositeToScreen()
{
  shaderFinal_->use();
  shaderFinal_->setVec3("background_color", AppConfig::backgroundColor());
  modelQuad_->Draw(*shaderFinal_, 0,
                   {{"texture_diffuse", fboAccum_.color.id}}, {}, GL_TRIANGLES,
                   {true, true});
}

void DepthPeelingApp::endFrame()
{
  glfwSwapBuffers(window_);
  glfwPollEvents();
}
