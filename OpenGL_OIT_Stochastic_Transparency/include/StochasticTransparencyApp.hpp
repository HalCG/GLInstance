#ifndef STOCHASTIC_TRANSPARENCY_APP_HPP
#define STOCHASTIC_TRANSPARENCY_APP_HPP

#include "AppConfig.hpp"
#include "Mesh.hpp"
#include "Model.hpp"
#include "Shader.hpp"
#include "stb_image.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>

/**
 * Stochastic Transparency 各向同性透明度渲染演示
 *
 * 使用 GLSL 深度测试与随机化避免排序：
 * 1. 初始化链表结构和着色器
 * 2. 逐对象多 pass 深度测试与随机化混合
 * 3. 全屏合成到默认帧缓冲
 */
class StochasticTransparencyApp {
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
  bool initBuffers();

  void beginFrame();
  void renderScene();
  void endFrame();

  glm::mat4 modelMatrix(const glm::vec3 &translation,
                        float scale = 0.5f) const;

  GLFWwindow *window_ = nullptr;

  unsigned int width_ = AppConfig::kInitialWidth;
  unsigned int height_ = AppConfig::kInitialHeight;

  glm::vec3 cameraPos_ = AppConfig::cameraPosition();
  float viewRotate_ = AppConfig::kInitialOrbitAngle;

  std::unique_ptr<Shader> shaderQuad_;

  std::unique_ptr<Model> modelQuad_;
  std::unique_ptr<Model> modelSpot_;
  
  std::unique_ptr<Texture> texWindowR_;
  std::unique_ptr<Texture> texWindowG_;
  std::unique_ptr<Texture> texWindowB_;
  std::unique_ptr<Texture> texSpot_;

  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint indexVbo_ = 0;

  bool autoRotate_ = true;
  glm::vec3 bgColor_ = glm::vec3(0.2f, 0.3f, 0.3f);

  static StochasticTransparencyApp *s_instance_;
};

#endif
