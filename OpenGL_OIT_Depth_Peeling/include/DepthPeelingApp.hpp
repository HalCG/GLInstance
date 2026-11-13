#ifndef DEPTH_PEELING_APP_HPP
#define DEPTH_PEELING_APP_HPP

#include "AppConfig.hpp"
#include "GlFramebuffer.hpp"
#include "Mesh.hpp"
#include "Model.hpp"
#include "OrbitCamera.hpp"
#include "Shader.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>

/**
 * Depth Peeling 透明渲染演示（与原 main.cpp 流程一致）
 *
 * Pass 1: 初始化 FBO_0 / FBO_1 的颜色与深度
 * Pass 2: 逐层剥离并混合到 FBO_0
 * Pass 3: 全屏合成到默认帧缓冲
 */
class DepthPeelingApp {
public:
  bool init();
  void run();
  void shutdown();

  static void framebufferSizeCallback(GLFWwindow *window, int width,
                                      int height);
  static void processInput(GLFWwindow *window);

private:
  bool initWindow();
  bool initShaders();
  bool initScene();
  bool initFramebuffers();

  void beginFrame();
  void initPeelBuffers();
  void peelAndBlend();
  void compositeToScreen();
  void endFrame();

  glm::mat4 modelMatrix(const glm::vec3 &translation,
                        float scale = 0.5f) const;
  /** Peel：用 3D MVP 把整场景（Spot+玻璃）再画一遍，写入 fboPeel_ */
  void drawSceneLayer(Shader &shader, GLuint targetFbo, int inputDepthIndex,
                      int outputDepthIndex);
  /** Blend：全屏 quad（无 MVP），采样 fboPeel_.color，硬件混合进 fboAccum_ */
  void blendPeelLayerToAccum();
  GLuint depthTexture(int index) const;
  GLuint waitSampleCount();

  GLFWwindow *window_ = nullptr;

  unsigned int width_ = AppConfig::kInitialWidth;
  unsigned int height_ = AppConfig::kInitialHeight;

  OrbitCamera camera_;
  glm::vec3 lightPos_ = AppConfig::lightPosition();
  glm::vec3 lightCoeffs_ = AppConfig::materialCoeffs();

  std::unique_ptr<Shader> shaderPeel_;
  std::unique_ptr<Shader> shaderBlend_;
  std::unique_ptr<Shader> shaderFinal_;

  std::unique_ptr<Model> modelQuad_;
  std::unique_ptr<Model> modelSpot_;
  std::unique_ptr<Texture> texWindowR_;
  std::unique_ptr<Texture> texWindowG_;
  std::unique_ptr<Texture> texWindowB_;
  std::unique_ptr<Texture> texSpot_;

  GlFramebuffer fboAccum_;
  GlFramebuffer fboPeel_;

  GLuint queryId_ = 0;

  int inputDepthIndex_ = 0;
  int outputDepthIndex_ = 1;
  int maxPeelLayers_ = 8;
  bool autoRotate_ = true;
  glm::vec3 bgColor_ = glm::vec3(0.0f, 0.0f, 0.0f);

  static DepthPeelingApp *s_instance_;
};

#endif
