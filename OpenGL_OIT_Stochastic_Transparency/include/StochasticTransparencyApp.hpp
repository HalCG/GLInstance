#ifndef STOCHASTIC_TRANSPARENCY_APP_HPP
#define STOCHASTIC_TRANSPARENCY_APP_HPP

#include "AppConfig.hpp"
#include "Mesh.hpp"
#include "Model.hpp"
#include "Shader.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>

/**
 * Stochastic Transparency：MSAA sample-mask 随机覆盖的 OIT 演示
 *
 * 透明片元按 alpha 随机打开一部分 sample，深度测试像不透明一样写入，无需按深度排序。
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

  void beginFrame();
  void renderScene();
  void endFrame();

  glm::mat4 modelMatrix(const glm::vec3 &translation,
                        float scale = 0.5f) const;

  GLFWwindow *window_ = nullptr;

  unsigned int width_ = AppConfig::kInitialWidth;
  unsigned int height_ = AppConfig::kInitialHeight;

  float viewRotate_ = AppConfig::kInitialOrbitAngle;

  std::unique_ptr<Shader> shaderQuad_;

  std::unique_ptr<Model> modelQuad_;
  std::unique_ptr<Model> modelSpot_;

  std::unique_ptr<Texture> texWindowR_;
  std::unique_ptr<Texture> texWindowG_;
  std::unique_ptr<Texture> texWindowB_;
  std::unique_ptr<Texture> texSpot_;

  bool autoRotate_ = true;
  glm::vec3 bgColor_ = glm::vec3(0.2f, 0.3f, 0.3f);

  static StochasticTransparencyApp *s_instance_;
};

#endif
