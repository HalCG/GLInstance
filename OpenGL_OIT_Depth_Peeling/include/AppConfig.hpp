#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP

#include <glm/glm.hpp>

/** 应用级常量：窗口、资源路径、相机与光照默认值 */
namespace AppConfig {
constexpr unsigned int kInitialWidth = 800;
constexpr unsigned int kInitialHeight = 600;

constexpr const char *kResourceRoot = "resources/";
constexpr const char *kWindowTitle = "OpenGL OIT - Depth Peeling";

constexpr float kInitialOrbitAngle = 45.0f;

static const glm::vec3 &lightPosition() {
  static const glm::vec3 v(2.0f, 2.0f, 0.0f);
  return v;
}
/** k.x=环境光, k.y=漫反射, k.z=高光 */
static const glm::vec3 &materialCoeffs() {
  static const glm::vec3 v(0.4f, 0.4f, 0.2f);
  return v;
}
} // namespace AppConfig

#endif
